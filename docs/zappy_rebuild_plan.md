# Zappy AI Client — Full Rebuild Plan

## 1. What's Safe to Keep

### Server (C codebase) — keep as-is
The server logic is solid: the time API, event buffer, SSL/WebSocket layer, and game rules are all correct. The direction enum (`NORTH=0, EAST=1, SOUTH=2, WEST=3` internally, sent as 0-based integers over the wire) is consistent.

### Client network layer — keep, with caveats

| File | Decision |
|---|---|
| `TcpSocket.cpp` | ✅ Keep |
| `SecureSocket.cpp` | ✅ Keep |
| `TlsContext.cpp` | ✅ Keep |
| `WebsocketClient.cpp` | ✅ Keep |
| `FrameCodec.cpp` | ✅ Keep |
| `CommandSender.cpp` | ✅ Keep — the problem was never here |

### Throw away entirely

| File | Decision |
|---|---|
| `AI.cpp` | ❌ Trash — full state machine rewrite |
| `WorldState.cpp` | ❌ Trash — vision/state tracking was wrong |
| `Client.cpp` | ❌ Trash — game loop logic |
| `ProtocolTypes.cpp` | ❌ Trash — orientation convention was muddled; JSON skeleton can be referenced but don't reuse |
| `NavigationPlanner.cpp` | ❌ Trash — coordinate conversion was broken |

---

## 2. The Core Bugs That Killed You

### Bug A: Vision coordinate system was never correctly understood

The server sends vision tiles in this order for a player at level L:
- Distance 0: 1 tile (your own tile)
- Distance 1: 3 tiles (left, center-forward, right)
- Distance 2: 5 tiles, and so on

The parser computed `localX = tilesProcessed - currentLevel` and `localY = currentLevel` — that part was fine. But then `buildPlanToResource` tried to convert those local coordinates to world deltas using orientation math that was inconsistent across files. The EAST case was broken multiple times across multiple attempted fixes. The AI was navigating in wrong directions when facing East or West — it thought it was going toward a resource but was moving away.

### Bug B: `_commandInFlight` fix was right idea, implemented too late and too weakly

The server's event buffer holds **10 commands per client**. The original AI sent `voir` + `inventaire` every single tick (50ms loop) — 20 commands per second, filling the buffer in half a second. The fix added `_commandInFlight` but the timeout fallback (`COMMAND_FLIGHT_TIMEOUT_MS`) would fire and clear the flag prematurely. The AI then made decisions on stale data with an inconsistent internal state.

### Bug C: Incantation logic had a fundamental design flaw

The `allStonesOnTile()` check compared against a vision snapshot that might be stale by the time incantation was sent. `placeAllStonesOnTile` would return without setting `_stonesPlaced = true` if any stone failed to place, then the loop re-entered and tried to place the same stone again next tick, burning commands pointlessly.

### Bug D: Direction convention was never fully settled

The server internally uses `0=N, 1=E, 2=S, 3=W`. It sends this as-is in the `orientation` field. The client code accumulated **three different conventions** across files: 0-indexed in some places, 1-indexed in others. `orientationFromServer()` was added late as a patch. Any code that ran before that patch used the raw 0-based value in arithmetic designed for 1-based values, silently producing wrong turn directions.

### Bug E: Broadcast-based rallying was too ambitious for a first implementation

Coordinating multiple AI clients via broadcast is genuinely hard. The server's broadcast direction computation uses an 8-directional system (1–8, with 0 meaning "same tile"), and any bug in the client's interpretation means followers walk *away* from leaders. This was attempted before having reliable single-agent survival, making debugging impossible — there was no way to tell if a failure came from navigation, food management, or rally coordination.

---

## 3. The Rebuild Plan — Step by Step

> **Rule:** Do not move to the next step until the current one is tested and confirmed working.

### Step 1 — Message parsing only, no AI

Build a new `ProtocolParser` that takes raw JSON strings and produces clean structs. Test this in **complete isolation** with hardcoded JSON strings before connecting it to anything.

**What must be exactly right:**
- Vision parsing: tile order, `localX`/`localY` computation
- Orientation: store everything as **0-indexed** (matching the server), never convert
- Inventory parsing
- Broadcast direction (0 = same tile, 1–8 = octants)

**Choose one convention and commit.** Use 0-indexed (`N=0, E=1, S=2, W=3`) because that's what the server sends. Document it in a comment at the top of every file that touches orientation.

---

### Step 2 — A command queue with synchronous confirmation

Build a `CommandQueue` that:
- Holds **at most 1 pending command** at a time
- Sends the command
- Waits for the response before allowing the next
- Has a hard timeout (3 seconds) that retries once then gives up

This is more conservative than the server's buffer allows, but it makes the system fully predictable. Once the AI is working, you can pipeline more aggressively.

**Special case:** `incantation` receives `in_progress` first, then `ok`/`ko`. The queue must handle this two-response pattern — do not mark incantation complete on `in_progress`.

---

### Step 3 — Single-agent survival loop

Write an AI that does **exactly one thing: not die**. The loop:

1. Send `voir`, wait for response, parse vision
2. Send `inventaire`, wait for response, parse inventory
3. If food on current tile → send `prend nourriture`
4. If food visible nearby → navigate one step toward it
5. If no food visible → turn right and move forward (exploration)
6. Go to 1

Test with a single client. It should run indefinitely without dying. **Do not add any stone or incantation logic yet.**

---

### Step 4 — Navigation primitive

Add a `navigate(localX, localY)` function that produces a sequence of turn + forward commands to reach a tile at the given local vision coordinates.

**The correct coordinate math (0-indexed orientation, N=0, E=1, S=2, W=3):**

```
// localX: negative = left of player, positive = right
// localY: rows forward (0 = player's own tile)

N(0): worldDX =  localX,  worldDY = -localY
E(1): worldDX =  localY,  worldDY =  localX
S(2): worldDX = -localX,  worldDY =  localY
W(3): worldDX = -localY,  worldDY = -localX
```

Navigate `worldDX` steps East/West, then `worldDY` steps North/South, turning as needed. Test by making the agent reliably walk to a specific visible resource.

---

### Step 5 — Stone collection and level 1→2 incantation

Level 1→2 requires: **1 player, 1 linemate**. This is the simplest possible incantation — no other players needed.

Write an AI that:
1. Survives (step 3 behavior)
2. Collects 1 linemate using the navigator (step 4)
3. Drops it on the current tile with `pose`
4. Sends `incantation`
5. Waits for `ok`

If this works, your parsing, navigation, command queue, and incantation flow are all confirmed correct.

---

### Step 6 — Stone collection for levels 2–4, easy mode testing

Level 2→3 requires 2 players. Before adding multi-agent complexity, test stone-collection logic for every level's requirements against a server with `ZAPPY_EASY_ASCENSION=1`. This bypasses player-count requirements so stone logic can be verified in complete isolation.

**Level requirement table:**

| Level | Players | Stones needed |
|---|---|---|
| 1→2 | 1 | 1 linemate |
| 2→3 | 2 | 1 linemate, 1 deraumere, 1 sibur |
| 3→4 | 2 | 2 linemate, 1 sibur, 2 phiras |
| 4→5 | 4 | 1 linemate, 1 deraumere, 2 sibur, 1 phiras |
| 5→6 | 4 | 1 linemate, 2 deraumere, 1 sibur, 3 mendiane |
| 6→7 | 6 | 1 linemate, 2 deraumere, 3 sibur, 1 phiras |
| 7→8 | 6 | 2 linemate, 2 deraumere, 2 sibur, 2 mendiane, 2 phiras, 1 thystame |

---

### Step 7 — Multi-agent coordination via broadcast

Only add broadcast-based coordination at this step. The design that works is simpler than what you had before:

**One agent becomes leader when it has all required stones.** It broadcasts `RALLY:<level>` periodically. All other agents at the same level move toward the broadcast source. When enough agents are on the same tile, the leader sends incantation.

**Key insight:** Agents don't need to know the leader's absolute position. They just need to keep taking one step in the broadcast direction (updating as new broadcasts arrive) until the direction becomes 0 (same tile).

The broadcast direction from the server gives values 1–8 (clockwise from ahead, 1=straight ahead). The client needs to turn to face that direction and move forward — one step per received broadcast message. Don't try to plan a full path to the leader.

**Protocol:**
- `RALLY:<level>` — leader advertising position
- `HERE:<level>` — follower confirming arrival on leader's tile
- `START:<level>` — leader signaling incantation is beginning
- `DONE:<level>` — leader signaling incantation succeeded or was abandoned

---

### Step 8 — Full level 1→8 with multiple agents

Once step 7 works for level 1→2 with 2 agents, extend to higher levels.

**Fork strategy:** Every agent forks once when it has more than 20 food, but only if fewer than `(level * 2)` total team members exist. This prevents runaway forking that starves the team of food.

**Fork timing note:** Forking is asynchronous. You fork, then later a new agent process connects and is assigned the pre-spawned player at the egg's location. The egg hatches after 600 time units by default.

---

## 4. Pitfalls to Avoid

**Direction conventions.** Pick 0-indexed, document it, never mix. Use an `enum class` to make mixing a compile error.

**Vision staleness.** After every `avance`, `droite`, or `gauche`, your vision is stale. Request fresh vision before any navigation decision that depends on tile contents. After a turn, the coordinates of existing tiles change even if their contents don't.

**Server event buffer is per-client.** If you send 10 commands without waiting for responses, the 11th is dropped silently. The server logs `[TIME][EVT] Buffer full!`. With one command in flight at a time you will never hit this.

**Incantation two-phase response.** The server schedules incantation in two phases: an immediate check (`incantation` command → `in_progress`), then a delayed execution → `ok` or `ko`. Your command queue must handle this two-response pattern — do not mark incantation complete on `in_progress`.

**Fork and egg hatching are asynchronous.** When you fork, the egg hatches after 600 time units. The new player spawns at the egg's location as `to_be_claimed` — waiting for a new TCP connection. Your new agent process needs to connect, log in with the team name, and it will be assigned the pre-spawned player. Do not expect an immediate new agent after forking.

**Food calculation is in time units, not milliseconds.** The client can't know the exact `time_unit` value without server config, but it can track food by counting `prend`/`pose` results and watching for the death event.

**The `voir` tile order.** Tile index 0 is always the player's own tile. The parsing was mostly correct — the bug was in coordinate *conversion*, not parsing. Don't rewrite the parser from scratch if you can help it; just fix the conversion math (Step 4).

**`allStonesOnTile()` must use fresh vision.** Always send `voir` immediately before checking if stones are in position for incantation. Never rely on a cached vision snapshot for this check.
