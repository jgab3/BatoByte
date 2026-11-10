/* Basic game rules: Attack, Defend, Counter, HP, rounds, and score. */
#ifndef BATOBYTE_GAME_H
#define BATOBYTE_GAME_H
#define START_HP 100
#define HIT_DAMAGE 30
#define DEFEND_DAMAGE 15
#define ROUNDS_TO_WIN 2
enum { ATTACK = 1, DEFEND, COUNTER };
typedef struct { int hp; int wins; int move; } Player;
static const char *move_name[] = { "", "Attack", "Defend", "Counter" };
static inline void start_round(Player players[2]) { players[0].hp = START_HP; players[1].hp = START_HP; }
static inline void resolve_turn(Player players[2]) {
    int damage[2] = {0, 0};
    int first = players[0].move, second = players[1].move;
    if (first == ATTACK && second == ATTACK) { damage[0] = HIT_DAMAGE; damage[1] = HIT_DAMAGE; }
    else if (first == ATTACK && second == DEFEND) damage[1] = DEFEND_DAMAGE;
    else if (first == DEFEND && second == ATTACK) damage[0] = DEFEND_DAMAGE;
    else if (first == ATTACK && second == COUNTER) damage[0] = HIT_DAMAGE;
    else if (first == COUNTER && second == ATTACK) damage[1] = HIT_DAMAGE;
    else if (first == DEFEND && second == COUNTER) damage[1] = DEFEND_DAMAGE;
    else if (first == COUNTER && second == DEFEND) damage[0] = DEFEND_DAMAGE;
    players[0].hp -= damage[0]; players[1].hp -= damage[1];
    if (players[0].hp < 0) players[0].hp = 0;
    if (players[1].hp < 0) players[1].hp = 0;
}
#endif
