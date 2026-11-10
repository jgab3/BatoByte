/* Shared Linux TCP helpers for the basic BatoByte game. */
#ifndef BATOBYTE_COMMON_H
#define BATOBYTE_COMMON_H
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define PORT 5000
static inline void start_network(void) { signal(SIGPIPE, SIG_IGN); }
static inline int send_line(int socket_fd, const char *text) {
    size_t left = strlen(text);
    while (left > 0) {
        ssize_t sent = send(socket_fd, text, left, 0);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return 0;
        text += sent; left -= (size_t)sent;
    }
    return send(socket_fd, "\n", 1, 0) == 1;
}
static inline int receive_line(int socket_fd, char *text, size_t size) {
    size_t used = 0; char character;
    while (used + 1 < size) {
        ssize_t received = recv(socket_fd, &character, 1, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received != 1) return 0;
        if (character == '\n') { text[used] = '\0'; return 1; }
        if (character != '\r') text[used++] = character;
    }
    text[used] = '\0'; return 0;
}
static inline int read_number(const char *text, int *value) {
    char *end; long number;
    errno = 0; number = strtol(text, &end, 10);
    if (end == text || errno == ERANGE || number < INT_MIN || number > INT_MAX) return 0;
    while (isspace((unsigned char)*end)) end++;
    if (*end != '\0') return 0;
    *value = (int)number; return 1;
}
#endif
