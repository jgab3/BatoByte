BATOBYTE: POWER BATTLE — UPDATED HEADER-COMPATIBLE LOCALHOST VERSION
==================================================================

Files:
  common.h             Your updated constants and array-based game structures
  game.h               Your updated GameError / TurnResult API
  game.c               Fixed rules engine using the updated headers
  server.c             Authoritative TCP server for exactly two clients
  client.c             Terminal-based TCP client (run twice)
  tests.c              Rule tests for poison, thorns, counter, healing, rounds
  integration_test.py  Automated TCP match test with two real sockets
  CHANGES.txt          Summary of fixes

COMPILING ON UBUNTU / WSL (recommended):
  gcc -Wall -Wextra -std=c11 server.c game.c -o server
  gcc -Wall -Wextra -std=c11 client.c -o client

PLAYING ON ONE COMPUTER (THREE TERMINALS):
  Terminal 1: ./server
  Terminal 2: ./client 127.0.0.1 5000
  Terminal 3: ./client 127.0.0.1 5000

One terminal is the server. Each of the other two terminals is one player.
If the server reports 'Address already in use', stop the prior server or
choose another port: ./server 6000 and ./client 127.0.0.1 6000 twice.

OPTIONAL TESTS:
  gcc -Wall -Wextra -std=c11 tests.c game.c -o tests
  ./tests
  python3 integration_test.py  (requires compiled ./server)

GAMEPLAY:
  1. Each player privately selects three DIFFERENT powers.
  2. Each round, each selects one still-unused power privately.
  3. Each turn, pick Attack, Defend, or Counter and optionally activate
     that round's power on a compatible move.
  4. Both moves resolve on the SERVER and are then revealed.
  5. First player to win TWO rounds wins.
  6. On mutual KO, the same round is replayed and selected powers restored.
  7. On normal round end, selected powers are spent even if not activated.

IMPORTANT:
  This is a terminal TCP application, not a browser or PHP localhost game.
  It uses POSIX sockets (Ubuntu, WSL, Linux, or macOS), NOT native MinGW.
  You need GCC (and Python 3 only if running the integration test).
  The game is a local/learning demonstration, not internet-ready hosting.
