#ifndef BATOBYTE_COMMON_H
#define BATOBYTE_COMMON_H

#define START_HP 100
#define MAX_HP 100
#define HIT_DMG 30
#define REDUCED_HIT 15
#define DOUBLE_DAMAGE_DMG 60
#define FULL_COUNTER_MULTIPLIER 2
#define DOUBLE_DEFEND_COUNTER_DMG 30
#define GUARD_BREAK_DMG 15
#define HEAL_AMT 30
#define POISON_DMG 10
#define POISON_TURNS 3
#define BLEED_DMG 5
#define BLEED_TURNS 3
#define STALL_FREE_TURNS 2
#define STALL_PENALTY 10
#define ROUNDS_TO_WIN 2
#define LOADOUT_SIZE 3
#define MAX_POWERS_PER_ROUND 1
#define PLAYER_COUNT 2
#define MAX_MESSAGE_LENGTH 512
#define MAX_RELAY_LENGTH (MAX_MESSAGE_LENGTH + 16)

typedef enum { MOVE_ATTACK, MOVE_DEFEND, MOVE_COUNTER, MOVE_COUNT } Move;
typedef enum {
    PW_NONE = -1, PW_HEAL, PW_DOUBLE_DAMAGE, PW_POISON,
    PW_DOUBLE_DEFEND, PW_THORNS, PW_GUARD_BREAK, PW_FULL_COUNTER, PW_COUNT
} Power;
typedef struct {
    int hp;
    int round_wins;
    Power remaining_powers[LOADOUT_SIZE];
    Power round_power;
    int power_activated;
    int poison_turns;
    int bleed_turns;
} Player;
typedef struct {
    Player players[PLAYER_COUNT];
    int stall_turns;
    int round_number;
    int match_winner;
} Game;
#endif
