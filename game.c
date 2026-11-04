/* BatoByte rule engine. Compatible with the user's array-based headers. */
#include "game.h"
#include <stddef.h>

/* Validate a power value before accessing any array. */
static int valid_power(Power p) { return p >= PW_HEAL && p < PW_COUNT; }
static int clamp(int hp) { return hp < 0 ? 0 : (hp > MAX_HP ? MAX_HP : hp); }

/* Validate all three different powers for each player before changing game. */
GameError game_init(Game *game, Power loadouts[PLAYER_COUNT][LOADOUT_SIZE]) {
    if (!game || !loadouts) return GAME_ERR_ARGUMENT;
    for (int p = 0; p < PLAYER_COUNT; ++p)
        for (int j = 0; j < LOADOUT_SIZE; ++j) {
            if (!valid_power(loadouts[p][j])) return GAME_ERR_LOADOUT;
            for (int k = j + 1; k < LOADOUT_SIZE; ++k)
                if (loadouts[p][j] == loadouts[p][k]) return GAME_ERR_LOADOUT;
        }
    *game = (Game){0};
    game->round_number = 1;
    game->match_winner = -1;
    for (int p = 0; p < PLAYER_COUNT; ++p) {
        game->players[p].hp = START_HP;
        game->players[p].round_power = PW_NONE;
        for (int j = 0; j < LOADOUT_SIZE; ++j)
            game->players[p].remaining_powers[j] = loadouts[p][j];
    }
    return GAME_OK;
}

/* Pick a still-unused power at the beginning of each round. */
GameError game_pick_round_power(Game *game, int player, Power power) {
    if (!game || player < 0 || player >= PLAYER_COUNT) return GAME_ERR_ARGUMENT;
    Player *p = &game->players[player];
    if (game->match_winner >= 0 || !valid_power(power) || p->round_power != PW_NONE)
        return GAME_ERR_POWER_PICK;
    for (int j = 0; j < LOADOUT_SIZE; ++j) {
        if (p->remaining_powers[j] == power) {
            p->round_power = power;
            p->power_activated = 0;
            return GAME_OK;
        }
    }
    return GAME_ERR_POWER_PICK;
}

/* Powers must be activated on their compatible move. Heal works with any. */
static int compatible(Power power, Move move) {
    if (power == PW_HEAL) return 1;
    if (power == PW_DOUBLE_DAMAGE || power == PW_POISON) return move == MOVE_ATTACK;
    if (power == PW_DOUBLE_DEFEND || power == PW_THORNS) return move == MOVE_DEFEND;
    if (power == PW_GUARD_BREAK || power == PW_FULL_COUNTER) return move == MOVE_COUNTER;
    return 0;
}

/* Apply each pair of moves exactly once, avoiding duplicated Attack/Attack. */
static void resolve_moves(const Move moves[2], const Power use[2],
                          int damage[2], int landed[2]) {
    damage[0] = damage[1] = landed[0] = landed[1] = 0;
    if (moves[0] == MOVE_ATTACK && moves[1] == MOVE_ATTACK) {
        damage[0] = use[1] == PW_DOUBLE_DAMAGE ? DOUBLE_DAMAGE_DMG : HIT_DMG;
        damage[1] = use[0] == PW_DOUBLE_DAMAGE ? DOUBLE_DAMAGE_DMG : HIT_DMG;
        landed[0] = landed[1] = 1; /* landed indexes the ATTACKER */
    } else {
        for (int a = 0; a < 2; ++a) {
            int d = 1 - a;
            if (moves[a] != MOVE_ATTACK) continue;
            if (moves[d] == MOVE_DEFEND) {
                if (use[d] != PW_DOUBLE_DEFEND) {
                    damage[d] = use[a] == PW_DOUBLE_DAMAGE ? DOUBLE_DAMAGE_DMG : REDUCED_HIT;
                    landed[a] = 1;
                }
            } else if (moves[d] == MOVE_COUNTER) {
                damage[a] = use[a] == PW_DOUBLE_DAMAGE ? DOUBLE_DAMAGE_DMG : HIT_DMG;
                if (use[d] == PW_FULL_COUNTER) damage[a] *= FULL_COUNTER_MULTIPLIER;
                /* Reflected attacks do not land and cannot poison defenders. */
            }
        }
    }
    /* Counter versus Defend: Guard Break and Double Defend interaction. */
    for (int c = 0; c < 2; ++c) {
        int d = 1 - c;
        if (moves[c] != MOVE_COUNTER || moves[d] != MOVE_DEFEND) continue;
        if (use[c] == PW_GUARD_BREAK) {
            damage[use[d] == PW_DOUBLE_DEFEND ? c : d] = GUARD_BREAK_DMG;
        } else if (use[d] == PW_DOUBLE_DEFEND) {
            damage[c] = DOUBLE_DEFEND_COUNTER_DMG;
        } else {
            damage[c] = REDUCED_HIT;
        }
    }
}

/* A mutual KO restarts the SAME round without consuming either chosen power. */
static void finish_round(Game *game, TurnResult *result) {
    int winner = result->round_winner;
    if (winner >= 0) {
        ++game->players[winner].round_wins;
        for (int p = 0; p < 2; ++p)
            for (int j = 0; j < LOADOUT_SIZE; ++j)
                if (game->players[p].remaining_powers[j] == game->players[p].round_power)
                    game->players[p].remaining_powers[j] = PW_NONE;
        if (game->players[winner].round_wins >= ROUNDS_TO_WIN) {
            game->match_winner = winner;
            result->outcome = TURN_MATCH_WON;
        } else {
            ++game->round_number;
            result->outcome = TURN_ROUND_WON;
        }
    } else {
        result->outcome = TURN_MUTUAL_KO;
    }
    for (int p = 0; p < 2; ++p) {
        game->players[p].hp = (winner >= 0 && game->match_winner >= 0)
                              ? game->players[p].hp : START_HP;
        game->players[p].round_power = PW_NONE;
        game->players[p].power_activated = 0;
        game->players[p].poison_turns = 0;
        game->players[p].bleed_turns = 0;
    }
    game->stall_turns = 0;
}

/* Process effects in official order and record each component separately. */
GameError resolve_turn(Game *game, const Move moves[2],
                       const Power use[2], TurnResult *result) {
    if (!game || !moves || !use || !result || game->match_winner >= 0)
        return GAME_ERR_ARGUMENT;
    for (int p = 0; p < 2; ++p) {
        const Player *pl = &game->players[p];
        if (moves[p] < MOVE_ATTACK || moves[p] >= MOVE_COUNT)
            return GAME_ERR_INVALID_MOVE;
        if (!valid_power(pl->round_power)) return GAME_ERR_INVALID_ACTIVATION;
        if (use[p] != PW_NONE &&
            (use[p] != pl->round_power || pl->power_activated ||
             !compatible(use[p], moves[p])))
            return GAME_ERR_INVALID_ACTIVATION;
    }
    TurnResult next = {0};
    next.round_winner = -1;
    int damage[2], landed[2];

    /* 1. Old poison/bleed ticks. New effects do not tick this turn. */
    for (int p = 0; p < 2; ++p) {
        Player *pl = &game->players[p];
        if (pl->poison_turns > 0) { next.tick_damage[p] += POISON_DMG; --pl->poison_turns; }
        if (pl->bleed_turns > 0) { next.tick_damage[p] += BLEED_DMG; --pl->bleed_turns; }
    }

    /* 2. Resolve move damage using actual activated powers only. */
    resolve_moves(moves, use, damage, landed);
    for (int p = 0; p < 2; ++p) next.move_damage[p] = damage[p];

    /* 3. New Poison and Thorns apply only if the ATTACKER landed a hit. */
    for (int a = 0; a < 2; ++a) {
        int d = 1 - a;
        if (moves[a] == MOVE_ATTACK && landed[a]) {
            if (use[a] == PW_POISON) game->players[d].poison_turns = POISON_TURNS;
            if (use[d] == PW_THORNS) game->players[a].bleed_turns = BLEED_TURNS;
        }
    }

    /* 4. Apply tick and move damage, then capped Heal. */
    for (int p = 0; p < 2; ++p) {
        int hp = clamp(game->players[p].hp - next.tick_damage[p] - damage[p]);
        if (use[p] == PW_HEAL) {
            int after = clamp(hp + HEAL_AMT);
            next.healing[p] = after - hp; /* Actual HP restored, not nominal 30. */
            hp = after;
        }
        next.final_hp[p] = hp;
    }

    /* 5. Stall counts move damage ONLY. Penalty occurs after Heal/cap. */
    if (damage[0] == 0 && damage[1] == 0) ++game->stall_turns;
    else game->stall_turns = 0;
    if (game->stall_turns > STALL_FREE_TURNS)
        for (int p = 0; p < 2; ++p) {
            next.stall_penalty[p] = STALL_PENALTY;
            next.final_hp[p] = clamp(next.final_hp[p] - STALL_PENALTY);
        }

    /* 6. Commit both players' state and check round result. */
    for (int p = 0; p < 2; ++p) {
        game->players[p].hp = next.final_hp[p];
        if (use[p] != PW_NONE) game->players[p].power_activated = 1;
    }
    if (next.final_hp[0] == 0 && next.final_hp[1] == 0) {
        finish_round(game, &next);
    } else if (next.final_hp[0] == 0 || next.final_hp[1] == 0) {
        next.round_winner = next.final_hp[0] == 0 ? 1 : 0;
        finish_round(game, &next);
    } else {
        next.outcome = TURN_CONTINUES;
    }
    *result = next; /* Keep final HP from completed turn even if round reset. */
    return GAME_OK;
}
