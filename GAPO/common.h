#ifndef BATOBYTE_COMMON_H
#define BATOBYTE_COMMON_H
#define START_HP 100
#define HIT_DMG 30
#define REDUCED_HIT 15
#define ROUNDS_TO_WIN 2
#define PLAYER_COUNT 2
typedef enum { MOVE_ATTACK, MOVE_DEFEND, MOVE_COUNTER, MOVE_COUNT } Move;
typedef struct { int hp; int round_wins; } Player;
typedef struct {
    Player players[PLAYER_COUNT];
    int round_number;
    int match_winner;
} Game;
#endif
