#ifndef BATOBYTE_GAME_H
#define BATOBYTE_GAME_H
#include "common.h"
typedef enum { GAME_OK, GAME_ERR_ARGUMENT, GAME_ERR_INVALID_MOVE } GameError;
typedef enum { TURN_CONTINUES, TURN_ROUND_WON, TURN_MUTUAL_KO, TURN_MATCH_WON } TurnOutcome;
typedef struct {
    TurnOutcome outcome;
    int round_winner;
    int move_damage[PLAYER_COUNT];
    int final_hp[PLAYER_COUNT];
} TurnResult;
GameError game_init(Game *game);
GameError resolve_turn(Game *game, const Move moves[PLAYER_COUNT], TurnResult *result);
#endif
