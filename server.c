/* POSIX TCP BatoByte server. Compatible with the user's revised headers. */
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

/* Write a complete newline-framed protocol message to TCP stream. */
static int sendline(int fd, const char *s) {
    char msg[LINE_CAP];
    int n = snprintf(msg, sizeof(msg), "%s\n", s);
    if (n < 0 || n >= (int)sizeof(msg)) return 0;
    for (int off = 0; off < n;) {
        ssize_t k = send(fd, msg + off, (size_t)(n-off), 0);
        if (k <= 0) return 0;
        off += (int)k;
    }
    return 1;
}

/* Receive one complete newline-framed message. */
static int readline(int fd, char *s, size_t cap) {
    size_t n = 0; char c;
    for (;;) {
        ssize_t k = recv(fd, &c, 1, 0);
        if (k <= 0) return 0;
        if (c == '\n') { s[n] = '\0'; return 1; }
        if (c == '\r') continue;
        if (n + 1 >= cap) return 0;
        s[n++] = c;
    }
}

/* Convert bitmask protocol loadout into the header's 3-Power array. */
static int mask_to_loadout(unsigned mask, Power out[LOADOUT_SIZE]) {
    int count = 0;
    if ((mask >> PW_COUNT) != 0) return 0;
    for (int i = 0; i < PW_COUNT; ++i)
        if (mask & (1u << i)) {
            if (count >= LOADOUT_SIZE) return 0;
            out[count++] = (Power)i;
        }
    return count == LOADOUT_SIZE;
}

/* Turn an array into a bitmask for the existing client's menu protocol. */
static unsigned available_mask(const Player *p) {
    unsigned mask = 0;
    for (int i = 0; i < LOADOUT_SIZE; ++i)
        if (p->remaining_powers[i] >= PW_HEAL && p->remaining_powers[i] < PW_COUNT)
            mask |= 1u << p->remaining_powers[i];
    return mask;
}

/* Each player privately sends three distinct selected powers. */
static int prompt_loadout(int fd, Power out[LOADOUT_SIZE]) {
    char line[LINE_CAP], extra;
    unsigned mask;
    for (;;) {
        if (!sendline(fd, "LOADOUT") || !readline(fd, line, sizeof(line))) return 0;
        if (sscanf(line, "%u%c", &mask, &extra) == 1 && mask_to_loadout(mask, out))
            return 1;
        if (!sendline(fd, "ERROR|Select exactly three different powers.")) return 0;
    }
}

/* Prompt a player for one still-available round power. */
static int prompt_round_power(int fd, const Player *player, Power *out) {
    char line[LINE_CAP], msg[LINE_CAP], extra;
    int selection;
    unsigned mask = available_mask(player);
    snprintf(msg, sizeof(msg), "POWER|%u", mask);
    for (;;) {
        if (!sendline(fd, msg) || !readline(fd, line, sizeof(line))) return 0;
        if (sscanf(line, "%d%c", &selection, &extra) == 1 &&
            selection >= 1 && selection <= PW_COUNT && (mask & (1u << (selection-1)))) {
            *out = (Power)(selection-1);
            return 1;
        }
        if (!sendline(fd, "ERROR|Power unavailable; choose again.")) return 0;
    }
}

/* Validate power compatibility before sending choices to the engine. */
static int compatible(Power pw, Move move) {
    return pw == PW_HEAL ||
      ((pw == PW_DOUBLE_DAMAGE || pw == PW_POISON) && move == MOVE_ATTACK) ||
      ((pw == PW_DOUBLE_DEFEND || pw == PW_THORNS) && move == MOVE_DEFEND) ||
      ((pw == PW_GUARD_BREAK || pw == PW_FULL_COUNTER) && move == MOVE_COUNTER);
}

/* Obtain move and optional power activation without revealing to opponent. */
static int prompt_turn(int fd, const Game *g, int id, int turn,
                       Move *move, Power *use) {
    char line[LINE_CAP], msg[LINE_CAP], extra;
    int m, activate;
    const Player *p = &g->players[id];
    const Player *other = &g->players[1-id];
    snprintf(msg, sizeof(msg), "TURN|%d|%d|%d|%d|%d|%d", g->round_number,
             turn, p->hp, other->hp, (int)p->round_power, p->power_activated);
    for (;;) {
        if (!sendline(fd, msg) || !readline(fd, line, sizeof(line))) return 0;
        if (sscanf(line, "%d %d%c", &m, &activate, &extra) == 2 &&
            m >= 1 && m <= MOVE_COUNT && (activate == 0 || activate == 1) &&
            (!activate || (!p->power_activated && compatible(p->round_power, (Move)(m-1))))) {
            *move = (Move)(m-1);
            *use = activate ? p->round_power : PW_NONE;
            return 1;
        }
        if (!sendline(fd, "ERROR|Invalid move or incompatible/used power.")) return 0;
    }
}

/* Results are broadcast only after both players have submitted a turn. */
static int send_result(int client[2], const Game *g, const TurnResult *r,
                       const Move moves[2], const Power use[2]) {
    char msg[LINE_CAP];
    int finished = r->outcome != TURN_CONTINUES;
    int winner = r->outcome == TURN_MUTUAL_KO ? -2 : r->round_winner;
    snprintf(msg, sizeof(msg),
      "RESULT|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
      r->final_hp[0], r->final_hp[1],
      g->players[0].round_wins, g->players[1].round_wins,
      (int)moves[0], (int)moves[1], (int)use[0], (int)use[1],
      r->tick_damage[0], r->tick_damage[1],
      r->move_damage[0], r->move_damage[1],
      r->healing[0], r->healing[1],
      r->stall_penalty[0], r->stall_penalty[1],
      winner, g->round_number, g->match_winner, finished);
    return sendline(client[0], msg) && sendline(client[1], msg);
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    int port = DEFAULT_PORT;
    if (argc > 2) { fprintf(stderr, "Usage: %s [port]\n", argv[0]); return 1; }
    if (argc == 2) {
        char *end; long number = strtol(argv[1], &end, 10);
        if (*end || number < 1024 || number > 65535) return 1;
        port = (int)number;
    }
    int listener = socket(AF_INET, SOCK_STREAM, 0), client[2] = {-1,-1};
    if (listener < 0) { perror("socket"); return 1; }
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listener, 2) < 0) {
        perror("bind/listen"); close(listener); return 1;
    }
    printf("BatoByte server listening on port %d.\n", port); fflush(stdout);
    for (int i = 0; i < 2; ++i) {
        client[i] = accept(listener, NULL, NULL);
        if (client[i] < 0) goto cleanup;
        char msg[32];
        snprintf(msg, sizeof(msg), "WELCOME|%d", i+1);
        if (!sendline(client[i], msg)) goto cleanup;
        printf("Player %d connected.\n", i+1); fflush(stdout);
    }
    close(listener); listener = -1;
    Power loadout[2][LOADOUT_SIZE];
    if (!prompt_loadout(client[0], loadout[0]) ||
        !prompt_loadout(client[1], loadout[1])) goto cleanup;
    Game game;
    if (game_init(&game, loadout) != GAME_OK) goto cleanup;
    while (game.match_winner < 0) {
        for (int i = 0; i < 2; ++i) {
            Power pw;
            if (!prompt_round_power(client[i], &game.players[i], &pw) ||
                game_pick_round_power(&game, i, pw) != GAME_OK) goto cleanup;
        }
        int turn = 0;
        for (;;) {
            Move moves[2]; Power use[2]; TurnResult result;
            ++turn;
            if (!prompt_turn(client[0], &game, 0, turn, &moves[0], &use[0]) ||
                !prompt_turn(client[1], &game, 1, turn, &moves[1], &use[1])) goto cleanup;
            if (resolve_turn(&game, moves, use, &result) != GAME_OK) goto cleanup;
            if (!send_result(client, &game, &result, moves, use)) goto cleanup;
            if (result.outcome != TURN_CONTINUES) break;
        }
    }
    printf("Match winner: Player %d.\n", game.match_winner+1);
cleanup:
    for (int i = 0; i < 2; ++i) if (client[i] >= 0) {
        sendline(client[i], "CLOSE"); close(client[i]);
    }
    if (listener >= 0) close(listener);
    return 0;
}
