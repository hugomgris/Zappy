#pragma once

#include "DataStructs.hpp"
#include "result.hpp"

#include <memory>
#include <string>

class WebsocketClient;
class CommandSender;

class ClientRunner {
	private:
		Arguments							_args;
		std::unique_ptr<WebsocketClient>	_ws;
		std::unique_ptr<CommandSender>		_commands;
		bool								_sawDieEvent;
		int64_t								_nextVoirAt;
		int64_t								_nextInventoryAt;
		int64_t								_nextFoodPickupEarliestAt;
	
	private:
		static int64_t nowMs();

		Result	tickUntilConnected(int timeoutMs);
		Result	tickUntilTextFrame(int timeoutMs, std::string& outText);
		Result	tickForDuration(int durationMs);
		Result	runTransportFlow();
		Result	runPersistentLoop(int intervalMs);
		Result	handleLoopTextFrame(const std::string& text, int64_t nowMs);
		
		bool	containsJsonType(const std::string& payload, const std::string& typeValue) const;
		
		int		tryParseNourritureCount(const std::string& text) const;

	public:
		explicit ClientRunner(const Arguments& args);
		~ClientRunner();

		Result run();
};
