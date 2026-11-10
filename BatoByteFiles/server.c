/* Host computer: server and local Player 1. Remote computer: Player 2. */
#include "common.h"
#include "game.h"

static int remote_client;

static void announce(const char *text) {
    puts(text);
    send_line(remote_client, text);
}

/* Player 1 enters locally; Player 2's valid answer comes through TCP. */
static int ask_move(int player) {
    char line[64];
    int move;
    for (;;) {
        if (player == 0) {
            printf("Player 1 move (1 Attack, 2 Defend, 3 Counter): ");
            fflush(stdout);
            if (fgets(line, sizeof(line), stdin) == NULL) exit(1);
        } else {
            send_line(remote_client, "Player 2 move (1 Attack, 2 Defend, 3 Counter):");
            send_line(remote_client, "ASK");
            if (!receive_line(remote_client, line, sizeof(line))) {
                puts("Player 2 disconnected.");
                exit(1);
            }
        }
        if (read_number(line, &move) && move >= ATTACK && move <= COUNTER) return move;
        if (player == 0) puts("Enter 1, 2, or 3.");
        else send_line(remote_client, "Invalid move. Enter 1, 2, or 3.");
    }
}

int main(void) {
    struct sockaddr_in address = {0};
    Player players[2] = {0};
    char message[160];
    int listener, reuse = 1, round = 1, turn;
    start_network();
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); return 1; }
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(listener, 1) < 0) {
        perror("server setup"); close(listener); return 1;
    }
    puts("BatoByte Basic: this computer is Server + Player 1.");
    puts("Waiting for Player 2 on TCP port 5000...");
    remote_client = accept(listener, NULL, NULL);
    close(listener);
    if (remote_client < 0) { perror("accept"); return 1; }
    announce("Player 2 connected. Basic rules: Attack, Defend, Counter.");
    while (players[0].wins < ROUNDS_TO_WIN && players[1].wins < ROUNDS_TO_WIN) {
        start_round(players);
        turn = 1;
        snprintf(message, sizeof(message), "--- Round %d | Score P1 %d - P2 %d ---", round, players[0].wins, players[1].wins);
        announce(message);
        while (players[0].hp > 0 && players[1].hp > 0) {
            snprintf(message, sizeof(message), "Turn %d | HP: P1 %d - P2 %d", turn++, players[0].hp, players[1].hp);
            announce(message);
            players[0].move = ask_move(0);
            players[1].move = ask_move(1);
            snprintf(message, sizeof(message), "P1 chose %s | P2 chose %s", move_name[players[0].move], move_name[players[1].move]);
            announce(message);
            resolve_turn(players);
            snprintf(message, sizeof(message), "After turn: P1 HP %d | P2 HP %d", players[0].hp, players[1].hp);
            announce(message);
        }
        if (players[0].hp == 0 && players[1].hp == 0) {
            announce("Mutual KO. This round will be replayed.");
            continue;
        }
        if (players[0].hp > 0) players[0].wins++; else players[1].wins++;
        snprintf(message, sizeof(message), "Player %d wins the round!", players[0].hp > 0 ? 1 : 2);
        announce(message);
        round++;
    }
    snprintf(message, sizeof(message), "Player %d wins the match!", players[0].wins == ROUNDS_TO_WIN ? 1 : 2);
    announce(message);
    send_line(remote_client, "END");
    close(remote_client);
    return 0;
}
