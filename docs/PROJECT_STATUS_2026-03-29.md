# Zappy Project Status Report (2026-03-29)

## 1) Context and Scope

This report captures the current observed state of the three-part Zappy project:
- C server
- Java AI client
- Godot GUI (observer)

The analysis is based on:
- Direct code inspection of server/client/gui modules
- Build validation on this machine
- Existing project docs and scripts
- No three-way runtime test yet (server + AI + GUI simultaneously)

Primary goal: recover full operational understanding before re-entry on campus.

## 2) Executive Summary

Current maturity by subsystem:
- **Server**: buildable and feature-rich; WebSocket + TLS + JSON protocol in place; observer login path exists; time progression intentionally paused until SIGUSR1.
- **GUI (Godot)**: observer connection and initial game-state world build are implemented and likely consistent with past successful sessions.
- **AI Client (Java)**: compiles, but behavior-level progression logic is incomplete for higher-level coordinated ascension; test suite is stale and currently failing at compile time.

Most probable near-term blockers:
1. **AI coordination deadlock around ascension (roughly level 4+)** due to incomplete decision logic and unimplemented cooperative movement handling.
2. **Observer live-update reliability risk**: observer registration appears to send initial snapshot, but storage/management of observer list is not clearly completed in server game state.
3. **Operational portability gap**: Godot auto-start script path is hardcoded to an old absolute filesystem path.

## 3) Verified Facts (What Was Actually Tested)

### 3.1 Server Build Health

Observed command result:
- `cd server && make -j4` completes successfully.

Implication:
- Server is currently buildable in its present branch state.

### 3.2 Java Client Build/Test Health

Observed command results:
- `cd client && mvn -q compile` succeeds.
- `cd client && mvn test` fails during test compilation.

Test failure cause:
- `PlayerTest` calls a non-existent method `player.broadcast(...)`.
- Current implementation exposes `broadcastCmd(...)` on `Player`.

Implication:
- Main client code compiles, but test suite is stale and cannot currently be used as confidence guardrail.

### 3.3 Repository State After Audit

Observed state:
- Temporary build outputs were cleaned (`client/target` removed).
- Workspace returned clean from this audit pass.

## 4) System-by-System Technical Status

## 4.1 C Server

### 4.1.1 Networking and Protocol Layer

Findings:
- Server uses SSL/TLS + WebSocket abstraction (`ssl_al`) with macros remapping socket calls (`send`, `recv`, `accept`, `close`) when SSL mode is enabled.
- Server performs WebSocket handshake and frame handling in `ssl_al` implementation.
- JSON-based protocol handling in `server.c` supports message `type` values including `login` and `cmd`.

Status judgment:
- **Good functional foundation** for WebSocket clients (Godot and Java).

### 4.1.2 Login Roles and Observer Path

Findings:
- Login role handling includes `player`, `admin`, and `observer`.
- Observer login calls `game_register_observer(fd)`.
- On successful observer registration, server sends an `ok` response and map snapshot pipeline is present.

Risk note:
- Observer notification path depends on observer list (`game_get_observers()`), but current code review suggests observer list persistence may be incomplete.

Status judgment:
- **Partially verified**: initial observer snapshot appears intentional and likely working; sustained observer event fan-out remains a key uncertainty.

### 4.1.3 Time API and SIGUSR1 Handshake

Findings:
- Time API initializes in paused state (`paused_ms = start_time_ms`).
- `SIGUSR1` toggles play/pause (`time_api_run` / `time_api_pause`).
- This aligns with your recollection of a GUI-triggered handshake before game progression.

Operational meaning:
- If SIGUSR1 is never sent, game can remain effectively static even with connected clients.

Status judgment:
- **Intentional design, working by code inspection**.

### 4.1.4 Argument Parsing and Startup Reliability

Finding:
- `parse_args` currently has an early `return SUCCESS;` before real option parsing logic.

Risk:
- Runtime launch behavior may rely more on debug defaults and partial paths than expected.
- Could mask startup/config mistakes during integration tests.

Status judgment:
- **Potentially fragile** operational interface despite successful build.

## 4.2 Godot GUI (Observer)

### 4.2.1 Connection and Auth Flow

Findings:
- GUI uses native `WebSocketPeer` with `wss://` and unsafe client TLS option (compatible with self-signed cert setup).
- Sends observer login JSON (`type=login`, `role=observer`, server key).
- Treats either full game-state payload or success message as authentication success.

Status judgment:
- **Solid for initial connect + bootstrap**.

### 4.2.2 World Bootstrap from Server Data

Findings:
- Data manager parses map/tiles/players/game structures and emits update signals.
- Main scene integration receives `game_state` and updates local game data.

Status judgment:
- **Likely working for initial session load** (consistent with your past results).

### 4.2.3 Portability Gap: Auto-start Script Path

Finding:
- Godot auto-start function uses an absolute path hardcoded to a teammate-local directory.

Impact:
- Auto-start handshake will fail outside that exact machine/path.
- Manual server-side `run.sh` invocation still remains possible.

Status judgment:
- **Known portability defect; low complexity to fix later**.

## 4.3 Java AI Client

### 4.3.1 Transport and Session Flow

Findings:
- Uses Java WebSocket client stack (Tyrus) over ws/wss.
- Handles welcome/login/response/event/error message types.
- Command queue + pending-response throttling exist.

Status judgment:
- **Operational baseline present**.

### 4.3.2 Decision Logic Quality

Findings indicating ascension deadlock risk:
- `goToElevationCall(...)` is a stub returning empty command list.
- In view-based decision flow, ready-to-elevate branch currently falls back to inventory checking instead of reliably issuing coordinated elevation sequence.
- Broadcast coordination exists conceptually but action path is incomplete.

Interpretation:
- The observed level-4-ish stalls are consistent with unfinished multi-agent coordination and state transitions for incantation.

Status judgment:
- **Main known functional weakness of the whole project at this point**.

### 4.3.3 Test Coverage and Trustworthiness

Finding:
- Unit tests do not compile due to API drift.

Impact:
- Team currently lacks automated regression confidence, especially around fragile AI behavior.

Status judgment:
- **Tooling debt that should be addressed early in re-entry**.

## 5) Integration Status (What Is Known vs Unknown)

## 5.1 Known Working

- Server can build and start.
- Godot can connect in observer mode and obtain initial state (historically confirmed; code supports it).
- Java AI client compiles and can connect through current protocol stack.

## 5.2 Unknown / Unverified

- End-to-end live update propagation from active AI commands -> server state changes -> observer push -> Godot visual updates.
- Whether observer registration is fully retained for ongoing notification broadcast.
- Exact AI deadlock mechanics under real multi-client timing at level 4+.

## 6) Risk Register (Prioritized)

1. **AI ascension coordination incompleteness** (High)
- Likely root cause of progress stalls and perceived deadlocks.

2. **Observer continuous notification reliability** (High)
- Could make GUI appear static after initial build, even if server evolves state.

3. **Stale tests** (Medium)
- Slows debugging and safe iteration.

4. **Hardcoded Godot script path** (Medium)
- Causes avoidable friction on different machines.

5. **Server CLI parse path fragility** (Medium)
- Can make launch behavior confusing during integration.

## 7) Root-Cause Hypothesis for Level-4 Deadlock

Most probable composite cause:
- AI reaches stage requiring coordinated behavior.
- Broadcast invite handling cannot materialize into movement/assembly because `goToElevationCall` is not implemented.
- Decision branch loops inventory checks and/or non-progressing behavior rather than completing synchronized incantation workflow.

Secondary amplifiers:
- Lack of regression tests for late-level strategy.
- Real-time synchronization sensitivity once multiple clients run concurrently.

## 8) Immediate Campus Readiness Checklist

Before first three-way run:
1. Confirm server cert files and launch command on campus machine.
2. Verify manual SIGUSR1 flow from terminal (`server/run.sh`) works against running server process.
3. In Godot, disable/ignore hardcoded auto-start path unless fixed.
4. Launch one AI client only first; observe whether GUI receives any live changes.
5. Scale to multiple clients only after one-client path confirms update flow.

## 9) Recommended Validation Sequence (No Code Changes Yet)

1. **Smoke A (Server + GUI)**
- Objective: verify observer connect and initial snapshot.

2. **Smoke B (Server + 1 AI + GUI)**
- Objective: verify command-induced state changes appear in GUI.

3. **Smoke C (Server + 3-6 AI + GUI)**
- Objective: reproduce ascension deadlock symptoms and collect logs.

4. **Stress D (Optional)**
- Objective: evaluate campus hardware margin.
- Keep map/team counts conservative first.

## 10) Key Conclusion

You are not restarting from zero.
- The system has a real, mostly connected foundation.
- Your GUI milestone (observer bootstrap from server session) is likely intact.
- The main re-entry challenge is AI late-game coordination and proving continuous server->observer updates under true three-way runtime.

This status should be treated as the baseline reference document for tomorrow’s implementation session.
