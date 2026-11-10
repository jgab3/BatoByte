CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11
ifeq ($(OS),Windows_NT)
EXE = .exe
LDLIBS = -lws2_32
endif

all: server$(EXE) client$(EXE)

server$(EXE): server.c game.c net.c game.h common.h net.h
	$(CC) $(CFLAGS) server.c game.c net.c -o $@ $(LDLIBS)

client$(EXE): client.c net.c game.h common.h net.h
	$(CC) $(CFLAGS) client.c net.c -o $@ $(LDLIBS)

tests$(EXE): tests.c game.c game.h common.h
	$(CC) $(CFLAGS) tests.c game.c -o $@

test: all tests$(EXE)
	./tests$(EXE)
	python integration_test.py

.PHONY: all test
