/* BatoByte POSIX TCP client; run two instances in two terminals. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "game.h"
#define LINE_CAP 256
#define DEFAULT_PORT 5000

/* Display names and move requirements for this client. */
static const char *game_power_name(Power p) {
    static const char *names[] = {"Heal", "Double Damage", "Poison", "Double Defend", "Thorns", "Guard Break", "Full Counter"};
    return p >= PW_HEAL && p < PW_COUNT ? names[p] : "None";
}
static const char *game_move_name(Move m) {
    static const char *names[] = {"Attack", "Defend", "Counter"};
    return m >= MOVE_ATTACK && m < MOVE_COUNT ? names[m] : "Unknown";
}
static int game_valid_activation(Power p, Move m) {
    return p == PW_HEAL ||
      ((p == PW_DOUBLE_DAMAGE || p == PW_POISON) && m == MOVE_ATTACK) ||
      ((p == PW_DOUBLE_DEFEND || p == PW_THORNS) && m == MOVE_DEFEND) ||
      ((p == PW_GUARD_BREAK || p == PW_FULL_COUNTER) && m == MOVE_COUNTER);
}


/* Read a complete newline-delimited message. */
static int readline(int fd,char *s,size_t cap) {
    size_t n=0;char ch;
    for(;;) {
        ssize_t k=recv(fd,&ch,1,0);
        if(k<=0)return 0;
        if(ch=='\n'){s[n]='\0';return 1;}
        if(ch=='\r')continue;
        if(n+1>=cap)return 0;
        s[n++]=ch;
    }
}
static int sendline(int fd,const char *s) {
    char msg[LINE_CAP];int n=snprintf(msg,sizeof(msg),"%s\n",s);
    if(n<0||n>=(int)sizeof(msg))return 0;
    for(int off=0;off<n;) {
        ssize_t k=send(fd,msg+off,(size_t)(n-off),0);
        if(k<=0)return 0;
        off+=(int)k;
    }
    return 1;
}
/* Use fgets instead of scanf so invalid entries do not poison input state. */
static int ask(const char *message,int min,int max) {
    char s[128],tail;int value;
    for(;;) {
        printf("%s",message);fflush(stdout);
        if(!fgets(s,sizeof(s),stdin))exit(0);
        if(sscanf(s,"%d %c",&value,&tail)==1&&value>=min&&value<=max)return value;
        printf("Please enter a number from %d to %d.\n",min,max);
    }
}
static unsigned choose_loadout(void) {
    unsigned mask=0;
    printf("\nPick THREE DIFFERENT powers for the match:\n");
    for(int slot=0;slot<LOADOUT_SIZE;) {
        for(int i=0;i<PW_COUNT;++i)if(!(mask&(1u<<i)))printf("  %d) %s\n",i+1,game_power_name((Power)i));
        int choice=ask("Power number: ",1,PW_COUNT);
        if(mask&(1u<<(choice-1)))printf("Already selected. Choose another.\n");
        else {mask|=1u<<(choice-1);++slot;}
    }
    return mask;
}
static int choose_power(unsigned remaining) {
    printf("\nSelect this round's SECRET power:\n");
    for(int i=0;i<PW_COUNT;++i)if(remaining&(1u<<i))printf("  %d) %s\n",i+1,game_power_name((Power)i));
    for(;;) {
        int choice=ask("Power number: ",1,PW_COUNT);
        if(remaining&(1u<<(choice-1)))return choice;
        printf("That power is no longer available.\n");
    }
}
static void show_result(const char *line,int own_id) {
    int v[20];char extra;
    int n=sscanf(line,
      "RESULT|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d%c",
      &v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9],
      &v[10],&v[11],&v[12],&v[13],&v[14],&v[15],&v[16],&v[17],&v[18],&v[19],&extra);
    if(n!=20){printf("Invalid result message.\n");return;}
    printf("\n--- TURN RESULT ---\n");
    for(int i=0;i<2;++i) {
        printf("Player %d: %s",i+1,game_move_name((Move)v[4+i]));
        if(v[6+i]>=0)printf(" + %s",game_power_name((Power)v[6+i]));
        printf(" | DOT: %d | Move damage: %d | Heal: %d | Stall: %d | HP: %d\n",
               v[8+i],v[10+i],v[12+i],v[14+i],v[i]);
    }
    printf("Round wins: P1 %d - P2 %d\n",v[2],v[3]);
    if(v[19]) {
        if(v[16]==-2)printf("Mutual KO! Powers restored; replaying the SAME round.\n");
        else printf("Player %d won the round!\n",v[16]+1);
    }
    if(v[18]>=0)printf("\nMATCH WINNER: Player %d%s\n",v[18]+1,
                          v[18]==own_id?" (YOU WIN!)":"");
}
int main(int argc,char **argv) {
    signal(SIGPIPE,SIG_IGN);
    if(argc>3){fprintf(stderr,"Usage: %s [IPv4 address] [port]\n",argv[0]);return 1;}
    const char *host=argc>=2?argv[1]:"127.0.0.1";
    int port=DEFAULT_PORT;
    if(argc==3) {
        char *end=NULL;long p=strtol(argv[2],&end,10);
        if(!end||*end||p<1024||p>65535){fprintf(stderr,"Invalid port\n");return 1;}
        port=(int)p;
    }
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0){perror("socket");return 1;}
    struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_port=htons((unsigned short)port);
    if(inet_pton(AF_INET,host,&addr.sin_addr)!=1){fprintf(stderr,"Enter an IPv4 address\n");close(fd);return 1;}
    if(connect(fd,(struct sockaddr *)&addr,sizeof(addr))<0){perror("connect");close(fd);return 1;}
    char line[LINE_CAP],reply[LINE_CAP];int player=0;
    while(readline(fd,line,sizeof(line))) {
        if(sscanf(line,"WELCOME|%d",&player)==1 && strncmp(line,"WELCOME|",8)==0) {
            printf("Connected as Player %d. Waiting for other player if necessary...\n",player);continue;
        }
        if(strcmp(line,"LOADOUT")==0) {
            snprintf(reply,sizeof(reply),"%u",choose_loadout());
            if(!sendline(fd,reply))break;
        } else if(strncmp(line,"POWER|",6)==0) {
            unsigned remaining=0;
            if(sscanf(line+6,"%u",&remaining)!=1)break;
            snprintf(reply,sizeof(reply),"%d",choose_power(remaining));
            if(!sendline(fd,reply))break;
        } else if(strncmp(line,"TURN|",5)==0) {
            int round,turn,hp,enemy_hp,power,already;
            if(sscanf(line+5,"%d|%d|%d|%d|%d|%d",&round,&turn,&hp,&enemy_hp,&power,&already)!=6)break;
            printf("\nRound %d, Turn %d | Your HP: %d | Opponent HP: %d\n",round,turn,hp,enemy_hp);
            printf("Your secret power: %s%s\n",game_power_name((Power)power),already?" (used)":"");
            printf("1) Attack  2) Defend  3) Counter\n");
            int move=ask("Move: ",1,3);
            int activate=0;
            if(!already && game_valid_activation((Power)power,(Move)(move-1)))
                activate=ask("Activate power this turn? (1=yes, 0=no): ",0,1);
            snprintf(reply,sizeof(reply),"%d %d",move,activate);
            if(!sendline(fd,reply))break;
            printf("Move submitted. Opponent may still be choosing...\n");
        } else if(strncmp(line,"RESULT|",7)==0) {
            show_result(line,player-1);
        } else if(strncmp(line,"ERROR|",6)==0) {
            printf("Server: %s\n",line+6);
        } else if(strcmp(line,"CLOSE")==0) {
            printf("Match connection closed.\n");break;
        } else {printf("Unexpected server message: %s\n",line);break;}
    }
    close(fd);
    return 0;
}
