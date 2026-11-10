#ifndef BATOBYTE_NET_H
#define BATOBYTE_NET_H

/* Small socket portability layer; game rules remain platform independent. */
#include <stdio.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Socket;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#else
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
typedef int Socket;
#define INVALID_SOCKET_HANDLE (-1)
#endif

int net_startup(void);
void net_cleanup(void);
void net_close(Socket socket_fd);
void net_error(const char *operation);
#endif
