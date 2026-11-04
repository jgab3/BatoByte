/* Engine regression tests for official BatoByte rules. */
#include <assert.h>
#include <stdio.h>
#include "game.h"

static void setup(Game *g, Power p0, Power p1) {
    Power loadout[2][3] = {{p0,PW_HEAL,PW_THORNS},{p1,PW_HEAL,PW_POISON}};
    /* For tests with duplicate requested powers, provide other unique values. */
    for (int p=0;p<2;p++) for (int j=0;j<3;j++)
        for (int k=0;k<j;k++) if(loadout[p][k]==loadout[p][j])
            for(int candidate=PW_HEAL;candidate<PW_COUNT;candidate++) {
                int exists=0;for(int t=0;t<j;t++)if(loadout[p][t]==candidate)exists=1;
                if(!exists){loadout[p][j]=(Power)candidate;break;}
            }
    assert(game_init(g,loadout)==GAME_OK);
    assert(game_pick_round_power(g,0,p0)==GAME_OK);
    assert(game_pick_round_power(g,1,p1)==GAME_OK);
}
static TurnResult turn(Game *g, Move m0, Move m1, Power u0, Power u1) {
    Move moves[2]={m0,m1};Power use[2]={u0,u1};TurnResult r;
    assert(resolve_turn(g,moves,use,&r)==GAME_OK);return r;
}
int main(void) {
    Game g;TurnResult r;
    setup(&g,PW_POISON,PW_HEAL);
    r=turn(&g,MOVE_ATTACK,MOVE_DEFEND,PW_POISON,PW_NONE);
    assert(r.move_damage[1]==15 && g.players[1].poison_turns==3);
    r=turn(&g,MOVE_DEFEND,MOVE_DEFEND,PW_NONE,PW_NONE);
    assert(r.tick_damage[1]==10 && g.players[1].poison_turns==2);

    setup(&g,PW_THORNS,PW_HEAL);
    r=turn(&g,MOVE_DEFEND,MOVE_ATTACK,PW_THORNS,PW_NONE);
    assert(r.move_damage[0]==15 && g.players[1].bleed_turns==3);
    r=turn(&g,MOVE_DEFEND,MOVE_DEFEND,PW_NONE,PW_NONE);
    assert(r.tick_damage[1]==5);

    setup(&g,PW_DOUBLE_DEFEND,PW_DOUBLE_DAMAGE);
    r=turn(&g,MOVE_DEFEND,MOVE_ATTACK,PW_DOUBLE_DEFEND,PW_DOUBLE_DAMAGE);
    assert(r.move_damage[0]==0 && r.move_damage[1]==0);

    setup(&g,PW_FULL_COUNTER,PW_DOUBLE_DAMAGE);
    r=turn(&g,MOVE_COUNTER,MOVE_ATTACK,PW_FULL_COUNTER,PW_DOUBLE_DAMAGE);
    assert(r.move_damage[1]==120 && r.outcome==TURN_ROUND_WON);

    setup(&g,PW_HEAL,PW_THORNS);
    g.players[0].hp=90;g.stall_turns=2;
    r=turn(&g,MOVE_DEFEND,MOVE_DEFEND,PW_HEAL,PW_NONE);
    assert(r.final_hp[0]==90 && r.final_hp[1]==90 && r.stall_penalty[0]==10);
    assert(r.healing[0]==10);

    setup(&g,PW_DOUBLE_DAMAGE,PW_HEAL);
    g.players[0].hp=30;g.players[1].hp=30;
    r=turn(&g,MOVE_ATTACK,MOVE_ATTACK,PW_DOUBLE_DAMAGE,PW_NONE);
    assert(r.outcome==TURN_MUTUAL_KO && g.round_number==1);
    assert(g.players[0].hp==100 && g.players[1].hp==100);
    assert(g.players[0].remaining_powers[0]==PW_DOUBLE_DAMAGE);
    assert(g.players[0].round_power==PW_NONE);

    setup(&g,PW_HEAL,PW_POISON);
    g.players[0].hp=15;g.players[1].hp=15;
    r=turn(&g,MOVE_ATTACK,MOVE_ATTACK,PW_HEAL,PW_NONE);
    assert(r.final_hp[0]==30 && r.final_hp[1]==0 && r.outcome==TURN_ROUND_WON);
    assert(g.round_number==2 && g.players[0].round_wins==1);
    puts("PASS: poison, thorns, defend, counter, heal/stall, mutual KO, round win");
    return 0;
}
