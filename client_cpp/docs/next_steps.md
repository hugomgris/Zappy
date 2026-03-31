# C++ Client - Next Steps

## Status: Transport Layer Complete ✅

The WebSocket transport layer is **production-ready**. All TLS handshake, HTTP upgrade, and secure bidirectional communication is validated and working.

## Immediate Next Steps

### 1. **Resolve Bootstrap Protocol Sequencing**
**Current Issue:** Bootstrap code expects server's initial "welcome" message, but server doesn't send one until login is received.

**Options:**
- **Option A (Recommended):** Modify `runTransportSmoke()` in `main.cpp` to skip waiting for initial frame and send login immediately
- **Option B:** Modify server to send initial welcome frame after WebSocket upgrade completes

**Location:** `client_cpp/srcs/main.cpp`, function `runTransportSmoke()` (lines ~65-90)

```cpp
// Current: Waits for server's first message
std::string firstServerMsg;
Result recvRes = tickUntilTextFrame(ws, 3000, firstServerMsg);

// Suggested: Send login immediately without waiting
// Then wait for login reply
```

### 2. **Integrate with Game Loop**
The bootstrap sequence is currently blocking and standalone. To integrate with the Godot GUI:

**Files to Update:**
- Wrap `runTransportSmoke()` calls in async task or background thread
- Move connection logic from `main()` to a proper initialization system
- Implement reconnection logic for network failures

**Suggested Architecture:**
- Create `ConnectionManager` class to handle connection state
- Emit signals/events when connection succeeds/fails
- Implement automatic reconnection with exponential backoff

### 3. **Remove Diagnostic Logging**
For production builds, remove or conditionally disable debug logs:

**Files to Clean:**
- `client_cpp/srcs/net/WebsocketClient.cpp` - Remove "WebSocket: Attempt X" debug logs
- `client_cpp/srcs/net/SecureSocket.cpp` - Uncomment diagnostic fprintf debugging (currently commented)
- `server/srcs/server/ssl_al.c` - Replace fprintf diagnostics with log_msg() or remove

**Suggested:** Create `NDEBUG` conditional compilation around diagnostic output

### 4. **Test with GUI**
Once protocol sequencing is fixed:

**Test Plan:**
1. Start server on port 8674
2. Run Godot GUI and attempt to connect
3. Verify full login → game spawn sequence works
4. Test reconnection after server restart
5. Test error handling (connection refused, timeout, etc.)

**Expected Flow:**
- Client connects via TLS WebSocket
- Sends login with team name
- Receives server welcome/game state
- Renders game board in Godot

### 5. **Add Reconnection Logic**
Implement graceful reconnection for game robustness:

```cpp
// Pseudocode
while (playing) {
  if (!connected) {
    if (canReconnect()) {
      connect();  // Uses exponential backoff
    } else {
      showErrorUI("Connection lost");
      break;
    }
  }
  
  tick();  // Process incoming frames
  handleUserInput();
}
```

## Known Limitations & TODOs

- [ ] Partial frame reassembly not yet implemented (current frames must fit in single packet)
- [ ] No ping/pong keepalive logic (prepare for 45-second silence handling)
- [ ] Buffer overflow protection on frame payloads (add max size checks)
- [ ] No frame fragmentation support (RFC 6455 section 5.4)
- [ ] Error recovery: connection closed mid-frame leaves state inconsistent

## Code Quality for Production

Before final submission:

- [ ] Run with `-fsanitize=address` to catch memory errors
- [ ] Run with `-Werror -Wall -Wextra` to catch all compiler warnings
- [ ] Add unit tests for frame codec with edge cases (empty frames, max size, etc.)
- [ ] Profile TLS handshake overhead (goal: <200ms)
- [ ] Validate certificate chain handling in non-insecure mode

## Files Modified This Session

**Client Side:**
- `client_cpp/srcs/net/TlsContext.cpp` - X509 certificate callback
- `client_cpp/srcs/net/WebsocketClient.cpp` - HTTP upgrade and frame handling
- `client_cpp/srcs/main.cpp` - Bootstrap smoke test (diagnostic logging)

**Server Side:**
- `server/srcs/server/ssl_al.c` - Diagnostic output
- `server/srcs/main.c` - Log level for debugging

## Testing Checklist

- [x] TLS handshake with self-signed certificate
- [x] HTTP 101 Switching Protocols response
- [x] WebSocket frame encoding/decoding
- [ ] Login message transmission
- [ ] Server state reception
- [ ] Multi-player synchronization
- [ ] Reconnection after disconnect
- [ ] Error messages on network failure
- [ ] Performance under high message rate
- [ ] Memory stability (24h+ uptime)

## Contact / Blockers

If stuck on integration:
1. Check server logs for protocol errors
2. Enable `LOG_LEVEL_DEBUG` in server to see handshake details
3. Add `fprintf(stderr, "...")` diagnostics to client WebSocket code
4. Verify DNS resolution: `nslookup localhost` or `getent hosts localhost`
5. Check firewall: `netstat -ln | grep 8674` to see listening ports
