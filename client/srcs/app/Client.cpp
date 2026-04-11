/*
 * Client.cpp
 *
 * Changes vs previous version:
 *
 * 1.  CommandSender::checkTimeouts() called every loop iteration.
 *     Previously timeouts were never checked, so a command dropped by the
 *     server (e.g. due to the event buffer being full) would leave
 *     _commandInFlight = true in the AI forever. Now timeouts are surfaced
 *     as error callbacks, which clear the flag via AI::clearInFlight().
 *     The timeout threshold matches AI::COMMAND_FLIGHT_TIMEOUT_MS.
 *
 * 2.  Initial sendVoir() on run() no longer registers an expectResponse —
 *     it is now a one-shot kick-start without a pending callback so it does
 *     not pollute the pending queue before the AI takes over.
 *
 * Everything else is unchanged from the previous rewrite.
 */

#include "Client.hpp"
#include "../helpers/Logger.hpp"

#include <chrono>
#include <thread>

namespace zappy {

Client::Client(const std::string& host, int port, const std::string& teamName)
    : _host(host), _port(port), _teamName(teamName)
    , _sender(_ws)
    , _ai(_state, _sender) {}

Client::~Client() {
    stop();
    _ws.close();
}

Result Client::connect(int timeoutMs) {
    Logger::info("Connecting to " + _host + ":" + std::to_string(_port));

    Result res = _ws.connect(_host, _port, true);
    if (!res.ok()) return res;

    int64_t nowMs = 0;
    auto startTime = std::chrono::steady_clock::now();

    while (!_ws.isConnected()) {
        Result tickRes = _ws.tick(nowMs);
        if (!tickRes.ok())
            return Result::failure(ErrorCode::NetworkError,
                                   "Websocket setup failed: " + tickRes.message);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        nowMs += 10;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsed > timeoutMs)
            return Result::failure(ErrorCode::NetworkError, "Websocket connection timeout");
    }
    Logger::info("Websocket connected");

    res = waitForBienvenue(nowMs, timeoutMs);
    if (!res.ok()) return res;

    res = performLogin(nowMs, timeoutMs);
    if (!res.ok()) return res;

    Logger::info("Logged in as " + _teamName);
    return Result::success();
}

Result Client::waitForBienvenue(int64_t& nowMs, int timeoutMs) {
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        Result tickRes = _ws.tick(nowMs);
        if (!tickRes.ok())
            return Result::failure(ErrorCode::NetworkError,
                                   "Websocket error waiting for bienvenue: " + tickRes.message);

        WebSocketFrame frame;
        IoResult io = _ws.recvFrame(frame);
        if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
            std::string text(frame.payload.begin(), frame.payload.end());
            ServerMessage msg = parseServerMessage(text);
            if (msg.type == ServerMessageType::Bienvenue) {
                Logger::info("Received bienvenue");
                if (_messageCallback) _messageCallback(msg);
                return Result::success();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        nowMs += 50;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsed > timeoutMs)
            return Result::failure(ErrorCode::ProtocolError, "No bienvenue received");
    }
}

Result Client::performLogin(int64_t& nowMs, int timeoutMs) {
    Result res = _sender.sendLogin(_teamName);
    if (!res.ok()) return res;

    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        Result tickRes = _ws.tick(nowMs);
        if (!tickRes.ok())
            return Result::failure(ErrorCode::NetworkError,
                                   "Websocket error during login: " + tickRes.message);

        WebSocketFrame frame;
        IoResult io = _ws.recvFrame(frame);
        if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
            std::string text(frame.payload.begin(), frame.payload.end());
            ServerMessage msg = parseServerMessage(text);

            if (msg.type == ServerMessageType::Welcome) {
                _state.onWelcome(msg);
                Logger::info("Login successful");
                if (_messageCallback) _messageCallback(msg);
                return Result::success();
            }
            if (msg.type == ServerMessageType::Error)
                return Result::failure(ErrorCode::ProtocolError, "Login error: " + msg.raw);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        nowMs += 50;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsed > timeoutMs)
            return Result::failure(ErrorCode::ProtocolError, "No welcome received");
    }
}

Result Client::run() {
    _running = true;
    // Kick-start: send voir without registering a pending callback.
    // The AI will issue its own voir once it starts ticking, properly tracked.
    (void)_sender.sendVoir();
    _networkThread = std::make_unique<std::thread>(&Client::networkLoop, this);
    return Result::success();
}

void Client::stop() {
    _running = false;
    if (_networkThread && _networkThread->joinable())
        _networkThread->join();
    _sender.cancelAll();
}

void Client::networkLoop() {
    int64_t nowMs = 0;
    int64_t lastStatusTime    = 0;
    int     reconnectAttempts = 0;
    const int MAX_RECONNECT   = 5;

    while (_running) {
        nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        Result tickRes = _ws.tick(nowMs);
        if (!tickRes.ok()) {
            Logger::error("Websocket tick error: " + tickRes.message);
            _running = false;
            break;
        }

        processIncomingMessages(nowMs);

        if (!_ws.isConnected()) {
            if (reconnectAttempts >= MAX_RECONNECT) {
                Logger::error("Max reconnect attempts reached");
                break;
            }
            reconnectAttempts++;
            Logger::warn("Disconnected, reconnect attempt " +
                         std::to_string(reconnectAttempts));
            std::this_thread::sleep_for(std::chrono::seconds(2));

            Result res = connect();
            if (!res.ok()) {
                Logger::error("Reconnection failed: " + res.message);
                continue;
            }
            reconnectAttempts = 0;
            Logger::info("Reconnected");
            continue;
        }

        // FIX 1 (client side): Check for timed-out pending commands every loop.
        // This surfaces dropped commands as error callbacks, which clears
        // AI::_commandInFlight so the AI does not stall permanently.
        _sender.checkTimeouts(AI::COMMAND_FLIGHT_TIMEOUT_MS);

        _ai.tick(nowMs);

        if (nowMs - lastStatusTime > 5000) {
            Logger::info("Status: level=" + std::to_string(_state.getLevel()) +
                         " food=" + std::to_string(_state.getFood()) +
                         " forks=" + std::to_string(_state.getForkCount()));
            lastStatusTime = nowMs;
        }

        if (_state.getLevel() >= 8)
            Logger::info("VICTORY! Reached level 8!");

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    _running = false;
    Logger::info("Network loop ended");
}

void Client::processIncomingMessages(int64_t nowMs) {
    (void)nowMs;

    WebSocketFrame frame;
    IoResult io = _ws.recvFrame(frame);

    while (io.status == NetStatus::Ok) {
        if (frame.opcode == WebSocketOpcode::Text) {
            std::string text(frame.payload.begin(), frame.payload.end());
            Logger::debug("RX: " + text);

            ServerMessage msg = parseServerMessage(text);

            if (msg.isDeath()) {
                _state.onEvent(msg);
                Logger::error("Player died! Stopping.");
                _running = false;
                break;
            }

            switch (msg.type) {
                case ServerMessageType::Response:
                    // 1. Update world state FIRST
                    _state.onResponse(msg);
                    // 2. Dispatch to sender (fires AI callback, which sees fresh state)
                    _sender.processResponse(msg);
                    break;

                case ServerMessageType::Event:
                    _state.onEvent(msg);
                    _ai.onMessage(msg);
                    break;

                case ServerMessageType::Message:
                    _state.onMessage(msg);
                    _ai.onMessage(msg);
                    break;

                case ServerMessageType::Error:
                    Logger::error("Server error: " + msg.raw);
                    break;

                default:
                    break;
            }

            if (_messageCallback) _messageCallback(msg);
        }
        else if (frame.opcode == WebSocketOpcode::Ping) {
            _ws.sendPong();
        }
        else if (frame.opcode == WebSocketOpcode::Close) {
            Logger::info("Received close frame");
            break;
        }

        io = _ws.recvFrame(frame);
    }
}

} // namespace zappy
