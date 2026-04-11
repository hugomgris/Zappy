#pragma once

#include "../net/WebsocketClient.hpp"
#include "CommandSender.hpp"
#include "WorldState.hpp"
#include "AI.hpp"
#include "../../incs/Result.hpp"

#include <string>
#include <atomic>
#include <thread>
#include <functional>

namespace zappy {
	class Client {
		using MessageCallback = std::function<void(const ServerMessage&)>;

		private:
			std::string	_host;
			int			_port;
			std::string	_teamName;

			WebsocketClient	_ws;
			CommandSender	_sender;
			WorldState		_state;
			AI				_ai;

			std::atomic<bool>				_running{false};
			std::unique_ptr<std::thread>	_networkThread;

			
			MessageCallback _messageCallback;

			void networkLoop();
			void processIncomingMessages(int64_t nowMs);
			Result waitForBienvenue(int64_t& nowMs, int timeoutMs);
			Result performLogin(int64_t& nowMs, int timeoutMs);

		public:
			Client(const std::string& host, int port, const std::string& teamName);
			~Client();

			// connection
			Result connect(int timeoutMs = 10000);
			Result run();
			void stop();

			// status
			bool isRunning() const { return _running; }
			bool isConnected() const { return _state.isConnected(); }
			const WorldState& getState() const { return _state; }

			// conf — forwarded to AI
			void setForkEnabled(bool enabled) { _ai.setForkEnabled(enabled); }

			//callbacks
			void onMessage(MessageCallback cb) { _messageCallback = cb; }
	};
} // namespace zappy