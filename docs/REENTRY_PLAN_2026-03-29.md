# Zappy Re-Entry Plan (Thorough) - 2026-03-29

## 1) Objective of This Plan

Recover full project control quickly and safely by:
- Reproducing the real three-way runtime path (server + AI + GUI)
- Isolating late-level AI deadlock behavior
- Confirming whether observer live updates are functioning beyond initial snapshot
- Establishing a practical implementation order for upcoming sessions

This plan intentionally separates **analysis/validation steps** from **future code changes**.

## 2) Working Assumptions

1. GUI bootstrap from server snapshot previously worked and likely still works.
2. AI can connect and play early game but may deadlock around coordinated ascension (around level 4).
3. Server progression requires explicit SIGUSR1 toggle to start/stop game time.
4. Campus machine can probably handle three processes if ramped gradually.

## 3) Plan Structure

Execution is split into 4 phases:
- Phase A: Environment and launch determinism
- Phase B: Integration truth-finding (three-way verification)
- Phase C: Deadlock diagnosis framework
- Phase D: Implementation roadmap after diagnosis

## 4) Phase A - Environment and Launch Determinism (Tomorrow, First 30-45 min)

## A1) Confirm build/launch commands on campus

Server:
- Build server with existing Makefile.
- Launch with explicit port/map/team args used by your project conventions.

Client:
- Compile Java client (`mvn compile`) before runtime tests.

Godot:
- Open project and run Main scene in real-server mode.
- Disable auto-start behavior if it still points to machine-specific path.

## A2) Confirm time handshake

- Start server and verify it is alive but time-paused by default.
- Trigger SIGUSR1 via `server/run.sh` from terminal.
- Observe log behavior for pause/resume transitions.

Deliverable for A:
- You can reliably and repeatably start/stop all three components without uncertainty.

## 5) Phase B - Integration Truth-Finding (Three-Way Runtime Ladder)

## B1) Test 1: Server + GUI only

Goal:
- Verify observer login and initial map/world build from live server.

Checks:
- GUI connects over `wss://`.
- Initial world appears consistent with server session parameters.

Expected output:
- Pass/fail statement for initial observer bootstrap.

## B2) Test 2: Server + 1 AI + GUI

Goal:
- Verify whether active game updates from AI actions are reflected in GUI.

Checks:
- AI receives welcome and sends command stream.
- Server accepts commands and mutates game state.
- GUI receives post-bootstrap updates (not just initial snapshot).

Expected output:
- Definitive answer to the unknown: "Do live client actions propagate to Godot?"

## B3) Test 3: Server + 3 AI + GUI

Goal:
- Reproduce multi-agent behaviors before high-level ascension complexity.

Checks:
- No immediate queue starvation.
- No widespread disconnection loops.
- Observe first coordination attempts via broadcast/incantation events.

Expected output:
- Baseline multiplayer behavior profile.

## B4) Test 4: Server + 6+ AI + GUI (bounded)

Goal:
- Push toward the level where deadlock was historically perceived.

Checks:
- Time-to-level progression.
- Where command flow plateaus.
- Whether agents loop on non-progressing actions.

Expected output:
- Reproducible deadlock signature with approximate timeline and conditions.

## 6) Phase C - Deadlock Diagnosis Framework (Evidence-Driven)

When deadlock appears, capture evidence in this order.

## C1) AI side evidence

Collect from logs:
- Command sequences per client near deadlock onset.
- Pending response behavior (queue pressure / starvation).
- Frequency of `inventaire` checks vs actual `incantation` attempts.
- Broadcast receive events and whether any movement/action follows.

Main hypotheses to validate:
1. `goToElevationCall` non-implementation blocks team convergence.
2. Decision branch repeatedly requests inventory instead of committing to elevation sequence.
3. Clients receive calls but do not execute follow-through actions.

## C2) Server side evidence

Collect from logs:
- Incantation validation failures (`ko`, missing players/resources conditions).
- Broadcast and event messages around same time window.
- Player/team level distribution on stalling tiles.

Main hypotheses to validate:
1. Server rule checks reject planned ascension due to real prerequisites not met.
2. Client strategy mismatch with server’s exact progression contract.

## C3) GUI side evidence

Collect from logs/view:
- Whether post-bootstrap messages are continuously received.
- Whether player positions/levels/resources visually update over time.

Main hypotheses to validate:
1. GUI update path is healthy and only mirrors AI deadlock reality.
2. GUI observer stream misses live updates due to observer-list handling gap.

## 7) Phase D - Implementation Roadmap (After Initial Campus Diagnostics)

Prioritized by impact-to-effort ratio.

## D1) Priority 0: Observability and confidence

1. Fix Java test API drift (`broadcast` vs `broadcastCmd`) to restore test compilation.
2. Add minimal focused tests for late-level decision transitions.
3. Add runbook notes for start order and SIGUSR1 timing.

Outcome:
- Faster iteration loop and fewer false assumptions.

## D2) Priority 1: AI deadlock core fixes

1. Implement `goToElevationCall` with at least minimal directional assembly behavior.
2. Correct ready-to-elevate branch to trigger `doElevation` path at appropriate times.
3. Introduce anti-deadlock strategy guards:
- retry windows
- fallback exploration if no progress
- bounded waiting on cooperative events

Outcome:
- AI can progress beyond mid-level coordination walls.

## D3) Priority 2: GUI portability and runtime ergonomics

1. Replace hardcoded absolute script path with configurable/project-relative path.
2. Keep manual signal fallback available.
3. Add clear startup mode indicators in GUI logs (mock vs real).

Outcome:
- Smooth setup on campus and teammate machines.

## D4) Priority 3: Observer continuity validation/fix

1. Verify observer registration persistence in server state.
2. If needed, implement proper add/remove observer list handling.
3. Retest server->observer event fan-out under load.

Outcome:
- Trustworthy real-time GUI updates.

## 8) Suggested Tomorrow Timeline (Practical)

## First 60-90 minutes

1. Bring all three components up deterministically.
2. Run Tests B1 and B2.
3. Decide immediately whether live updates are functioning.

## Next 90-120 minutes

1. Run B3/B4 until deadlock reproduces.
2. Capture concise evidence logs.
3. Confirm which hypothesis is primary (AI logic vs observer update path vs both).

## Final 30 minutes

1. Freeze findings.
2. Convert into actionable implementation tickets for the next session.

## 9) Success Criteria for Re-Entry

This re-entry phase is successful when you can answer “yes” to all:
1. Can I launch the full stack reliably without setup confusion?
2. Do I know whether GUI receives ongoing updates after bootstrap?
3. Can I reproduce the AI deadlock condition consistently?
4. Do I have enough evidence to implement fixes in correct order?

## 10) Concrete Ticket Backlog Seed (Post-Diagnosis)

1. AI: implement cooperative ascension movement path.
2. AI: fix ready-to-elevate branch regression.
3. Tests: restore Java unit test compilation and add ascension path tests.
4. Server: verify/fix observer list persistence and notification fan-out.
5. Godot: remove machine-specific auto-start path dependency.
6. Docs: write canonical launch + test matrix runbook.

## 11) Final Notes

- Do not optimize for performance first; optimize for deterministic correctness first.
- Keep map size and client count conservative while establishing ground truth.
- Once behavior is trustworthy at low scale, scale gradually.

This document is the action plan companion to the status report and should drive tomorrow’s first campus session.
