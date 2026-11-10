
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net.h"
#include "game.h"
#define LINE_CAP 256
#define DEFAULT_PORT 5000

typedef struct {
    const char *host;
    int port;
} ClientOptions;

static int parse_options(int argc, char **argv, ClientOptions *options) {
    if (argc > 3) {
        fprintf(stderr, "Usage: %s [hostname] [port]\n", argv[0]);
        return 0;
    }
    options->host = argc >= 2 ? argv[1] : "127.0.0.1";
    options->port = DEFAULT_PORT;
    if (argc == 3) {
        char *end;
        long port = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end || port < 1024 || port > 65535) {
            fprintf(stderr, "Invalid port: expected 1024 through 65535.\n");
            return 0;
        }
        options->port = (int)port;
    }
    return 1;
}

/* Follow the handout's lookup/connect flow, using POSIX getaddrinfo. */
static Socket connect_to_server(const ClientOptions *options) {
    struct addrinfo hints = {0}, *addresses, *address;
    char service[6];
    Socket fd = INVALID_SOCKET_HANDLE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%d", options->port);
    printf("Looking for host '%s'...\n", options->host);
    int error = getaddrinfo(options->host, service, &hints, &addresses);
    if (error != 0) {
        fprintf(stderr, "Host lookup failed: %s\n", gai_strerror(error));
        return INVALID_SOCKET_HANDLE;
    }
    printf("Host found! Connecting at port %d...\n", options->port);
    for (address = addresses; address; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd == INVALID_SOCKET_HANDLE) continue;
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
        net_close(fd);
        fd = INVALID_SOCKET_HANDLE;
    }
    if (fd == INVALID_SOCKET_HANDLE) net_error("Unable to connect to server");
    freeaddrinfo(addresses);
    return fd;
}

static const char *game_move_name(Move move) {
    static const char *names[] = {"Attack", "Defend", "Counter"};
    return move >= MOVE_ATTACK && move < MOVE_COUNT ? names[move] : "Unknown";
}

/* Read a complete newline-delimited message. */
static int readline(Socket fd,char *s,size_t cap) {
    size_t n=0;char ch;
    for(;;) {
        int k=recv(fd,&ch,1,0);
        if(k<=0)return 0;
        if(ch=='\n'){s[n]='\0';return 1;}
        if(ch=='\r')continue;
        if(n+1>=cap)return 0;
        s[n++]=ch;
    }
}
static int sendline(Socket fd,const char *s) {
    char msg[LINE_CAP];int n=snprintf(msg,sizeof(msg),"%s\n",s);
    if(n<0||n>=(int)sizeof(msg))return 0;
    for(int off=0;off<n;) {
        int k=send(fd,msg+off,(size_t)(n-off),0);
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
static void show_result(const char *line, int own_id) {
    int v[11]; char extra;
    if (sscanf(line, "RESULT|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d%c",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &extra) != 11) {
        puts("Invalid result message."); return;
    }
    puts("\n--- TURN RESULT ---");
    for (int p = 0; p < 2; ++p)
        printf("Player %d: %s | Damage taken: %d | HP: %d\n", p+1,
               game_move_name((Move)v[4+p]), v[6+p], v[p]);
    printf("Round wins: P1 %d - P2 %d\n", v[2], v[3]);
    if (v[10] == TURN_MUTUAL_KO) puts("Mutual KO! Replaying the same round.");
    else if (v[8] >= 0) printf("Player %d won the round!\n", v[8]+1);
    if (v[9] >= 0) printf("MATCH WINNER: Player %d%s\n", v[9]+1, v[9] == own_id ? " (YOU WIN!)" : "");
}
static void run_game_session(Socket fd) {
    char line[LINE_CAP],reply[LINE_CAP];int player=0;
    while(readline(fd,line,sizeof(line))) {
        if(sscanf(line,"WELCOME|%d",&player)==1 && strncmp(line,"WELCOME|",8)==0) {
            printf("Connected as Player %d. Server is Player 1. Waiting for the server player...\n",player);continue;
        }
        if(strncmp(line,"TURN|",5)==0) {
            int round, turn, hp, enemy_hp;
            if(sscanf(line+5,"%d|%d|%d|%d",&round,&turn,&hp,&enemy_hp)!=4)break;
            printf("\nRound %d, Turn %d | Your HP: %d | Opponent HP: %d\n",round,turn,hp,enemy_hp);
            puts("1) Attack  2) Defend  3) Counter");
            int move=ask("Move: ",1,3);
            snprintf(reply,sizeof(reply),"%d",move);
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
}

int main(int argc, char **argv) {
    ClientOptions options;
    if (!parse_options(argc, argv, &options)) return EXIT_FAILURE;
    if (!net_startup()) return EXIT_FAILURE;
    atexit(net_cleanup);
    printf("Client starting ...\n");
    Socket client_sock = connect_to_server(&options);
    if (client_sock == INVALID_SOCKET_HANDLE) return EXIT_FAILURE;
    printf("Connection successful!\n");
    run_game_session(client_sock);
    net_close(client_sock);
    return EXIT_SUCCESS;
}
