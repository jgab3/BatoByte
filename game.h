#ifndef BATOBYTE_GAME_H
#define BATOBYTE_GAME_H

#include "common.h"

typedef enum {
	GAME_OK,
	GAME_ERR_ARGUMENT,
	GAME_ERR_LOADOUT,
	GAME_ERR_POWER_PICK,
	GAME_ERR_INVALID_MOVE,
	GAME_ERR_INVALID_ACTIVATION
} GameError;

typedef enum {
	TURN_CONTINUES,
	TURN_ROUND_WON,
	TURN_MUTUAL_KO,
	TURN_MATCH_WON
} TurnOutcome;

typedef struct {
	TurnOutcome outcome;
	int round_winner; 
	int tick_damage[PLAYER_COUNT];
	int move_damage[PLAYER_COUNT];
	int stall_penalty[PLAYER_COUNT];
	int healing[PLAYER_COUNT];
	int final_hp[PLAYER_COUNT];
} TurnResult;

GameError game_init(Game *game,
					Power loadouts[PLAYER_COUNT][LOADOUT_SIZE]);

GameError game_init_basic(Game *game);

GameError game_pick_round_power(Game *game, int player, Power power);

GameError resolve_turn(Game *game,
					   const Move moves[PLAYER_COUNT],
					   const Power use[PLAYER_COUNT],
					   TurnResult *result);

#endif
