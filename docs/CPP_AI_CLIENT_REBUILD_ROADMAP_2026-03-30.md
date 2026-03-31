# C++ AI Client Rebuild Roadmap (2026-03-30)

## 1. Decision and Recommendation

Short answer: yes, rebuilding the AI client in C++ is a good move for this repo.

Why this is a strong decision now:
- Full ownership: language, architecture, and build are under your control.
- Faster debugging for you: no Java/Tyrus black-box behavior.
- Cleaner recovery path: current Java client has drift, partial logic, and trust issues.
- Better long-term maintainability: explicit modules, deterministic tests, and reproducible runs.

Main caution:
- The team decision for this project branch is to fully substitute the classic text protocol with JSON/WebSocket.
- So the C++ client roadmap should prioritize JSON/WebSocket first; classic text can remain an optional compatibility adapter.

## 2. Ground Truth from Current Server Code

Based on current server implementation:

### 2.1 Transport and framing
- WebSocket handshake and framing are implemented in server SSL layer.
- Ping/Pong handling is implemented server-side.
- TLS is used by current local flow.
- Messages are JSON payloads sent in text WebSocket frames.

### 2.2 Login flow (current project contract)
- Server sends initial bienvenue JSON on connect.
- Client login message must be JSON:
  - type: login
  - key: SOME_KEY
  - role: player
  - team-name: <team>
- Successful login response is JSON:
  - type: welcome
  - remaining_clients: <int>
  - map_size: { x: <int>, y: <int> }

### 2.3 Command flow (current project contract)
- Client sends command JSON:
  - type: cmd
  - cmd: <commande>
  - arg: <optional string>
- Commands recognized:
  - avance, droite, gauche, voir, inventaire, prend, pose, expulse, broadcast, incantation, fork, connect_nbr
- Server responses are JSON; common form is:
  - type: response
  - cmd: <commande>
  - status and or arg
- Broadcast to receivers uses message-type JSON (not classic text line):
  - type: message
  - arg: <text>
  - status: <K direction>
- Incantation level-up can arrive as event JSON:
  - type: event
  - status: Level up!

### 2.4 Queue and timing constraints
- Server-side per-client event buffer size is 10 (MAX_EVENTS).
- Subject rule still applies conceptually: do not exceed 10 in-flight requests.
- Timing is scheduled in server time units; time may be paused/resumed in this repo during tests.

## 3. Product Targets

Define two target modes from day 1:

1. Project mode (must-pass first):
- JSON over WebSocket/TLS compatible with current server branch.

2. Optional compatibility mode (second):
- Classic newline text protocol adapter for portability if needed later.

This dual-mode design avoids lock-in while keeping the team-approved JSON pipeline as the delivery priority.

## 4. Architecture Blueprint (from scratch)

Create a new project folder, for example:
- cpp_client/

Suggested modules:
- app/
  - main entrypoint, CLI parsing, lifecycle
- net/
  - socket, tls, websocket, reconnect/session wrappers
- protocol/
  - serializer/deserializer interfaces
  - json_protocol (project mode, primary)
  - text_protocol (optional compatibility mode)
- engine/
  - command scheduler (max in-flight 10)
  - response correlator
  - state store (player/map/inventory/team)
- ai/
  - strategy layer
  - behavior tree or finite-state machine
  - elevation coordinator
- runtime/
  - tick loop, timers, watchdogs, logging/tracing
- tests/
  - unit tests, protocol fixtures, integration harness

Hard rule:
- Keep transport, protocol, and AI strategy independent via interfaces.

## 5. Milestones and Deliverables

## M0. Bootstrap and Build Control

Deliverables:
- Makefile-first project with strict warnings.
- Profiles: debug and release.
- Zero-dependency baseline build on Linux dumps.
- Basic logger and error model.

Acceptance:
- make PROFILE=debug and make PROFILE=release succeed from clean clone.

## M1. CLI Compatibility

Deliverables:
- Required flags:
  - -n team
  - -p port
  - -h host (default localhost)
- Optional flags for compatibility mode:
  - --ws, --wss, --insecure, --protocol text|json

Acceptance:
- Usage behavior mirrors subject expectations for required flags.

## M2. Transport Layer (JSON/WebSocket First)

Deliverables:
- TCP + TLS + WebSocket client implementation.
- Frame read/write, ping/pong, close handling.
- Backpressure-safe send queue.

Acceptance:
- Connects and completes JSON login flow in project mode.
- Handles websocket command/response stream without blocking or active wait.

## M3. Protocol Layer (JSON Project Contract)

Deliverables:
- JSON codec for login, cmd, response, message, event.
- Strong validation and unknown-field tolerance.
- Structured internal message model independent of transport.

Acceptance:
- Successful JSON handshake/login in live run.
- Parse and classify observed project response/event/message payloads.

## M3b. Optional Text Protocol Adapter

Deliverables:
- Text serializer/parser for classic BIENVENUE/newline protocol.
- Runtime adapter boundary that does not affect JSON mode.

Acceptance:
- Text adapter passes local compatibility tests without regressing JSON mode.

## M4. Command Pipeline

Deliverables:
- In-flight cap hard-limited to 10.
- Command queue with dedupe policies:
  - dedupe repeated voir
  - dedupe identical broadcast payloads
- Response-driven credit return.

Acceptance:
- Never exceeds 10 pending requests.
- Stable command flow in 2-5 minute run.

## M5. World and Player State Store

Deliverables:
- Player state: level, inventory, life estimate, position/orientation confidence.
- Vision decoding to local world model.
- Team and connection counters.

Acceptance:
- State updates match server responses over sustained run.

## M6. AI FSM v1 (Single Agent Survival)

Deliverables:
- Explicit FSM:
  - BOOT
  - LOGIN
  - SCOUT
  - FEED
  - GATHER
  - ELEVATE_PREP
  - WAIT_ALLIES
  - INCANTING
  - COOLDOWN
- Safety rules:
  - starvation guard
  - cooldowns to avoid spam
  - no command storms

Acceptance:
- Single agent survives and progresses with no deadlock loops.

## M7. Team Coordination v1 (Broadcast)

Deliverables:
- Broadcast parser/handler with directional convergence behavior.
- Elevation call protocol for same-level grouping.
- Timeout and fallback if regroup fails.

Acceptance:
- 3-agent test reaches at least one successful coordinated elevation.

## M8. Reproduction and Scaling

Deliverables:
- fork and connect_nbr policies.
- Spawn policy thresholds based on map and food confidence.

Acceptance:
- Controlled client growth without immediate starvation collapse.

## M9. Optional Compatibility Adapter (Text Protocol)

Deliverables:
- Text serializer/parser for classic protocol.
- Newline transport wiring.
- Runtime switch between text and json protocol modules.

Acceptance:
- Text adapter works in compatibility tests without breaking JSON project mode.

## M10. Hardening and Documentation

Deliverables:
- Full runbook for Server + 1, +3, +6 clients.
- Troubleshooting matrix.
- Performance and stability checklist.

Acceptance:
- Reproducible runs from clean environment following docs only.

## 6. Test Strategy

Unit tests:
- Protocol parsing/serialization fixtures.
- Queue credit logic and dedupe behavior.
- FSM transitions and cooldown behavior.

Integration tests:
- JSON-project-mode smoke runs (1 client, 3 clients, 6 clients).
- Fault injection:
  - delayed responses
  - malformed JSON and malformed lines (for optional adapter)
  - forced disconnects
  - ping/pong bursts

Runtime assertions:
- pending_count <= 10 always.
- no infinite tight loop without outgoing I/O or state change.

## 7. Build and Tooling Choices

Recommended baseline:
- C++20
- Makefile
- Catch2 or doctest for unit tests
- nlohmann/json for JSON
- OpenSSL for TLS

Note:
- If minimizing external dependencies is critical for dump compatibility, keep a pure POSIX + OpenSSL + lightweight JSON parser fallback option.

## 8. Migration Plan from Java Client

Do not port line-by-line.

Do this instead:
1. Extract behavior requirements from logs and tests.
2. Re-encode them as C++ test cases against new interfaces.
3. Reimplement only validated behaviors.
4. Drop legacy assumptions that were coupled to old architecture.

## 9. Execution Plan (Practical 10-Day Slice)

Day 1-2:
- M0, M1, skeleton modules, build/test pipeline.

Day 3-4:
- M2 websocket transport and stable connect/login.

Day 5:
- M3 JSON protocol decode/encode complete.

Day 6:
- M4 command pipeline with in-flight safety.

Day 7:
- M5 state store wired to protocol events.

Day 8:
- M6 single-agent FSM v1.

Day 9:
- M7 coordination and elevation behavior.

Day 10:
- M10 docs, smoke runs, stabilization backlog.

Optional bonus slice:
- M3b/M9 text-compat adapter once JSON mode is stable.

## 10. Risks and Mitigations

Risk: protocol mismatch between subject and current repo server.
- Mitigation: JSON project mode is canonical; text mode remains isolated behind optional adapter boundary.

Risk: transport instability under TLS/WebSocket edge cases.
- Mitigation: explicit ping/pong handling, close code handling, reconnect strategy.

Risk: AI deadlocks in coordination loops.
- Mitigation: hard cooldowns, retry budgets, fallback exploration states.

Risk: over-queue command loss.
- Mitigation: strict in-flight credits and queue caps.

## 11. Definition of Done (Phase 1)

Phase 1 is done when all are true:
- New C++ client logs in and plays autonomously in JSON project mode.
- No pending command overflow beyond 10.
- Single-client behavior is stable for 5+ minutes.
- 3-client run achieves at least one successful cooperative elevation.
- Runbook allows repeatable execution on your environment.

Optional done:
- Text compatibility mode also runs without breaking JSON mode.

## 12. Immediate Next Actions

1. Finalize Makefile profiles and module stubs (M0).
2. Implement JSON transport proof-of-life: connect, receive bienvenue JSON, send login JSON, parse welcome JSON (M2-M3 subset).
3. Add command credit manager before any strategy logic (M4 core).

---

This roadmap is intentionally built to give you full ownership while matching the actual server contract in this repository today.
