# Tomorrow Command Cheat Sheet (Campus)

Quick copy-paste runbook for first re-entry session.

## 0) Open Terminals

Use 4 terminals:
1. Server build/run
2. Signal control and logs
3. Java client
4. Godot launch notes

## 1) Server (Build + Run)

From repo root:
```bash
cd server
make -j4
```

Run server (recommended for current branch behavior):
```bash
./zappy 8674
```

Important note:
- In this branch, argument parsing appears partially bypassed in code.
- The command above is the safest immediate launch path for tomorrow.

Nominal full usage (documented by server headers, may not be fully honored right now):
```bash
./zappy -p 8674 -x 10 -y 10 -n team1 team2 -c 20 -t 100
```

## 2) Start Game Time (SIGUSR1 Handshake)

In another terminal:
```bash
cd server
./run.sh
```

What it does:
- Sends SIGUSR1 to running zappy process(es)
- Toggles pause/resume of server time API

Tip:
- If clients connect but game looks static, run `./run.sh` once.

## 3) Java Client (Single and Multi)

Build once:
```bash
cd client
mvn -q compile
```

Run 1 client (secure/wss default):
```bash
./client -n team1 -p 8674 -h 127.0.0.1 -c 1
```

Run 3 clients:
```bash
./client -n team1 -p 8674 -h 127.0.0.1 -c 3
```

Run insecure ws (only if needed for debugging):
```bash
./client -n team1 -p 8674 -h 127.0.0.1 -c 1 --insecure
```

## 4) Godot GUI

1. Open Godot project: `zappy_godot/project.godot`
2. In Main scene inspector:
- `use_mock_server = false`
- `server_ip = 127.0.0.1`
- `server_port = 8674`
- `auto_start = false` (recommended for now)
3. Run scene (F5)

Why `auto_start = false` tomorrow:
- Current script points to a machine-specific absolute path for `server/run.sh`.

## 5) Test Ladder (Do in Order)

## Test A: Server + GUI

Goal:
- Confirm observer login + initial map bootstrap.

## Test B: Server + 1 AI + GUI

Goal:
- Confirm live updates (AI actions reflected in GUI after initial snapshot).

## Test C: Server + 3 AI + GUI

Goal:
- Check stable multiplayer flow before late ascension.

## Test D: Server + 6 AI + GUI

Goal:
- Reproduce deadlock around level progression if present.

## 6) Fast Log Capture Commands

Server log tail (if using log file):
```bash
tail -f /tmp/log.txt
```

If log path differs, check `server/config` (`LOG_FILE_PATH`).

Save one run transcript quickly:
```bash
mkdir -p ../session_logs
script ../session_logs/run_$(date +%F_%H-%M).log
```
(Then run your test; exit with `Ctrl+D`)

## 7) If Something Fails

1. Confirm server process is alive:
```bash
pgrep -a zappy
```

2. Re-send SIGUSR1 toggle:
```bash
cd server && ./run.sh
```

3. Re-run one-client scenario before scaling up.

4. Keep map/client counts small until behavior is deterministic.

## 8) Minimum Success Criteria for Tomorrow

1. GUI connects and builds initial world.
2. One AI client runs and produces observable state changes.
3. You can state clearly whether GUI receives ongoing updates.
4. You can reproduce or rule out the level-4 deadlock pattern.
