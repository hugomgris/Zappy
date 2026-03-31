# Zappy Client - Devlog - 1

## Table of Contents
1. [There and Back Again (In a Network Sense)](#11---there-and-back-again-in-a-network-sense)


<br>
<br>

# 1.1 - There and Back Again (In a Network Sense)
I could start this log by saying something like *New Project, New Me*, but that would be a blatant lie. Truth be told, this devlog finds me in a somewhat middle point of the development, after a handful of months worth hiatus from the 3 poeple team that set sails to build this *zappy* some forgotten amount of time ago. Back then, my main role was to build the GUI side of the project (my two partners were in charge of the Server and the Client), which I developed in Godot until the GUI-Server communication milestone was achieved. This means that on my side (and on the server's side), the Godot GUI could (and still can) connect to the initialized C server, receive an initial game session setup and build and display the game arena accordingly. All fine and dandy there (well, at some point I'm going to have to go back to the GUI and work my bottom off, but today is not that day), and the general assesment at this re-entry point is that we have a functional GUI, a working server and a (very) WIP client. This client was originally written in Java and had a considerable part of its implementation already done, but because I'm now taking over its development some decisions are going to be made (by me). The most critical: **we are going to build an AI client for Zappy from scratch in my beloved C++**, using what was already done in Java as reference, but essentially starting from scratch. An interesting journey lays in front of us, then, one that will take me through some not-so-familiar roads, as I'm used to engine, systems, graphics and gameplay programming, not so much to network client building. An opportunity, as they say, to change hats and reinforce my current knwoledge of the subject and learn new things.

As always, right in that very moment before taking the dreaded first step, writing a roadmap sounds advisiable, so we could start getting into motion with that. And we can do it by analysing what is already done in the Java client and mixing it up with the task's requirements, which could give us the following milestones:

0. **Build control and Bootstrapping**
	- Makefile with strict building paths and profile splitting
	- Zero-dependency baseline build on Linnux dumps
	- A basic logger and error model (more on why later)
		- *At this pint, the build pipeline should work in both debug and release profiles*
1.  **CLI compatibility**
	- Manage the required flags: -n team -p port -h host (defaulted to localhost)
	- Optional bonus flags (right now I don't know which, so, yeah). Some possibilities: --ws, --wss, --insecure, --protocol text|json
		- *At this point, the usage behavior should mirror the task expectation for required flags*
2. **Transport Layer**
	- TCP + TLS + Websocket client implementation
	- Frame read/write, ping/pong, close handling
	- Backpressure-safe send queue.
		- *At this point, the client should connect and complete the JSON login flow in project mode, as well as handling websocket command/response stream without blocking or active wait*
3. **Protocol Layer**
	- JSON codec for login, cmd, response, message, event.
	- Strong validation and unknown-field tolerance.
	- Structured internal message model independent of transport.
		- *ATP, there should be a successfull JSON handshake/login in a live run, as well as a parsed and classified observed project response/event/message payloads*.
	- (OPTIONAL) Text serializer/parser for classic BIENVENUE/newline protocol.
	- (OPTIONAL) Runtime adapter boundary that does not affect JSON mode.
4. **Command pipeline**
	- In-flight cap hard-limited to 10.
	- Command queue with dedupe policies (for repeated voir and identical broadcast payloads)
	- Response-driven credit return
		- *ATP, client should never exceed 10 pending request and a stable command flow should be sustained for a 2-5 minute run*
5. **World and Player State Store**
	- Player state: level, inventory, life estimate, position/orientation confidence.
	- Vision decoding to local world model.
	- Team and connection counters
		- *ATP, state updates should match server responses over sustained runs*
6. AI State Machine v1 (Single Agent Survival)
	- Explicit SM:
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
		- Starvation guard
		- cooldowns to avoid spam
		- no command storms
			- *ATP, a single agent should survive and progress with no deadlock loops* (Something NOT happening in the Java client)
7. **Team Coordination v1 (Broadcast)**
	- Broadcat parser/handler with directional convergence behavior.
	- Elevation call protocol for same-level grouping.
	- Timeout and fallback if regroup fails.
		- *ATP, a 3 agent test should reach at least one succesful coordinated elevation*
8. **Reproduction and Scaling
	- fork and connect_nbr policies
	- Spawn policy thresholds based on map and food confidence
		- *ATP, there should be a controlled client growth without immediate starvatiion collapse*
9. **OPTIONAL Compatibility Adapter (Text Protocol)**
	- Text serializer/parser for classic protocol
	- Newline transport wiring
	- Runtime switch between text and json protocol modules
		- *ATP, a text adapter should be in place and working in compatibility without breaking JSON project mode*
10. **Hardening and Documentation**
	- Full runbook for server +1, +3, +6, +10, +50 clients
	- Troubleshooting matrix
	- Performance and stability checklist
		- *ATP, there should be reproducible runs from clean environment follwing docs only*

That was intense, phew. Now, because I've been in the situation of waiting almost until the project reaches a production status to write test suites (BAD), a from-the-beginning automated test writing protocol should also be set in place. I'll base it in what I know, GTest, and target these suites:

- **Unit tests**
	- Protocol parsing/serialization fixtures
	- Queue credit logic and dedupe behavior
	- State Machine transitions and cooldown behavior
- **Integration tests**
	- JSON communication smoke runs (1, 3, 6, n clients)
	- Fault injection:
		- delayed responses
		- malformed JSON and malformed lines (for optional adapter)
		- forced disconnects
		- ping/pong bursts
- **Runtime assertions**
	- pending_count <= 10 always
	- no infinite tight loop without outgoing I/O or state change

> *If down the line the compilation times of the test suites starts going out of control, I'll consider alternative tools lke Catch2 or doctest*

Besides all of this, some general considerations regarding the technical baseline of the project:
- C++20
- Makefile
- GTest
- nlohmann/json
- OpenSSL for TLS

And, while we're at it, we can define a **Phase 1 objective**:
- New C++ client logs in and plays autonomously in JSON project mode
- No pending command overflow beyond 10
- Single-client behavior is stable for 5+ minutes
- 3-client run achieves at least one successful cooperative elevation
- Runbook allows repeatable execution in any environment

All of which will rest on these modules:
- `app/`
	- main entrypoint, CLI parsing, lifecycle
- `net/`
	- socket, TLS, websocket, reconnect/session wrappers
- `protocol/`
	- serializer/deserializer interfaces
	- json_protocol
	- text_protocol (OPTIONAL)
- `engine/`
	- command scheduler (max in-flight 10)
	- response correlator
	- state store (player/map/inventory/team)
- `ai/`
	- strategy layer
	- state machine
	- elevation coordinator
- `runtime/`
	- tick loopp, timers, watchdogs, logging/tracing
- `tests/`
	- unit tests, protocol fixtures, integration harness

> *Also, let's keep transport, protocol and AI strategy independent via interfaces*

And we're ready to start. L E T ' S  G O ! ! !

<br>
<br>

# 1.2 - A Small Step for Zappy, an Irrelevant Step for Humanity
First thing's first: project setup. Nothing strange or fancy, just a very default-y Makefile with profiles, test rules and a bootstrapper with a basic logger. These two latter items being somewhat new to my usual project entryway, so let's talk about them. A concrete bootstrap step at the doors of `main()`, wired to a logger is needed in this project due to its nature: **an autonomous networked client**.
- The **bootstrap** will act as a safety gate. If you're unfamiliar with the concept, just take it as an **initialization phase** that runs once when the client starts, before anything else happens, a kind of *startup ritual** where the runtime environment is prepared.
	- The command-line arguments are parsed
	- The transport layer is inizialized (socket, TLS, WebSocket)
	- The configuration is laoded and validated
	- The command queue and State Machine are set up
	- There is an early return if anything fails (fail-fast approach)
- The **logger** is critical because there is no interactive debugging. I'm not going to be able to easily set a breakpoint or step through code when the client is running because of its assynchronous nature, while waiting for network responses or scheduling timed events. a text ouput is going to be needed to understand what happens during executions
	- Also, this will give the project a reproducible failure diagnosis. When something goes wrong along a run with several coordinated clients, the need for a complete trace of every decision, netweork event, and state transitions will set itself as a MUST. The logger will we our sword and our shield in that regard.
	- With several levels, verbosity will be controlled:
		- **Debug**: Every tiny decision (currently exploring tile 5, checking inventory, etc)
		- **Info**: Major state changes (login successful, elevation attempt started)
		- **Warn**: Recoverable problems (no allies nearby, retrying connection)
		- **Error**: Fatal problems (connection failed, protocol violated)
	- Also, this is **thread-safe**.

Anyways, the logger itself is not that big of a deal, quite rudimental. The `bootstrapper`, on its hand, could use some devloggin'. As stated before, it is going to be a safegate for the client initialization process, which will undergo the sequential steps stated above.

### 1.2.1 Argument Parsing
No need to say too much about this. If you're reading this, I'm sure you've parsed dozens of arguments in a C/C++ context. Run of the mill: **check amount, capture content, validate data**. The usual first step in a program like this AI Client, that kick-starting action that sets things in montion and quickly returns something that compiles, does *something*, tests the output pipelines (debug, info, all the layers in the `Logger`), and is in itself targeteable for the automated test suite.

### 1.2.2. Transport Layer Initialization
Now we're talking. This second bootstrap step is going to need a triple attack combination:
1. **TCP layer (Foundation)**
	- Raw socket with connection/disconnection lifecycel
	- Non-blocking I/O
	- Read/Write buffers with proper backpressure handling
	- Graceful reconnection logic with exponential backoff
2. **TSL Layer (Security wrapper)**
	- OpenSSL integratin wraps the TCP socket
	- Handshake negotiation (happens once, post-connect)
	- Endcrypted read/write that transforms plaintext buffers to/from ciphertext
	- Certificate validation (or `--insecure` bypass for testing)
	- Session reuse/resumption (ptional)
3. **WebSocket Layer (Protocol Framing)**
	- Sits on top of the TLS layer
	- Handles frame encoding/decoding (RFC 6455 compliance)
	- Implements ping/pong keepalive (prevents idle timeots)
	- Manages connection upgrade (HTTP 101 Switching Protocols handshae)
	- Handes close frames gracefully

To build this, we're going to need `OpenSSL`, which means adding some flags to the building process in `Makefile`, and we could also use some external tools like **Base64 library** (for frame masking) and **SHA-1 hasher** (for handshake). The structure planned is the following:
```
┌─────────────────────────────────────────────┐
│  WebSocket Frame Handler (public API)       │  Knows: frames, ping/pong, close
│  - readFrame() → FrameData                  │
│  - writeFrame(FrameData) → enqueue          │
│  - isPingExpected() → bool                  │
└────────────────────────┬────────────────────┘
                         │
┌─────────────────────────────────────────────┐
│  TLS Wrapper (transparent encryption)       │  Knows: handshake, ciphers
│  - tlsRead(buffer) → bytes_read             │
│  - tlsWrite(buffer) → bytes_written         │
│  - isHandshakeDone() → bool                 │
└────────────────────────┬────────────────────┘
                         │
┌─────────────────────────────────────────────┐
│  TCP Socket Manager (base I/O)              │  Knows: connect, FDs, buffers
│  - connect(host, port, tls_mode)            │
│  - read(buffer, max_bytes) → bytes_read     │
│  - write(buffer, bytes) → bytes_written     │
│  - close()                                  │
│  - isConnected() → bool                     │
└─────────────────────────────────────────────┘
```
> *The important key here: each layer exposes read/write ops and status queries, and lower layers don't know about upper layers*

In a more specific way, as well as more helpful regarding how the hell to write this three layers, the main idea is:
- TCP Baseline: low-level socket operations (creation, connection, binding, listening), with non-blocking flag set
- TSL Wrapping: certificate handling and context creation, handshake management, encryption
- Websocket Framing: encoding/decoding, ping/pong, frame handling, connection orchestration

Before starting, though, my research tells me that some decisions need to be made regarding how the data is going to be sent through the transport layer, what's going to be the Ping interval, what reconnect strategy is going to be put inn place, and what OpenSSL version is going to be handled. This are the initial decisions, which might change down the development line:
- **Deque based send queue**
	- **Non-blocking I/O safety**: `send()` can fail with `EAGAIN`/`EWOULDBLOCK` if the OS buffer is full. A queue allows a retry without losing data.
	- **Backpressure design**: A couple of milestones in a 10 in-flight commands maximum will be enforced. The queue will become the credit accounting mechanism, so that when a response arrives, the queue is popped and a credit is freed. If we start coding with this in mind we won't have to rework the architecture later (much more painfully).
	- **Flow stability**: without a queue, the AI layer would block. With a queue, AI keeps producing commands, and the network layer handles timing independently.
```
AI Layer                  Network Layer
   |                          |
   +---> enqueue_frame() ---> [SendQueue: deque<Frame>]
                               |
                         tick() / flush()
                               |
                         WebSocket::write()
                               |
                         TLS::write()
                               |
                         TCP::send()
```
> The queue will be **internal to WebsocketClient**. Each `tick()` makes a `send()` happen.

- **Ping Interval of 45 seconds** (configurable via class constant, maybe through config file)
	- **30-60 seconds** is RFC guidance, so 45 is a safe middle ground
	- **Sustain without spamming**, as many servers timeout idle connections at 60-120 seconds.
	- **Configurable** by setting it up as a static constant in `WebsocketClient`.
```cpp
class WebsocketClient {
    static constexpr int PING_INTERVAL_SECONDS = 45;
    int64_t last_ping_time = 0;  // in milliseconds or ticks
    
    void tick(int64_t now_ms) {
        if (now_ms - last_ping_time > PING_INTERVAL_SECONDS * 1000) {
            sendPing();
            last_ping_time = now_ms;
        }
    }
};
```

- **Fail hard and stop for reconnections**
	- Keeps things simple at this stage (and testable)
	- After achieving 5+ minutes stability with a single client, a more complex reconnection pipeline is relegated to a later milestone
	- This prevents an accidental "silent failure loop", i.e. a client silently retrying forever without logging
```cpp
Result WebsocketClient::connect(const std::string& host, int port) {
    Result tcp_res = tcp_socket_.connect(host, port);
    if (!tcp_res.ok()) {
        Logger::error("TCP connect failed: " + tcp_res.message);
        return tcp_res;  // Propagate to main, which exits
    }
    
    // TLS handshake, WebSocket upgrade...
    // If ANY step fails, return error and let main handle it
}
```
> It seems quite important to correctly, clearly and thoroughly log any failure at this point (errno, TLS error code, etc).

- **OpenSSL 3.x accepting APIs from 1.1.x**
	- This is a compatiility shim that works on both old and new OpenSSL (and was already defined, so...)

#### 1.2.2.1 Transmission Control Protocol
