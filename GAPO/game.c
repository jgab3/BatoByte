/* Basic simultaneous Attack / Defend / Counter rules. */
#include "game.h"

GameError game_init(Game *game) {
    if (!game) return GAME_ERR_ARGUMENT;
    *game = (Game){0};
    game->round_number = 1;
    game->match_winner = -1;
    for (int p = 0; p < PLAYER_COUNT; ++p) game->players[p].hp = START_HP;
    return GAME_OK;
}

GameError resolve_turn(Game *game, const Move moves[PLAYER_COUNT], TurnResult *result) {
    if (!game || !moves || !result || game->match_winner >= 0) return GAME_ERR_ARGUMENT;
    for (int p = 0; p < PLAYER_COUNT; ++p)
        if (moves[p] < MOVE_ATTACK || moves[p] >= MOVE_COUNT) return GAME_ERR_INVALID_MOVE;
    TurnResult next = {0};
    next.round_winner = -1;
    for (int p = 0; p < PLAYER_COUNT; ++p) {
        int other = 1 - p;
        if (moves[p] == MOVE_ATTACK) {
            if (moves[other] == MOVE_COUNTER) next.move_damage[p] += HIT_DMG;
            else next.move_damage[other] += moves[other] == MOVE_DEFEND ? REDUCED_HIT : HIT_DMG;
        } else if (moves[p] == MOVE_COUNTER && moves[other] == MOVE_DEFEND) {
            next.move_damage[p] += REDUCED_HIT;
        }
    }
    for (int p = 0; p < PLAYER_COUNT; ++p) {
        int hp = game->players[p].hp - next.move_damage[p];
        game->players[p].hp = next.final_hp[p] = hp > 0 ? hp : 0;
    }
    if (next.final_hp[0] == 0 && next.final_hp[1] == 0) {
        next.outcome = TURN_MUTUAL_KO;
    } else if (next.final_hp[0] == 0 || next.final_hp[1] == 0) {
        next.round_winner = next.final_hp[0] == 0 ? 1 : 0;
        if (++game->players[next.round_winner].round_wins == ROUNDS_TO_WIN) {
            game->match_winner = next.round_winner;
            next.outcome = TURN_MATCH_WON;
        } else {
            ++game->round_number;
            next.outcome = TURN_ROUND_WON;
        }
    }
    if (next.outcome != TURN_CONTINUES && next.outcome != TURN_MATCH_WON)
        for (int p = 0; p < PLAYER_COUNT; ++p) game->players[p].hp = START_HP;
    *result = next;
    return GAME_OK;
}
