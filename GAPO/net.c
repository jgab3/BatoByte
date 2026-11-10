/* Shared networking implementation for Windows and POSIX C builds. */
#define _POSIX_C_SOURCE 200809L
#include "net.h"

#ifdef _WIN32
int net_startup(void) {
    WSADATA data;
    int error = WSAStartup(MAKEWORD(2, 2), &data);
    if (error) fprintf(stderr, "WSAStartup failed: %d\n", error);
    return error == 0;
}
void net_cleanup(void) { WSACleanup(); }
void net_close(Socket socket_fd) { closesocket(socket_fd); }
void net_error(const char *operation) {
    fprintf(stderr, "%s failed: Winsock error %d\n", operation, WSAGetLastError());
}
#else
int net_startup(void) { signal(SIGPIPE, SIG_IGN); return 1; }
void net_cleanup(void) { }
void net_close(Socket socket_fd) { close(socket_fd); }
void net_error(const char *operation) { perror(operation); }
#endif
