CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic

all: server client

server: server.c common.h game.h
	$(CC) $(CFLAGS) server.c -o server

client: client.c common.h
	$(CC) $(CFLAGS) client.c -o client

.PHONY: all
