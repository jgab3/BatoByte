# BatoByte

The server is Player 1 and the client is Player 2. The game uses Attack,
Defend, and Counter.

Build and test from PowerShell on Windows (MSYS2 UCRT64 GCC):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Test
```

Play in two terminals, all opened in this directory:

```powershell
# Terminal 1
.\server.exe
# Terminal 2
.\client.exe localhost 5000
```

The server terminal is Player 1; the client terminal is Player 2. Choose Attack, Defend, or Counter each turn. First to two round wins wins.

On Linux/macOS, run `make` and launch `./server` plus one `./client localhost 5000` instance.
Run `make test` for the engine and socket integration tests (Python required).

Rules: each player starts a round with 100 HP. Attack vs Attack deals 30 to
both players. Attack vs Defend deals 15 to the defender. Counter reflects
Attack for 30. Defend vs Counter deals 15 to the counter player. Defend vs
Defend and Counter vs Counter cause no damage. There are no powers or stall
penalties. Mutual KO replays the same round. First to two round wins wins.

Linux build:
```bash
gcc -Wall -Wextra -std=c11 server.c game.c net.c -o server
gcc -Wall -Wextra -std=c11 client.c net.c -o client
```
