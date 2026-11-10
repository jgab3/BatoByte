#include "game.h"

#include <stddef.h>

GameError game_init(Game *game,
					Power loadouts[PLAYER_COUNT][LOADOUT_SIZE])
{
	int player;
	int power;
	int other_power;

	if (game == NULL || loadouts == NULL) {
		return GAME_ERR_ARGUMENT;
	}

	for (player = 0; player < PLAYER_COUNT; ++player) {
		for (power = 0; power < LOADOUT_SIZE; ++power) {
			if (loadouts[player][power] < PW_HEAL ||
				loadouts[player][power] >= PW_COUNT) {
				return GAME_ERR_LOADOUT;
			}
			for (other_power = power + 1;
				 other_power < LOADOUT_SIZE;
				 ++other_power) {
				if (loadouts[player][power] == loadouts[player][other_power]) {
					return GAME_ERR_LOADOUT;
				}
			}
		}
	}

	*game = (Game){0};
	for (player = 0; player < PLAYER_COUNT; ++player) {
		game->players[player].hp = START_HP;
		game->players[player].round_power = PW_NONE;
		for (power = 0; power < LOADOUT_SIZE; ++power) {
			game->players[player].remaining_powers[power] =
				loadouts[player][power];
		}
	}
	game->stall_enabled = 1;
	game->round_number = 1;
	game->match_winner = -1;
	return GAME_OK;
}

GameError game_init_basic(Game *game)
{
	int player;
	int power;

	if (game == NULL) {
		return GAME_ERR_ARGUMENT;
	}

	*game = (Game){0};
	for (player = 0; player < PLAYER_COUNT; ++player) {
		game->players[player].hp = START_HP;
		for (power = 0; power < LOADOUT_SIZE; ++power) {
			game->players[player].remaining_powers[power] =
				power == 0 ? PW_HEAL : PW_NONE;
		}
		game->players[player].round_power = PW_NONE;
	}
	game->stall_enabled = 0;
	game->round_number = 1;
	game->match_winner = -1;
	return GAME_OK;
}

GameError game_pick_round_power(Game *game, int player, Power power)
{
	int i;
	int found = 0;

	if (game == NULL || player < 0 || player >= PLAYER_COUNT) {
		return GAME_ERR_ARGUMENT;
	}
	if (game->match_winner >= 0 || power < PW_HEAL || power >= PW_COUNT) {
		return GAME_ERR_POWER_PICK;
	}
	if (game->players[player].round_power != PW_NONE) {
		return GAME_ERR_POWER_PICK;
	}

	for (i = 0; i < LOADOUT_SIZE; ++i) {
		if (game->players[player].remaining_powers[i] == power) {
			found = 1;
		}
	}
	if (!found) {
		return GAME_ERR_POWER_PICK;
	}

	game->players[player].round_power = power;
	game->players[player].power_activated = 0;
	return GAME_OK;
}

static void resolve_moves(const Move moves[PLAYER_COUNT],
						  const Power use[PLAYER_COUNT],
						  int damage[PLAYER_COUNT],
						  int landed[PLAYER_COUNT])
{
	int player_zero_double_defend = use[0] == PW_DOUBLE_DEFEND;
	int player_one_double_defend = use[1] == PW_DOUBLE_DEFEND;

	damage[0] = 0;
	damage[1] = 0;
	landed[0] = 0;
	landed[1] = 0;

	if (moves[0] == MOVE_ATTACK && moves[1] == MOVE_ATTACK) {
		damage[0] = use[1] == PW_DOUBLE_DAMAGE ? DOUBLE_DAMAGE_DMG : HIT_DMG;
		damage[1] = use[0] == PW_DOUBLE_DAMAGE ? DOUBLE_DAMAGE_DMG : HIT_DMG;
		landed[0] = damage[0] > 0;
		landed[1] = damage[1] > 0;
	} else if (moves[0] == MOVE_ATTACK && moves[1] == MOVE_DEFEND) {
		if (!player_one_double_defend) {
			damage[1] = use[0] == PW_DOUBLE_DAMAGE
							? DOUBLE_DAMAGE_DMG
							: REDUCED_HIT;
			landed[1] = damage[1] > 0;
		}
	} else if (moves[1] == MOVE_ATTACK && moves[0] == MOVE_DEFEND) {
		if (!player_zero_double_defend) {
			damage[0] = use[1] == PW_DOUBLE_DAMAGE
							? DOUBLE_DAMAGE_DMG
							: REDUCED_HIT;
			landed[0] = damage[0] > 0;
		}
	} else if (moves[0] == MOVE_ATTACK && moves[1] == MOVE_COUNTER) {
		damage[0] = use[0] == PW_DOUBLE_DAMAGE ? DOUBLE_DAMAGE_DMG : HIT_DMG;
		if (use[1] == PW_FULL_COUNTER) {
			damage[0] *= FULL_COUNTER_MULTIPLIER;
		}
	} else if (moves[1] == MOVE_ATTACK && moves[0] == MOVE_COUNTER) {
		damage[1] = use[1] == PW_DOUBLE_DAMAGE ? DOUBLE_DAMAGE_DMG : HIT_DMG;
		if (use[0] == PW_FULL_COUNTER) {
			damage[1] *= FULL_COUNTER_MULTIPLIER;
		}
	} else if (moves[0] == MOVE_DEFEND && moves[1] == MOVE_COUNTER) {
		if (player_zero_double_defend && use[1] == PW_GUARD_BREAK) {
			damage[1] = REDUCED_HIT;
		} else if (player_zero_double_defend) {
			damage[1] = DOUBLE_DEFEND_COUNTER_DMG;
		} else if (use[1] == PW_GUARD_BREAK) {
			damage[0] = GUARD_BREAK_DMG;
		} else {
			damage[1] = REDUCED_HIT;
		}
	} else if (moves[1] == MOVE_DEFEND && moves[0] == MOVE_COUNTER) {
		if (player_one_double_defend && use[0] == PW_GUARD_BREAK) {
			damage[0] = REDUCED_HIT;
		} else if (player_one_double_defend) {
			damage[0] = DOUBLE_DEFEND_COUNTER_DMG;
		} else if (use[0] == PW_GUARD_BREAK) {
			damage[1] = GUARD_BREAK_DMG;
		} else {
			damage[0] = REDUCED_HIT;
		}
	}
}

static void finish_round(Game *game, TurnResult *result)
{
	int player;
	int i;
	int winner = result->round_winner;

	if (winner < 0) {
		for (player = 0; player < PLAYER_COUNT; ++player) {
			game->players[player].hp = START_HP;
			game->players[player].poison_turns = 0;
			game->players[player].bleed_turns = 0;
			game->players[player].round_power = PW_NONE;
			game->players[player].power_activated = 0;
		}
		game->stall_turns = 0;
		result->outcome = TURN_MUTUAL_KO;
		return;
	}

	++game->players[winner].round_wins;
	for (player = 0; player < PLAYER_COUNT; ++player) {
		for (i = 0; i < LOADOUT_SIZE; ++i) {
			if (game->players[player].remaining_powers[i] ==
				game->players[player].round_power) {
				game->players[player].remaining_powers[i] = PW_NONE;
			}
		}
		game->players[player].round_power = PW_NONE;
		game->players[player].power_activated = 0;
		game->players[player].poison_turns = 0;
		game->players[player].bleed_turns = 0;
	}
	game->stall_turns = 0;

	if (game->players[winner].round_wins >= ROUNDS_TO_WIN) {
		game->match_winner = winner;
		result->outcome = TURN_MATCH_WON;
	} else {
		++game->round_number;
		for (player = 0; player < PLAYER_COUNT; ++player) {
			game->players[player].hp = START_HP;
		}
		result->outcome = TURN_ROUND_WON;
	}
}

GameError resolve_turn(Game *game,
					   const Move moves[PLAYER_COUNT],
					   const Power use[PLAYER_COUNT],
					   TurnResult *result)
{
	TurnResult next = {0};
	int move_damage[PLAYER_COUNT];
	int tick_damage[PLAYER_COUNT] = {0, 0};
	int landed[PLAYER_COUNT];
	int player;
	int stall_turn;
	Move allowed_move;
	int new_hp;

	if (game == NULL || moves == NULL || use == NULL || result == NULL) {
		return GAME_ERR_ARGUMENT;
	}
	if (game->match_winner >= 0) {
		return GAME_ERR_ARGUMENT;
	}

	for (player = 0; player < PLAYER_COUNT; ++player) {
		Power power = game->players[player].round_power;
		allowed_move = MOVE_COUNT;

		if (moves[player] < MOVE_ATTACK || moves[player] >= MOVE_COUNT) {
			return GAME_ERR_INVALID_MOVE;
		}
		if (power == PW_NONE) {
			return GAME_ERR_INVALID_ACTIVATION;
		}
		if (power == PW_DOUBLE_DAMAGE || power == PW_POISON) {
			allowed_move = MOVE_ATTACK;
		} else if (power == PW_DOUBLE_DEFEND || power == PW_THORNS) {
			allowed_move = MOVE_DEFEND;
		} else if (power == PW_GUARD_BREAK || power == PW_FULL_COUNTER) {
			allowed_move = MOVE_COUNTER;
		}
		if (use[player] != PW_NONE &&
			(use[player] != power ||
			 game->players[player].power_activated ||
			 (power != PW_HEAL && moves[player] != allowed_move))) {
			return GAME_ERR_INVALID_ACTIVATION;
		}
	}

	for (player = 0; player < PLAYER_COUNT; ++player) {
		if (game->players[player].poison_turns > 0) {
			tick_damage[player] += POISON_DMG;
			--game->players[player].poison_turns;
		}
		if (game->players[player].bleed_turns > 0) {
			tick_damage[player] += BLEED_DMG;
			--game->players[player].bleed_turns;
		}
		next.tick_damage[player] = tick_damage[player];
	}

	resolve_moves(moves, use, move_damage, landed);
	for (player = 0; player < PLAYER_COUNT; ++player) {
		next.move_damage[player] = move_damage[player];
	}

	if (moves[0] == MOVE_ATTACK && use[0] == PW_POISON && landed[1]) {
		game->players[1].poison_turns = POISON_TURNS;
	}
	if (moves[1] == MOVE_ATTACK && use[1] == PW_POISON && landed[0]) {
		game->players[0].poison_turns = POISON_TURNS;
	}
	if (moves[1] == MOVE_DEFEND && use[1] == PW_THORNS && landed[1]) {
		game->players[0].bleed_turns = BLEED_TURNS;
	}
	if (moves[0] == MOVE_DEFEND && use[0] == PW_THORNS && landed[0]) {
		game->players[1].bleed_turns = BLEED_TURNS;
	}

	if (game->stall_enabled) {
    		stall_turn = move_damage[0] == 0 && move_damage[1] == 0;
    		if (stall_turn) {
        		++game->stall_turns;
        		if (game->stall_turns > STALL_FREE_TURNS) {
           			 next.stall_penalty[0] = STALL_PENALTY;
           			 next.stall_penalty[1] = STALL_PENALTY;
       		 	}
    		} else {
        		game->stall_turns = 0;
    		}
	} else {
    		game->stall_turns = 0;
	}

	for (player = 0; player < PLAYER_COUNT; ++player) {
    		if (use[player] == PW_HEAL) {
        		next.healing[player] = HEAL_AMT;
    		}

    		new_hp = game->players[player].hp - tick_damage[player]
             		- move_damage[player] - next.stall_penalty[player]
             		+ next.healing[player];
    		if (new_hp < 0) {
        		new_hp = 0;
    		}
    		if (new_hp > MAX_HP) {
        		new_hp = MAX_HP;
    		}
    		next.final_hp[player] = new_hp;
	}

	for (player = 0; player < PLAYER_COUNT; ++player) {
		game->players[player].hp = next.final_hp[player];
		if (use[player] != PW_NONE) {
			game->players[player].power_activated = 1;
		}
	}

	next.round_winner = -1;
	next.outcome = TURN_CONTINUES;
	if (game->players[0].hp == 0 && game->players[1].hp == 0) {
		finish_round(game, &next);
	} else if (game->players[0].hp == 0 || game->players[1].hp == 0) {
		next.round_winner = game->players[0].hp == 0 ? 1 : 0;
		finish_round(game, &next);
	}

	*result = next;
	return GAME_OK;
}
