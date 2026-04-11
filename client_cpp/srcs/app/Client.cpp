#include "Client.hpp"
#include "helpers/Logger.hpp"

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

		// 1 websocket connection
		Result res = _ws.connect(_host, _port, true); // insecure for testing. TODO: handle secure
		if (!res.ok()) {
			return res;
		}

		int64_t nowMs = 0;
		auto startTime = std::chrono::steady_clock::now();

		// wait for websocket con
		while (!_ws.isConnected()) {
			Result tickRes = _ws.tick(nowMs);
			if (!tickRes.ok()) {
				return Result::failure(ErrorCode::NetworkError, "Websocket setup failed: " + tickRes.message);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			nowMs += 10;

			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - startTime).count();
			if (elapsed > timeoutMs) {
				return Result::failure(ErrorCode::NetworkError, "Websocket connection timeout");
			}
		}
		Logger::info("Websocket connected");

		// wait for bienvenue
		res = waitForBienvenue(nowMs, timeoutMs);
		if (!res.ok()) return res;

		// login
		res = performLogin(nowMs, timeoutMs);
		if (!res.ok()) return res;

		Logger::info("Successfully logged in as " + _teamName);
		return Result::success();
	}

	Result Client::waitForBienvenue(int64_t& nowMs, int timeoutMs) {
		auto startTime = std::chrono::steady_clock::now();

		while (true) {
			Result tickRes = _ws.tick(nowMs);
			if (!tickRes.ok()) {
				return Result::failure(ErrorCode::NetworkError, "Websocket error while waiting bienvenue: " + tickRes.message);
			}

			WebSocketFrame frame;
			IoResult io = _ws.recvFrame(frame);
			if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
				std::string text(frame.payload.begin(), frame.payload.end());
				ServerMessage msg = parseServerMessage(text);

				if (msg.type == ServerMessageType::Bienvenue) {
					Logger::info("Received bienvenue: " + msg.raw);
					if (_messageCallback) {
						_messageCallback(msg);
					}
					return Result::success();
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			nowMs += 50;
			
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - startTime).count();
			if (elapsed > timeoutMs) {
				return Result::failure(ErrorCode::ProtocolError, "No bienvenue message received");
			}
		}
	}

	Result Client::performLogin(int64_t& nowMs, int timeoutMs) {
		// Send login
		Result res = _sender.sendLogin(_teamName);
		if (!res.ok()) return res;
		
		auto startTime = std::chrono::steady_clock::now();
		
		while (true) {
			Result tickRes = _ws.tick(nowMs);
			if (!tickRes.ok()) {
				return Result::failure(ErrorCode::NetworkError, "Websocket error during login: " + tickRes.message);
			}
			
			WebSocketFrame frame;
			IoResult io = _ws.recvFrame(frame);
			if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
				std::string text(frame.payload.begin(), frame.payload.end());
				ServerMessage msg = parseServerMessage(text);
				
				if (msg.type == ServerMessageType::Welcome) {
					_state.onWelcome(msg);
					Logger::info("Login successful! " + msg.raw);
					if (_messageCallback) {
						_messageCallback(msg);
					}
					return Result::success();
				}
				
				if (msg.type == ServerMessageType::Error) {
					return Result::failure(ErrorCode::ProtocolError, "Login error: " + msg.raw);
				}
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			nowMs += 50;
			
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - startTime).count();
			if (elapsed > timeoutMs) {
				return Result::failure(ErrorCode::ProtocolError, "No welcome message received");
			}
		}
	}

	Result Client::run() {
		_running = true;
		// Kick-start activity early to avoid idle disconnects on strict servers.
		(void)_sender.sendVoir();
		_networkThread = std::make_unique<std::thread>(&Client::networkLoop, this);
		return Result::success();
	}

	void Client::stop() {
		_running = false;
		if (_networkThread && _networkThread->joinable()) {
			_networkThread->join();
		}
		_sender.cancelAll();
	}

	void Client::networkLoop() {
		int64_t nowMs = 0;
		int64_t lastStatusTime = 0;
		int reconnectAttempts = 0;
		const int MAX_RECONNECT_ATTEMPTS = 5;

		while (_running) {
			nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();

			// tick websocket
			Result tickRes = _ws.tick(nowMs);
			if (!tickRes.ok()) {
				Logger::error("Websocket error: " + tickRes.message);
				_running = false;
				break;
			}

			// process incoming message
			processIncomingMessages(nowMs);

			// FIXED: Check if disconnected (was inverted)
			if (!_ws.isConnected()) {
				Logger::warn("WebSocket disconnected, attempting reconnect...");
				
				if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
					Logger::error("Max reconnect attempts reached, giving up");
					break;
				}
				
				reconnectAttempts++;
				std::this_thread::sleep_for(std::chrono::seconds(2));
				
				Result res = connect();
				if (!res.ok()) {
					Logger::error("Reconnection failed: " + res.message);
					continue;
				}
				
				reconnectAttempts = 0;
				Logger::info("Reconnected successfully");
				continue;
			}

			// run AI
			_ai.tick(nowMs);

			// status logging
			if (nowMs - lastStatusTime > 1000) {
				Logger::info("Status: Level=" + std::to_string(_state.getLevel()) +
							" Food=" + std::to_string(_state.getFood()) +
							" Forks=" + std::to_string(_state.getForkCount()));
				lastStatusTime = nowMs;
			}

			// Check win condition
			if (_state.getLevel() >= 8) {
				Logger::info("VICTORY! Reached level 8!");
				// Keep running but log victory
			}
			
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		_running = false;
		Logger::info("Network loop ended");
	}

	void Client::processIncomingMessages(int64_t nowMs) {
		(void) nowMs;

		WebSocketFrame frame;
		IoResult io = _ws.recvFrame(frame);

		while (io.status == NetStatus::Ok) {
			Logger::info("Received frame, opcode=" + std::to_string(static_cast<int>(frame.opcode)) + 
				", payload size=" + std::to_string(frame.payload.size()));
				
			if (frame.opcode == WebSocketOpcode::Text) {
				std::string text(frame.payload.begin(), frame.payload.end());
				Logger::info("RECEIVED TEXT: " + text);
				ServerMessage msg = parseServerMessage(text);
				
				// Update world state
				if (msg.isDeath()) {
					_state.onEvent(msg);
					Logger::error("Player died! Exiting client loop.");
					_running = false;
					break;
				}

				switch (msg.type) {
					case ServerMessageType::Response:
						Logger::info("Processing response, cmd=" + msg.cmd + ", status=" + msg.status);
						_state.onResponse(msg);
						_sender.processResponse(msg);
						break;
					case ServerMessageType::Event:
						_state.onEvent(msg);
						_ai.onMessage(msg);  // Let AI handle incantation_start and Level up
						break;
					case ServerMessageType::Message:
						_state.onMessage(msg);
						_ai.onMessage(msg);  // FIXED: Forward to AI for coordination
						break;
					case ServerMessageType::Error:
						Logger::error("Server error: " + msg.raw);
						break;
					default:
						break;
				}
				
				// Callback
				if (_messageCallback) {
					_messageCallback(msg);
				}
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