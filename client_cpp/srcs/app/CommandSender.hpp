#pragma once

#include "DataStructs.hpp"
#include "app/command/ResourceType.hpp"
#include "result.hpp"

#include <string>

class WebsocketClient;

class CommandSender {
	private:
		WebsocketClient& _ws;

	private:
		Result sendCommand(const std::string& cmd, const std::string& arg = "");
		Result sendRawJson(const std::string& payload, const std::string& context);
	
	public:
		explicit CommandSender(WebsocketClient& ws);

		Result sendLogin(const Arguments& args);
		Result sendVoir();
		Result sendInventaire();
		Result sendPrend(ResourceType resource);
		Result sendPrendNourriture();
};
