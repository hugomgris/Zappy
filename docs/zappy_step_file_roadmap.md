# Zappy AI Client — Step-by-File Implementation Roadmap

This document maps each rebuild step to the exact files being created or modified,
and lists the specific things to implement in each file per step.

> **Rule:** Complete all items in a step before touching anything in the next step.
> Each step should produce something testable before moving on.

---

## Step 1 — Message Parsing

**Goal:** A parser that correctly turns raw server JSON into clean structs.
No network, no AI, no state. Test with hardcoded strings.

### Files created this step

#### `protocol/Message.hpp` — create from scratch

- [x] Define `enum class Orientation : int { N=0, E=1, S=2, W=3 }`
- [x] Add comment at top: *"Orientation is always 0-indexed matching the server enum. N=0,E=1,S=2,W=3. Never convert this."*
- [x] Define `struct VisionTile { int distance; int localX; int localY; int playerCount; std::vector<std::string> items; }`
- [x] Add `VisionTile::hasItem(name)` helper
- [x] Add `VisionTile::countItem(name)` helper
- [x] Define `struct Inventory` with all 7 resource fields
- [x] Define `enum class MsgType` with: Unknown, Bienvenue, Welcome, Response, Event, Broadcast, Error
- [x] Define `struct ServerMessage` with all fields (cmd, arg, status, optional vision, optional inventory, optional broadcastDirection, etc.)
- [x] Add `ServerMessage::isOk()`, `isKo()`, `isInProgress()`, `isDeath()`, `isLevelUp()` helpers

#### `protocol/Parser.hpp` — create from scratch

- [x] Declare `ServerMessage parse(const std::string& json)`
- [x] No state, no constructor needed — pure function

#### `protocol/Parser.cpp` — create from scratch

- [x] Implement `parse()` using cJSON
- [x] Parse `type` field → set `MsgType`
- [x] Parse `cmd`, `arg`, `status` fields
- [x] For Welcome: parse `remaining_clients`, `map_size.x`, `map_size.y`, `orientation`
- [x] **Vision tile parsing:** iterate the `vision` JSON array, track `currentLevel` and `tilesProcessed`, compute `localX = tilesProcessed - currentLevel`, `localY = currentLevel`, increment both correctly
- [x] For each vision tile: count `"player"` entries → `playerCount`, push everything else to `items`
- [x] For inventaire: parse `inventaire` JSON object → `Inventory` struct
- [x] For message (broadcast): parse `arg` → `messageText`, parse `status` as int → `broadcastDirection`
- [x] For event: check `status` for `"Level up!"` → set flag; check for `"-"` type (death)
- [x] **Unit test:** write `test_parser.cpp` with at least these hardcoded inputs and verify output:
  - A `voir` response with 1 tile (level 1 player)
  - A `voir` response with 4 tiles (level 1, distance 0 + distance 1)
  - An `inventaire` response
  - A `message` broadcast with direction 3
  - A `welcome` response

---

## Step 2 — Command Queue

**Goal:** A sender that allows exactly one command in flight at a time, with confirmed callbacks.

### Files created this step

#### `protocol/Sender.hpp` — create from scratch

- [x] Declare `Sender(WebsocketClient& ws)`
- [x] Declare all send methods: `sendVoir()`, `sendAvance()`, `sendDroite()`, `sendGauche()`, `sendPrend(resource)`, `sendPose(resource)`, `sendBroadcast(text)`, `sendIncantation()`, `sendFork()`, `sendConnectNbr()`, `sendInventaire()`, `sendLogin(teamName)`
- [x] Declare `expect(cmd, callback)` — registers callback for next response matching `cmd`
- [x] Declare `processResponse(msg)` — match and fire pending callback
- [x] Declare `checkTimeouts(int timeoutMs)` — fire error callbacks for stale entries
- [x] Declare `cancelAll()`

#### `protocol/Sender.cpp` — create from scratch

- [x] Implement all send methods as cJSON object builders → JSON string → `_ws.sendText()`
- [x] `expect()`: push `{cmd, sentAt, callback}` to `_pending` deque
- [x] `processResponse()`:
  - For `incantation` + `in_progress`: fire callback but **do not remove** from pending
  - For all other statuses: find by `cmd`, fire callback, remove from pending
  - For `prend`/`pose`: match key as `"prend " + arg` / `"pose " + arg` so different resources don't collide
- [x] `checkTimeouts()`: iterate pending, fire error callback (status="timeout") for entries older than `timeoutMs`, remove them
- [x] **Test:** in isolation, verify that:
  - Sending `sendVoir()` then calling `processResponse()` with a matching voir response fires the callback
  - Incantation `in_progress` does not remove the pending entry
  - A timed-out entry fires an error callback

---

## Step 3 — Single-Agent Survival

**Goal:** One client connects, logs in, and survives indefinitely by eating food.

### Files created this step

#### `agent/State.hpp` — create from scratch

- [x] Define `struct PlayerState { int x; int y; Orientation orientation; int level; Inventory inventory; int remainingSlots; }`
- [x] Define `struct WorldState { PlayerState player; std::vector<VisionTile> vision; int mapWidth; int mapHeight; }`
- [x] Add `WorldState::nearestTileWithItem(name)` — returns first tile in `vision` that has the item (distance 0 first)
- [x] Add `WorldState::playersOnCurrentTile()` — returns `vision[0].playerCount` if vision is non-empty
- [x] Add `WorldState::countItemOnCurrentTile(name)` — counts item in `vision[0]`
- [x] Add read-only food helper: `int PlayerState::food() const { return inventory.nourriture; }`

#### `agent/Behavior.hpp` — create from scratch (survival only)

- [x] Define `enum class AIState { Idle, CollectFood }`
- [x] Declare `void tick(int64_t nowMs)` — the main entry point called every loop
- [x] Declare `bool commandInFlight` flag
- [x] Declare `void onResponse(const ServerMessage& msg)` — for any response the behavior needs to react to

#### `agent/Behavior.cpp` — create from scratch (survival only)

- [x] Implement `tick()`:
  - If command in flight → return immediately
  - If vision is stale (never requested, or flagged stale after move) → `sendVoir()`, set in-flight, on callback clear in-flight and mark vision fresh
  - If inventory is stale → `sendInventaire()`, same pattern
  - If food on current tile (`vision[0]` has `"nourriture"`) → `sendPrend("nourriture")`
  - Else if nearest food tile found → build one nav step toward it (just one turn or one forward)
  - Else → turn right (exploration)
- [x] Each send call goes through `Sender`, each registers a callback that clears `commandInFlight`
- [x] After `avance`/`droite`/`gauche` response → mark vision stale
- [x] After `prend nourriture` response → update `State.player.inventory.nourriture++`

#### `agent/Agent.hpp` + `agent/Agent.cpp` — create from scratch (minimal)

- [x] Constructor takes host, port, teamName
- [x] `connect()`: connect websocket, send login, wait for welcome, update `State` from welcome message
- [x] `run()`: start network loop thread
- [x] Network loop:
  - `_ws.tick(nowMs)`
  - drain incoming frames → `Parser::parse()` → route to `State` update + `Sender::processResponse()` or `Behavior::onBroadcast()`
  - `_sender.checkTimeouts(3000)`
  - `_behavior.tick(nowMs)`
  - sleep 50ms
- [x] `stop()`: set running flag to false, join thread

#### `main.cpp` — create from scratch

- [x] Parse `<host> <port> <team_name>` from args
- [x] Optional `--debug` flag
- [x] Install signal handlers for SIGINT/SIGTERM → `agent.stop()`
- [x] Create Agent, call `connect()`, call `run()`, loop until stopped

**Test:** Run one client against the server. It should stay alive for at least 5 minutes without dying. Watch logs for `[TIME][EVT] Buffer full!` — if you see it, the command-in-flight guard is broken.

---

## Step 4 — Navigation Primitive

**Goal:** The agent can reliably walk to any visible tile.

### Files created this step

#### `agent/Navigator.hpp` — create from scratch

- [x] Declare `enum class NavCmd { Forward, TurnLeft, TurnRight }`
- [x] Declare `std::pair<int,int> localToWorldDelta(Orientation facing, int localX, int localY)`
- [x] Declare `std::vector<NavCmd> turnToFace(Orientation current, Orientation target)`
- [x] Declare `std::vector<NavCmd> planPath(Orientation facing, int localX, int localY)`
- [x] Declare `std::vector<NavCmd> explorationStep(int& stepCount)` — stepCount is incremented internally for variation

#### `agent/Navigator.cpp` — create from scratch

- [x] Implement `localToWorldDelta()` with the **exact** formulas:
  ```
  N(0): worldDX =  localX,  worldDY = -localY
  E(1): worldDX =  localY,  worldDY =  localX
  S(2): worldDX = -localX,  worldDY =  localY
  W(3): worldDX = -localY,  worldDY = -localX
  ```
- [x] Implement `turnToFace()`: compute `diff = (target - current + 4) % 4`, emit TurnRight if diff==1, TurnLeft if diff==3, two TurnRights if diff==2
- [x] Implement `planPath()`: call `localToWorldDelta`, then emit turns+forwards for X axis first, then Y axis
- [x] Implement `explorationStep()`: every 7th step turn right, every 13th turn left, always add one Forward
- [x] **Unit test** `localToWorldDelta` for all 4 orientations × several (localX, localY) values before connecting to anything

### Files modified this step

#### `agent/Behavior.cpp`

- [x] Replace the one-step-toward-food hack with `Navigator::planPath()` call
- [x] Store the resulting `std::deque<NavCmd>` as `_navPlan`
- [x] Each tick: if `_navPlan` is non-empty and no command in flight → execute front of plan
- [x] Clear `_navPlan` when vision becomes stale (after any move or turn)
- [x] Clear `_navPlan` when the target item is no longer visible after a fresh `voir`

**Test:** Place a resource on a specific tile and verify the agent reaches it from various starting orientations. Test all 4 facing directions.

---

## Interlude 1 - Documentation of the basic behaving client build
- [x] WRITE

## Step 5 — Stone Collection and Level 1→2 Incantation

**Goal:** One client can reach level 2 completely autonomously.

### Files modified this step

#### `agent/Behavior.hpp`

- [x] Add `enum class AIState` values: `CollectStones`, `Incantating`
- [x] Add `std::vector<std::string> _stonesNeeded`
- [x] Add `bool _incantationSent` flag
- [x] Add `bool _stonesPlaced` flag

#### `agent/Behavior.cpp`

- [x] Implement `computeMissingStones()`: compare `levelReq(level).stones` against current inventory + items already on current tile from fresh vision
- [x] Add `CollectStones` state:
  - Compute missing stones
  - If empty → transition to `Incantating`
  - Find nearest tile with needed stone → navigate to it → `prend`
  - If food drops low → transition to `CollectFood`, return to `CollectStones` after
- [x] Add stone placement before incantation: for each required stone, if it's in inventory and not yet on the tile, `pose` it (one per tick, check callback before next pose)
- [x] Add `Incantating` state:
  - Send fresh `voir`
  - Verify required stones are on current tile from fresh vision
  - Send `incantation`
  - On `in_progress`: wait
  - On `ok`: increment level in State, clear stones tracking, transition to `Idle`
  - On `ko`: transition to `Idle`, restart from stone collection

#### Add level requirement table

Define this as a static function or constant in `Behavior.cpp`:

```
level 1→2: { players=1, linemate=1 }
level 2→3: { players=2, linemate=1, deraumere=1, sibur=1 }
level 3→4: { players=2, linemate=2, sibur=1, phiras=2 }
level 4→5: { players=4, linemate=1, deraumere=1, sibur=2, phiras=1 }
level 5→6: { players=4, linemate=1, deraumere=2, sibur=1, mendiane=3 }
level 6→7: { players=6, linemate=1, deraumere=2, sibur=3, phiras=1 }
level 7→8: { players=6, linemate=2, deraumere=2, sibur=2, mendiane=2, phiras=2, thystame=1 }
```

**Test:** Run with `ZAPPY_EASY_ASCENSION=1`. One client should reach level 8 automatically (easy mode bypasses player-count requirement). If it reaches 8, your stone logic and incantation flow are confirmed correct.

---

## Interlude 1 - Documentation of the basic behaving client build
- [x] WRITE

DONE UNTIL HERE !!

## Step 6 — Multi-Level Stone Logic (Normal Mode, Single Agent)

**Goal:** Stone collection works correctly for all levels in non-easy mode. Confirm level 1→2 still works (only 1 player needed).

### Files modified this step

#### `agent/Behavior.cpp`

- [ ] Confirm `computeMissingStones()` correctly handles all 7 levels from the requirement table
- [ ] Add priority ordering to stone collection: collect the rarest stone first (thystame → phiras → mendiane → sibur → deraumere → linemate → nourriture)
- [ ] Add opportunistic food grab: if food is visible on a tile the agent passes through while collecting stones, pick it up
- [ ] Add fork decision to `Idle` state:
  - Only if food > 20
  - Only if level >= 2 (no point forking at level 1 before you know the AI works)
  - Only if `_forkEnabled` flag is true (from command-line arg)
  - Fork is one `sendFork()` call — then immediately return to normal behavior

**Test:** With `ZAPPY_EASY_ASCENSION=0`, a single client should complete level 1→2 (requires 1 player). For levels 2+, it will get stuck waiting for players — that's expected and correct. The test here is that it correctly identifies it has all stones and knows it needs more players before attempting incantation.

---

## Step 7 — Broadcast Coordination and Multi-Agent Rallying

**Goal:** Multiple clients coordinate via broadcast to reach levels requiring 2+ players.

### Files created this step

#### `agent/Behavior.hpp`

- [ ] Add `enum class AIState` values: `Leading`, `MovingToRally`, `Rallying`
- [ ] Add `bool _isLeader` flag
- [ ] Add `int _rallyLevel`
- [ ] Add `int _broadcastDirection` (last received rally direction)
- [ ] Add `int64_t _lastRallyBroadcastMs`
- [ ] Declare `onBroadcast(const ServerMessage& msg)` — called by Agent when a message-type message arrives

### Files modified this step

#### `agent/Behavior.cpp`

- [ ] Implement `Leading` state:
  - Broadcast `"RALLY:<level>"` every 500ms
  - Wait for peer count on current tile (from fresh `voir`) to reach required player count
  - On enough players → transition to `Rallying`
  - On timeout (30s) → broadcast `"DONE:<level>"`, transition to `Idle`

- [ ] Implement `MovingToRally` state:
  - If `_broadcastDirection == 0` → already on leader's tile, transition to `Rallying`
  - Otherwise: use `Navigator::planApproachDirection(_broadcastDirection)` to get one nav step
  - Execute one step, then wait for fresh `voir`, update `_broadcastDirection` from new rally broadcasts
  - On timeout (30s) → transition to `Idle`

- [ ] Implement `Rallying` state:
  - Place stones (if leader or if instructed)
  - If leader + enough players + all stones on tile → send `incantation`
  - If follower + direction == 0 → broadcast `"HERE:<level>"`
  - On timeout → transition to `Idle`

- [ ] Implement `onBroadcast()`:
  - Parse message prefix (`RALLY:`, `HERE:`, `START:`, `DONE:`)
  - `RALLY:<level>`:
    - If not leader and level matches current level → set `_broadcastDirection`, transition to `MovingToRally` (or `Rallying` if direction == 0)
    - If leader and direction != 0 → relinquish leadership, follow
  - `HERE:<level>`: if leader, increment peer-confirmed counter
  - `DONE:<level>`: if in rally states, disband and go `Idle`

- [ ] In `Idle` state: after collecting all stones, transition to `Leading` instead of directly to incantation (for levels requiring > 1 player)

#### `agent/Navigator.hpp` / `Navigator.cpp`

- [ ] Add `std::vector<NavCmd> planApproachDirection(int broadcastDirection, Orientation currentFacing)`:
  - Maps broadcast direction (1–8) to an approach offset (0=forward, 1=right, 2=behind, 3=left)
  - Returns turn commands + one Forward
  ```
  direction 1      → offset 0 (forward)
  direction 2, 8   → offset 0 (forward-ish)
  direction 3, 4   → offset 1 (right)
  direction 5      → offset 2 (behind)
  direction 6, 7   → offset 3 (left)
  ```

**Test:** Run 2 clients against a server with a 2-player level (level 2→3 requires 2). One should become leader, broadcast, the other should navigate to the same tile, and they should complete the incantation together. Watch for `Level up!` event on both clients.

---

## Step 8 — Full Game, Forking, and Winning Condition

**Goal:** A team of clients levels up all the way to 8, with forking to maintain team size.

### Files modified this step

#### `agent/Behavior.cpp`

- [ ] Implement forking decision properly:
  - Fork allowed only when: food > 20, level >= 2, `nowMs - _lastForkMs > FORK_INTERVAL_MS` (suggest 60 seconds)
  - After forking, set `_lastForkMs = nowMs`
  - Do not fork while in any rally/incantation state
- [ ] Add level-8 win condition detection: if `State.player.level >= 8`, transition to a `Won` state and stop issuing commands (just send a broadcast `"WIN"` for fun)
- [ ] Tune food thresholds:
  - `FOOD_CRITICAL` = 3 (override everything, go find food)
  - `FOOD_LOW` = 8 (avoid starting new multi-step actions)
  - `FOOD_SAFE` = 15 (comfortable to start stone collection)
  - `FOOD_FORK_GATE` = 20 (safe to fork)

#### `main.cpp`

- [ ] Support launching multiple clients from one process if desired (optional — multiple processes also works)
- [ ] Add `--no-fork` flag to disable forking (useful for testing coordination without population growth)

**Final test checklist:**
- [ ] Single client survives for 10 minutes without dying
- [ ] Single client reaches level 2 in easy mode
- [ ] Single client reaches level 8 in easy mode
- [ ] Two clients coordinate to reach level 3 together
- [ ] Full team reaches level 8 in normal mode

---

## Quick Reference: Which Files Change Per Step

| File | Step 1 | Step 2 | Step 3 | Step 4 | Step 5 | Step 6 | Step 7 | Step 8 |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| `protocol/Message.hpp` | **create** | | | | | | | |
| `protocol/Parser.hpp` | **create** | | | | | | | |
| `protocol/Parser.cpp` | **create** | | | | | | | |
| `protocol/Sender.hpp` | | **create** | | | | | | |
| `protocol/Sender.cpp` | | **create** | | | | | | |
| `agent/State.hpp` | | | **create** | | | | | |
| `agent/Behavior.hpp` | | | **create** | | modify | modify | modify | |
| `agent/Behavior.cpp` | | | **create** | modify | modify | modify | modify | modify |
| `agent/Navigator.hpp` | | | | **create** | | | modify | |
| `agent/Navigator.cpp` | | | | **create** | | | modify | |
| `agent/Agent.hpp` | | | **create** | | | | | |
| `agent/Agent.cpp` | | | **create** | | | | | modify |
| `main.cpp` | | | **create** | | | | | modify |
| `net/*` | unchanged | unchanged | unchanged | unchanged | unchanged | unchanged | unchanged | unchanged |

---

## Testing Strategy Per Step

| Step | How to test |
|---|---|
| 1 | `test_parser.cpp` with hardcoded JSON strings, no server needed |
| 2 | `test_sender.cpp` with a mock WebsocketClient, no server needed |
| 3 | One real client on real server, watch it not die for 5+ minutes |
| 4 | One client, place a resource nearby, verify it walks to it from all 4 orientations |
| 5 | `ZAPPY_EASY_ASCENSION=1`, one client should reach level 8 |
| 6 | `ZAPPY_EASY_ASCENSION=0`, one client reaches level 2, correctly waits for more players for level 3 |
| 7 | Two clients in a 2-player server, watch them cooperate to level 3 |
| 8 | Full team, full game, reach the winning condition |
