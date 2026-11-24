# BatoByte: Power Battle

A two-player, round-based terminal game in C. One player runs the server and plays in its
terminal; the other player runs the client and connects to the server over TCP.

## Build

The source files are in the `development` folder. Build from there:

```bash
cd development
gcc game.c server.c -o server
gcc client.c -o client
```

## Run

Run both programs from the `development` folder. Start the server first (Player 1), giving it a
port number:

```bash
./server <port>
```

Then start the client (Player 2) in another terminal, giving it the server's host name or IP
address and the same port:

```bash
./client <host> <port>
```

For example, to play on one machine with two terminals:

```bash
./server 5000
./client localhost 5000
```

Both players see a short how-to-play screen when the client connects.
