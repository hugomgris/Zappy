// ================================================================
// FIX: tickClaimingLeader() double-send bug
//
// Root cause: _commandInFlight is set to true inside
// tickClaimingLeader(), but the tick() dispatcher can call
// tickClaimingLeader() again on the very next loop iteration
// because vision/inventory refreshes run (and their callbacks
// complete) *before* the switch dispatch, clearing
// _commandInFlight each time.  Result: claim_leader is sent
// 2-3 times before any response returns; the 2nd and 3rd sends
// get "ko" (correctly — the flag is now set), which overwrites
// the "ok" state from the first send.
//
// Fix: add a _claimSent latch to Behavior, set it when we send,
// clear it when we leave the ClaimingLeader state.
// ================================================================

// ----------------------------------------------------------------
// CHANGE 1 — Behavior.hpp
// Add this field alongside the other bool flags (e.g. near
// _forkInProgress):
// ----------------------------------------------------------------

    bool _claimSent = false;   // ← ADD THIS

// ----------------------------------------------------------------
// CHANGE 2 — Behavior.cpp: replace tickClaimingLeader() entirely
// ----------------------------------------------------------------

void Behavior::tickClaimingLeader() {
    // Hard latch: once we've sent claim_leader, do nothing until
    // the response callback transitions us out of this state.
    // Without this, the tick loop re-enters here on every cycle
    // because vision/inventory refreshes clear _commandInFlight
    // before the switch dispatch runs.
    if (_claimSent) return;

    _claimSent = true;
    _commandInFlight = true;

    Logger::info("Behavior: sending claim_leader for level " +
        std::to_string(_state.player.level));

    _sender.sendClaimLeader();
    _sender.expect("claim_leader", [this](const ServerMessage& msg) {
        _commandInFlight = false;
        _claimSent = false;   // reset latch regardless of outcome

        if (msg.isOk()) {
            Logger::info("Behavior: claim_leader OK — entering Leading");
            _isLeader             = true;
            _rallyLevel           = _state.player.level;
            _peerConfirmedCount   = 0;
            _lastRallyBroadcastMs = 0;   // force immediate first broadcast
            _leadingTimeoutMs     = 0;   // tickLeading init guard uses 0
            _aiState = AIState::Leading;

        } else {
            // "ko" or timeout: another client already owns leadership.
            // Park at -1 until the leader's RALLY broadcast tells us
            // which direction to move.
            Logger::info("Behavior: claim_leader KO — entering MovingToRally as follower");
            _isLeader           = false;
            _isMovingToRally    = false;   // let tickMovingToRally re-init
            _broadcastDirection = -1;      // -1 = haven't heard RALLY yet
            _aiState = AIState::MovingToRally;
        }
    });
}

// ----------------------------------------------------------------
// CHANGE 3 — Behavior.cpp: reset _claimSent in disbandRally()
// Add this line at the top of disbandRally(), alongside the other
// resets:
// ----------------------------------------------------------------

    _claimSent = false;   // ← ADD THIS LINE in disbandRally()

// ----------------------------------------------------------------
// No other files need changes.
// MessageParser, Sender, State, Navigator — all untouched.
// ----------------------------------------------------------------
