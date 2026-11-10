/* Remote Player 2 client: displays server messages and sends keyboard input. */
#include "common.h"
int main(int argc, char *argv[]) {
    struct sockaddr_in address = {0}; char message[256], answer[64]; int server_socket;
    if (argc != 2) { printf("Usage: %s SERVER_IPV4_ADDRESS\n", argv[0]); return 1; }
    start_network(); server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) { perror("socket"); return 1; }
    address.sin_family = AF_INET; address.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &address.sin_addr) != 1 ||
        connect(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("connect"); close(server_socket); return 1;
    }
    while (receive_line(server_socket, message, sizeof(message))) {
        if (strcmp(message, "END") == 0) break;
        if (strcmp(message, "ASK") == 0) {
            if (fgets(answer, sizeof(answer), stdin) == NULL) break;
            answer[strcspn(answer, "\r\n")] = '\0';
            if (!send_line(server_socket, answer)) break;
        } else puts(message);
    }
    puts("Game ended."); close(server_socket); return 0;
}
