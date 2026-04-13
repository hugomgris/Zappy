#pragma once

#include "../net/WebsocketClient.hpp"
#include "Message.hpp"

#include <functional>
#include <chrono>
#include <deque>

class Sender {
	public:
			struct PendingCommand {
				std::string									cmd;
				std::chrono::steady_clock::time_point		sentAt;
				std::function<void(const ServerMessage&)>	callback;
			};

	private:
		WebsocketClient&			_ws;
		std::deque<PendingCommand>	_pending;

		// helper
		Result sendObject(const std::string& dump);

	public:
		explicit Sender(WebsocketClient& ws);
		~Sender() = default;

		// cmd handlers
		Result sendLogin(const std::string& teamName, const std::string& key);

		Result sendVoir();
		Result sendInventaire();
		

		Result sendAvance();
		Result sendDroite();
		Result sendGauche();

		Result sendPrend(const std::string& resource);
		Result sendPose(const std::string& resource);

		Result sendBroadcast(const std::string& msg);
		
		Result sendIncantation();
		Result sendFork();
		Result sendConnectNbr();

		// response tracking
		void expect(const std::string& cmd, std::function<void(const ServerMessage&)> callback);
		void processResponse(const ServerMessage& msg);
		void checkTimeouts(int timeoutMs);

		void cancelAll();
};