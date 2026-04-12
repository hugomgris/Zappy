# Zappy AI Client — New Codebase Structure

## Overview

The network layer files are kept **unchanged**. All game logic is rewritten from scratch in a new directory structure that enforces clean separation between layers.

```
src/
├── net/                        ← KEEP UNCHANGED from current codebase
│   ├── TcpSocket.cpp / .hpp
│   ├── SecureSocket.cpp / .hpp
│   ├── TlsContext.cpp / .hpp
│   ├── WebsocketClient.cpp / .hpp
│   └── FrameCodec.cpp / .hpp
│
├── protocol/                   ← NEW — replaces ProtocolTypes + CommandSender
│   ├── Message.hpp
│   ├── Parser.cpp / .hpp
│   └── Sender.cpp / .hpp
│
├── agent/                      ← NEW — replaces AI + WorldState + NavigationPlanner + Client
│   ├── State.hpp
│   ├── Navigator.cpp / .hpp
│   ├── Behavior.cpp / .hpp
│   └── Agent.cpp / .hpp
│
└── main.cpp                    ← NEW — replaces old main.cpp + Client.cpp
```

---

## Layer Responsibilities

The critical rule is: **each layer only talks to the layer directly below it.**

```
main.cpp
    │
    └── Agent          (owns the main loop, connects layers)
            │
            ├── Behavior       (the AI state machine — reads State, calls Sender)
            │       │
            │       └── Navigator  (pure coordinate math — no state, no network)
            │
            ├── State          (pure data — never sends commands)
            │
            └── Sender         (wraps WebsocketClient — no game logic)
                    │
                    └── Parser (JSON ↔ struct — no state, no sending)
```

---

## File-by-File Specification

---

### `net/` — No changes

All files kept exactly as they are. Do not touch.

---

### `protocol/Message.hpp`

Pure data structs only. No parsing logic, no JSON, no methods beyond simple helpers.

```cpp
// Convention: orientation is ALWAYS 0-indexed
//   N=0, E=1, S=2, W=3
// This matches the server's internal enum exactly.
// Document this at the top of every file that touches orientation.

enum class Orientation : int { N = 0, E = 1, S = 2, W = 3 };

struct VisionTile {
    int distance;   // 0 = player's own tile
    int localX;     // negative = left, 0 = center, positive = right
    int localY;     // always == distance (rows forward)
    int playerCount;
    std::vector<std::string> items;

    bool hasItem(const std::string& item) const;
    int  countItem(const std::string& item) const;
};

struct Inventory {
    int nourriture = 0;
    int linemate   = 0;
    int deraumere  = 0;
    int sibur      = 0;
    int mendiane   = 0;
    int phiras     = 0;
    int thystame   = 0;
};

enum class MsgType {
    Unknown, Bienvenue, Welcome, Response, Event, Broadcast, Error
};

struct ServerMessage {
    MsgType type = MsgType::Unknown;
    std::string raw;

    // For Response messages
    std::string cmd;
    std::string arg;
    std::string status;

    // For Welcome
    std::optional<int> mapWidth;
    std::optional<int> mapHeight;
    std::optional<int> remainingSlots;
    std::optional<Orientation> playerOrientation;

    // For voir response
    std::optional<std::vector<VisionTile>> vision;

    // For inventaire response
    std::optional<Inventory> inventory;

    // For broadcast (message)
    std::optional<std::string> messageText;
    std::optional<int> broadcastDirection;  // 0 = same tile, 1–8 = octants

    // Helpers
    bool isOk() const { return status == "ok"; }
    bool isKo() const { return status == "ko"; }
    bool isInProgress() const { return status == "in_progress"; }
    bool isDeath() const;
    bool isLevelUp() const;
};
```

---

### `protocol/Parser.cpp / .hpp`

Stateless. Takes a raw JSON string, returns a `ServerMessage`. No side effects.

**Responsibilities:**
- JSON parsing via cJSON
- Vision tile array → `std::vector<VisionTile>` with correct `localX`, `localY`
- Orientation field: read raw 0-based int from server, store as `Orientation` enum (no conversion needed since both use 0-based)
- Broadcast direction: read `status` field as integer for message-type messages
- Inventory JSON object → `Inventory` struct

**What it must NOT do:**
- Hold any state between calls
- Know anything about the AI's current situation
- Send anything over the network

---

### `protocol/Sender.cpp / .hpp`

Wraps `WebsocketClient`. Knows how to build and send each command JSON. Manages the pending-response queue.

**Responsibilities:**
- Build and send every command (`voir`, `avance`, `prend <resource>`, etc.)
- `expect(cmd, callback)` — register a callback for when a specific response arrives
- `processResponse(msg)` — match incoming response to registered callback, fire it
- `checkTimeouts(ms)` — fire error callbacks for stale pending commands
- `cancelAll()` — clear pending queue on shutdown

**Special handling for incantation:**
- `in_progress` → fire callback but **keep** the pending entry
- `ok` or `ko` → fire callback and remove pending entry

**What it must NOT do:**
- Make decisions about what command to send next
- Read or write any game state
- Know anything about orientation, vision, or resources

---

### `agent/State.hpp`

Pure data. No methods that send commands. Updated only by `Agent` after receiving parsed messages.

```cpp
struct PlayerState {
    int x = 0;
    int y = 0;
    Orientation orientation = Orientation::N;
    int level = 1;
    Inventory inventory;
    int remainingSlots = 0;
};

struct WorldState {
    PlayerState player;
    std::vector<VisionTile> vision;    // last known vision
    int mapWidth  = 0;
    int mapHeight = 0;

    // Helpers — read-only queries
    bool visionHasItem(const std::string& item) const;
    std::optional<VisionTile> nearestTileWithItem(const std::string& item) const;
    int playersOnCurrentTile() const;
    int countItemOnCurrentTile(const std::string& item) const;
};
```

**What it must NOT do:**
- Request vision or inventory updates
- Know about the AI's goals or state machine
- Contain any network code

---

### `agent/Navigator.cpp / .hpp`

Pure functions. Takes a `WorldState` (or just orientation + target coordinates), returns a list of commands to execute. No state, no side effects.

**Core function:**

```cpp
enum class NavCmd { Forward, TurnLeft, TurnRight };

// Returns sequence of NavCmds to reach tile at (localX, localY)
// relative to player's current facing.
std::vector<NavCmd> planPath(Orientation facing, int localX, int localY);

// Converts vision-relative coordinates to world-axis delta.
// N=0: worldDX =  localX, worldDY = -localY
// E=1: worldDX =  localY, worldDY =  localX
// S=2: worldDX = -localX, worldDY =  localY
// W=3: worldDX = -localY, worldDY = -localX
std::pair<int,int> localToWorldDelta(Orientation facing, int localX, int localY);

// Returns turn commands needed to face targetOrientation from currentOrientation.
std::vector<NavCmd> turnToFace(Orientation current, Orientation target);

// Returns a simple exploration step (turn + move) to find new tiles.
std::vector<NavCmd> explorationStep(int stepCount);
```

**What it must NOT do:**
- Hold any state between calls
- Send commands directly
- Know about the AI's goals

---

### `agent/Behavior.cpp / .hpp`

The AI state machine. Reads `WorldState`, issues commands via `Sender`, manages goals.

**States:**

```
Idle
  → CollectFood        (food < threshold)
  → CollectStones      (have all food needed, missing stones for level)
  → Leading            (have all stones, need to rally peers)
  → Forking            (conditions met for fork)

CollectFood
  → Idle               (food restored)

CollectStones
  → Idle               (all stones collected)
  → CollectFood        (food dropped critical)

Leading
  → Rallying           (enough peers on tile OR level 1 solo incantation)
  → Idle               (timeout)

MovingToRally
  → Rallying           (broadcast direction == 0)
  → Idle               (timeout)

Rallying
  → Incantating        (stones placed, enough players, leader sends incantation)
  → Idle               (timeout)

Incantating
  → Idle               (ok or ko received)

Forking
  → Idle               (fork command sent)
```

**Rules:**
- Only one command in flight at a time. Next command is sent only inside the callback of the previous one.
- Never check `allStonesOnTile()` from a cached vision. Always request fresh `voir` first.
- Food emergency overrides all states except `Incantating`.

**What it must NOT do:**
- Parse JSON
- Manage sockets
- Know about localX/Y coordinate math (delegate to Navigator)

---

### `agent/Agent.cpp / .hpp`

Owns the main loop. Connects all layers together.

**Responsibilities:**
- Connect to server via `WebsocketClient`
- Handle login sequence (bienvenue → send login → wait for welcome)
- Call `_ws.tick()` every loop iteration
- Call `_sender.checkTimeouts()` every loop iteration
- Call `_behavior.tick(nowMs)` every loop iteration
- Route incoming messages: update `WorldState`, then call `_sender.processResponse()` or `_behavior.onBroadcast()`
- Handle reconnection on disconnect

**What it must NOT do:**
- Contain any game logic
- Make decisions about what command to send
- Parse JSON directly (delegate to Parser)

---

### `main.cpp`

Minimal. Parses command-line arguments, creates an `Agent`, calls `agent.run()`, handles signals.

```cpp
// Usage: ./zappy_ai <host> <port> <team_name> [--debug] [--no-fork]
int main(int argc, char** argv) {
    // parse args
    // setup signal handlers (SIGINT → agent.stop())
    // create Agent
    // agent.connect()
    // agent.run()  ← blocks until done
    return 0;
}
```

---

## Dependency Graph

```
main.cpp
  depends on: Agent

Agent
  depends on: Behavior, WorldState, Sender, Parser, WebsocketClient

Behavior
  depends on: Navigator, WorldState, Sender

Navigator
  depends on: Message.hpp (Orientation enum only)

Sender
  depends on: WebsocketClient, Message.hpp

Parser
  depends on: Message.hpp, cJSON

WorldState
  depends on: Message.hpp

WebsocketClient (net/)
  depends on: SecureSocket, FrameCodec

SecureSocket (net/)
  depends on: TcpSocket, TlsContext
```

No circular dependencies. Each layer can be unit-tested in isolation by mocking the layer below it.
