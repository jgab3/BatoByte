#include <assert.h>
#include <stdio.h>
#include "../game.h"

static void check_turn(int first_move, int second_move, int first_hp, int second_hp) {
    Player players[2] = {0};
    start_round(players);
    players[0].move = first_move;
    players[1].move = second_move;
    resolve_turn(players);
    assert(players[0].hp == first_hp);
    assert(players[1].hp == second_hp);
}

int main(void) {
    check_turn(ATTACK, ATTACK, 70, 70);
    check_turn(ATTACK, DEFEND, 100, 85);
    check_turn(DEFEND, ATTACK, 85, 100);
    check_turn(ATTACK, COUNTER, 70, 100);
    check_turn(COUNTER, ATTACK, 100, 70);
    check_turn(DEFEND, COUNTER, 100, 85);
    check_turn(COUNTER, DEFEND, 85, 100);
    check_turn(DEFEND, DEFEND, 100, 100);
    check_turn(COUNTER, COUNTER, 100, 100);
    puts("PASS: basic move rules work.");
    return 0;
}
