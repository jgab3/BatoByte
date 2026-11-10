# BatoByte Basic

This folder contains the simplified two-computer version of BatoByte. It has Attack, Defend, Counter, 100 HP, round scoring, and first-to-two-round wins. There are no powers, healing, poison, or other twists yet.

The host computer runs `server` and is Player 1. The second computer runs `client` and is Player 2.

## Build

```bash
make
```

## Play

On the host computer:

```bash
./server
```

On the second computer:

```bash
./client SERVER_IP
```

For one-computer testing, replace `SERVER_IP` with `127.0.0.1`.

Attack vs Attack damages both players for 30. Attack vs Defend damages the defender for 15. Counter reflects an attack for 30. Defend vs Counter damages the Counter player for 15. Defend vs Defend and Counter vs Counter cause no damage.
