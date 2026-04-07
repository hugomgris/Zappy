#pragma once

#include "DataStructs.hpp"
#include "app/command/CommandType.hpp"
#include "result.hpp"

#include <memory>
#include <string>

class WebsocketClient;
class CommandSender;
class CommandManager;
struct CommandRequest;

class ClientRunner {
	private:
		Arguments							_args;
		std::unique_ptr<WebsocketClient>	_ws;
		std::unique_ptr<CommandSender>		_commands;
		std::unique_ptr<CommandManager>		_manager;
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
		Result	waitForManagedCommandCompletion(CommandType expectedType, int timeoutMs, std::string& outDetails);
		Result	handleLoopTextFrame(const std::string& text, int64_t nowMs);
		Result	dispatchManagedCommand(const CommandRequest& req);
		Result	handleCompletedCommands(int64_t nowMs);
		bool	isDieEventFrame(const std::string& text) const;
		
		bool	containsJsonType(const std::string& payload, const std::string& typeValue) const;
		
		int		tryParseNourritureCount(const std::string& text) const;

	public:
		explicit ClientRunner(const Arguments& args);
		~ClientRunner();

		Result run();
};
