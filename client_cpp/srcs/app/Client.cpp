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

		// 1 wbesocket connection
		Result res = _ws.connect(_host, _port, true); // insecure for testing. TODO: handle secure
		if (!res.ok()) {
			return res;
		}

		int64_t nowMs = 0;
		auto startTime = std::chrono::steady_clock::now();

		// wait for websocket con
		while (!_ws.isConnected()) {
			_ws.tick(nowMs);
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

		Logger::info("Succesfully logged in as " + _teamName);
		return Result::success();
	}

	Result Client::waitForBienvenue(int64_t& nowMs, int timeoutMs) {
		auto startTime = std::chrono::steady_clock::now();

		while (true) {
			_ws.tick(nowMs);

			WebSocketFrame frame;
			IoResult io = _ws.recvFrame(frame);
			if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
				std::string text(frame.payload.begin(), frame.payload.end());
				ServerMessage msg = parseServerMessage(text);

				if (msg.type == ServerMessageType::Bienvenue) {
					Logger::info("Received bienvenue: " + msg.raw);
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
			_ws.tick(nowMs);
			
			WebSocketFrame frame;
			IoResult io = _ws.recvFrame(frame);
			if (io.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
				std::string text(frame.payload.begin(), frame.payload.end());
				ServerMessage msg = parseServerMessage(text);
				
				if (msg.type == ServerMessageType::Welcome) {
					_state.onWelcome(msg);
					Logger::info("Login successful! " + msg.raw);
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

		while (_running) {
			nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();

			// tick websocket
			Result tickRes = _ws.tick(nowMs);
			if (!tickRes.ok()) {
				Logger::error("Websocket error: " + tickRes.message);
				break;
			}

			// process incoming message
			processIncomingMessages(nowMs);

			// still connected???
			if (_ws.isConnected()) {
				Logger::error("Websocket disconnected");
				break;
			}

			// run AI
			_ai.tick(nowMs);

			// status logging
			if (nowMs - lastStatusTime > 10000) {
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
		
    	Logger::info("Network loop ended");
	}

	void Client::processIncomingMessages(int64_t nowMs) {
		(void) nowMs;

		WebSocketFrame frame;
		IoResult io = _ws.recvFrame(frame);

		while (io.status == NetStatus::Ok) {
			if (frame.opcode == WebSocketOpcode::Text) {
				std::string text(frame.payload.begin(), frame.payload.end());
				ServerMessage msg = parseServerMessage(text);
				
				// Update world state
				switch (msg.type) {
					case ServerMessageType::Response:
						_state.onResponse(msg);
						_sender.processResponse(msg);
						break;
					case ServerMessageType::Event:
						_state.onEvent(msg);
						break;
					case ServerMessageType::Message:
						_state.onMessage(msg);
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