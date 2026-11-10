/* Server hosts the match and plays locally as Player 1. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net.h"
#include "game.h"
#define LINE_CAP 256
#define DEFAULT_PORT 5000

/* Write a complete newline-framed protocol message to TCP stream. */
static int sendline(Socket fd, const char *s) {
    char msg[LINE_CAP];
    int n = snprintf(msg, sizeof(msg), "%s\n", s);
    if (n < 0 || n >= (int)sizeof(msg)) return 0;
    for (int off = 0; off < n;) {
        int k = send(fd, msg + off, (size_t)(n-off), 0);
        if (k <= 0) return 0;
        off += (int)k;
    }
    return 1;
}

/* Receive one complete newline-framed message. */
static int readline(Socket fd, char *s, size_t cap) {
    size_t n = 0; char c;
    for (;;) {
        int k = recv(fd, &c, 1, 0);
        if (k <= 0) return 0;
        if (c == '\n') { s[n] = '\0'; return 1; }
        if (c == '\r') continue;
        if (n + 1 >= cap) return 0;
        s[n++] = c;
    }
}

/* Collect one basic move; choices are revealed after both players submit. */
static int prompt_turn(Socket fd, const Game *g, int id, int turn, Move *move) {
    char line[LINE_CAP], msg[LINE_CAP], extra;
    int selection;
    snprintf(msg, sizeof(msg), "TURN|%d|%d|%d|%d", g->round_number, turn,
             g->players[id].hp, g->players[1-id].hp);
    for (;;) {
        if (!sendline(fd, msg) || !readline(fd, line, sizeof(line))) return 0;
        if (sscanf(line, "%d %c", &selection, &extra) == 1 && selection >= 1 && selection <= MOVE_COUNT) {
            *move = (Move)(selection - 1);
            return 1;
        }
        if (!sendline(fd, "ERROR|Choose 1 Attack, 2 Defend, or 3 Counter.")) return 0;
    }
}

static int send_result(Socket client, const Game *g, const TurnResult *r, const Move moves[2]) {
    char msg[LINE_CAP];
    snprintf(msg, sizeof(msg), "RESULT|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
             r->final_hp[0], r->final_hp[1], g->players[0].round_wins, g->players[1].round_wins,
             (int)moves[0], (int)moves[1], r->move_damage[0], r->move_damage[1],
             r->outcome == TURN_MUTUAL_KO ? -2 : r->round_winner, g->match_winner, (int)r->outcome);
    return sendline(client, msg);
}

static int parse_options(int argc, char **argv, int *port) {
    *port = DEFAULT_PORT;
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        return 0;
    }
    if (argc == 2) {
        char *end; long number = strtol(argv[1], &end, 10);
        if (end == argv[1] || *end || number < 1024 || number > 65535) {
            fprintf(stderr, "Invalid port: expected 1024 through 65535.\n");
            return 0;
        }
        *port = (int)number;
    }
    return 1;
}

static Socket create_listener(int port) {
    Socket listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET_HANDLE) { net_error("socket"); return INVALID_SOCKET_HANDLE; }
    int reuse = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0) {
        net_error("setsockopt"); net_close(listener); return INVALID_SOCKET_HANDLE;
    }
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0) {
        net_error("bind"); net_close(listener); return INVALID_SOCKET_HANDLE;
    }
    if (listen(listener, 1) < 0) {
        net_error("listen"); net_close(listener); return INVALID_SOCKET_HANDLE;
    }
    printf("BatoByte server listening on port %d.\n", port); fflush(stdout);
    return listener;
}

static Socket accept_player(Socket listener) {
    Socket client = accept(listener, NULL, NULL);
    if (client == INVALID_SOCKET_HANDLE) { net_error("accept"); return client; }
    if (!sendline(client, "WELCOME|2")) {
        net_close(client);
        return INVALID_SOCKET_HANDLE;
    }
    puts("Player 2 connected. You are Player 1.");
    return client;
}

static int prompt_local_turn(const Game *game, int turn, Move *move) {
    char line[128], extra;
    int selection;
    printf("\nRound %d, Turn %d | Your HP: %d | Opponent HP: %d\n",
           game->round_number, turn, game->players[0].hp, game->players[1].hp);
    puts("1) Attack  2) Defend  3) Counter");
    for (;;) {
        printf("Player 1 move: "); fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) return 0;
        if (!strchr(line, '\n') && !feof(stdin)) {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) { }
            puts("Input too long. Choose 1, 2, or 3.");
            continue;
        }
        if (sscanf(line, "%d %c", &selection, &extra) == 1 && selection >= 1 && selection <= MOVE_COUNT) {
            *move = (Move)(selection - 1);
            return 1;
        }
        puts("Choose 1 Attack, 2 Defend, or 3 Counter.");
    }
}

static void show_local_result(const Game *game, const TurnResult *result, const Move moves[2]) {
    static const char *names[] = {"Attack", "Defend", "Counter"};
    puts("\n--- TURN RESULT ---");
    for (int p = 0; p < PLAYER_COUNT; ++p)
        printf("Player %d: %s | Damage taken: %d | HP: %d\n", p+1,
               names[moves[p]], result->move_damage[p], result->final_hp[p]);
    printf("Round wins: P1 %d - P2 %d\n", game->players[0].round_wins, game->players[1].round_wins);
    if (result->outcome == TURN_MUTUAL_KO) puts("Mutual KO! Replaying the same round.");
    else if (result->round_winner >= 0) printf("Player %d won the round!\n", result->round_winner+1);
    if (game->match_winner >= 0)
        printf("MATCH WINNER: Player %d%s\n", game->match_winner+1,
               game->match_winner == 0 ? " (YOU WIN!)" : "");
}

static int run_game_session(Socket client) {
    Game game;
    if (game_init(&game) != GAME_OK) return 0;
    while (game.match_winner < 0) {
        int turn = 0;
        for (;;) {
            Move moves[2]; TurnResult result;
            ++turn;
            if (!prompt_local_turn(&game, turn, &moves[0]) ||
                !prompt_turn(client, &game, 1, turn, &moves[1])) return 0;
            if (resolve_turn(&game, moves, &result) != GAME_OK) return 0;
            show_local_result(&game, &result, moves);
            if (!send_result(client, &game, &result, moves)) return 0;
            if (result.outcome != TURN_CONTINUES) break;
        }
    }
    printf("Match winner: Player %d.\n", game.match_winner+1);
    return 1;
}

static void close_connections(Socket listener, Socket client) {
    if (client != INVALID_SOCKET_HANDLE) {
        sendline(client, "CLOSE"); net_close(client);
    }
    if (listener != INVALID_SOCKET_HANDLE) net_close(listener);
}

int main(int argc, char **argv) {
    int port;
    Socket client = INVALID_SOCKET_HANDLE;
    if (!parse_options(argc, argv, &port)) return EXIT_FAILURE;
    if (!net_startup()) return EXIT_FAILURE;
    atexit(net_cleanup);
    printf("Server starting ...\n");
    Socket listener = create_listener(port);
    if (listener == INVALID_SOCKET_HANDLE) return EXIT_FAILURE;
    puts("You are Player 1. Waiting for Player 2... "); fflush(stdout);
    client = accept_player(listener);
    int success = client != INVALID_SOCKET_HANDLE;
    if (success) {
        net_close(listener);
        listener = INVALID_SOCKET_HANDLE;
        success = run_game_session(client);
    }
    if (!success) fprintf(stderr, "Server session ended before the match completed.\n");
    close_connections(listener, client);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
