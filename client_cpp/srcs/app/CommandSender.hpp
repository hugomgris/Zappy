#pragma once

#include "net/WebsocketClient.hpp"
#include "Result.hpp"
#include "ProtocolTypes.hpp"

#include <cJSON.h>
#include <functional>
#include <deque>
#include <chrono>
#include <mutex>
#include <algorithm>

namespace zappy {
	class CommandSender {
		public:
			struct PendingCommand {
				uint64_t									id;
				std::string									cmd;
				std::chrono::steady_clock::time_point		sentAt;
				std::function<void(const ServerMessage&)>	callback;
			};
		
		private:
			WebsocketClient&			_ws;
			std::deque<PendingCommand>	_pending;  // FIXED: Changed from queue to deque for search
			std::mutex					_mutex;
			uint64_t					_nextId = 1;

			Result sendRaw(const std::string& json);
			Result sendCommandObj(cJSON* cmd);

		public:
			explicit CommandSender(WebsocketClient& ws);
			~CommandSender() = default;

			// auth
			Result sendLogin(const std::string& teamName, const std::string& key = "SOME_KEY");

			// movement
			Result sendAvance();
			Result sendDroite();
			Result sendGauche();

			// info
			Result sendVoir();
			Result sendInventaire();
			Result sendConnectNbr();

			// inventory actions
			Result sendPrend(const std::string& resource);
			Result sendPose(const std::string& resource);

			// social
			Result sendExpulse();
			Result sendBroadcast(const std::string& msg);

			// progression
			Result sendIncantation();
			Result sendFork();

			// response tracking
			uint64_t expectResponse(const std::string& cmd, std::function<void(const ServerMessage&)> cb);
			void processResponse(const ServerMessage& msg);
			void checkTimeouts(int timeoutMs = 30000);

			size_t pendingCount() const { return _pending.size(); }
			void cancelAll();
	};
} // namespace zappy