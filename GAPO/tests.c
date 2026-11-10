#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
int main(void) {
    /* Expected damage received by player 0 for all nine move pairs. */
    int damage[3][3] = {{30,0,30},{15,0,0},{0,15,0}};
    Game g; TurnResult r;
    for (int a=0; a<MOVE_COUNT; ++a) for (int b=0; b<MOVE_COUNT; ++b) {
        assert(game_init(&g)==GAME_OK);
        Move moves[2]={(Move)a,(Move)b};
        assert(resolve_turn(&g,moves,&r)==GAME_OK);
        assert(r.move_damage[0]==damage[a][b]);
        assert(r.move_damage[1]==damage[b][a]);
        assert(r.final_hp[0]==START_HP-damage[a][b]);
    }
    game_init(&g);
    Move moves[2]={MOVE_ATTACK,MOVE_DEFEND};
    for(int i=0;i<14;++i) assert(resolve_turn(&g,moves,&r)==GAME_OK);
    assert(g.match_winner==0 && g.players[0].round_wins==2 && r.outcome==TURN_MATCH_WON);
    assert(resolve_turn(&g,moves,&r)==GAME_ERR_ARGUMENT);
    game_init(&g); g.players[0].hp=g.players[1].hp=30;
    moves[1]=MOVE_ATTACK;
    assert(resolve_turn(&g,moves,&r)==GAME_OK);
    assert(r.outcome==TURN_MUTUAL_KO && r.final_hp[0]==0 && r.final_hp[1]==0);
    assert(g.round_number==1 && g.players[0].hp==100 && g.players[1].hp==100);
    Game before=g;
    moves[0]=MOVE_COUNT;
    assert(resolve_turn(&g,moves,&r)==GAME_ERR_INVALID_MOVE);
    assert(memcmp(&g,&before,sizeof(g))==0);
    assert(game_init(NULL)==GAME_ERR_ARGUMENT);
    puts("PASS: all nine move pairs, round/match wins, mutual KO, invalid moves");
    return 0;
}
