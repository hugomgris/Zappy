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
