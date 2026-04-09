#pragma once

#include "DataStructs.hpp"
#include "app/command/CommandType.hpp"
#include "app/intent/Intent.hpp"
#include "result.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

class WebsocketClient;
class CommandSender;
class CommandManager;
struct CommandRequest;
class IntentRequest;
class DecisionPolicy;

class ClientRunner {
	private:
		Arguments							_args;
		std::unique_ptr<WebsocketClient>	_ws;
		std::unique_ptr<CommandSender>		_commands;
		std::unique_ptr<CommandManager>		_manager;
		std::unique_ptr<DecisionPolicy>		_policy;
		IntentCompletionHandler				_intentCompletionHandler;
		std::unordered_map<std::uint64_t, std::string>	_intentTypeByCommandId;
		std::deque<IntentResult>			_completedIntents;
		bool								_sawDieEvent;
		std::int64_t						_lastCmdLayerTraceAtMs;
	
	private:
		static int64_t nowMs();
		std::uint64_t submitIntentAt(const IntentRequest& intent, std::int64_t nowMs);

		Result	tickUntilConnected(int timeoutMs);
		Result	tickUntilTextFrame(int timeoutMs, std::string& outText);
		Result	tickForDuration(int durationMs);
		Result	runTransportFlow();
		Result	runPersistentLoop(int intervalMs);
		Result	tickCommandLayer(int64_t nowMs);
		Result	processManagedTextFrame(const std::string& text, int64_t nowMs);
		Result	waitForManagedCommandCompletion(CommandType expectedType, int timeoutMs, std::string& outDetails);
		Result	handleLoopTextFrame(const std::string& text, int64_t nowMs);
		Result	dispatchManagedCommand(const CommandRequest& req);
		Result	handleCompletedCommands(int64_t nowMs);
		bool	isDieEventFrame(const std::string& text) const;

	public:
		explicit ClientRunner(const Arguments& args);
		ClientRunner(const Arguments& args, std::function<Result(const CommandRequest&)> testDispatch);
		~ClientRunner();

		void setDecisionPolicy(std::unique_ptr<DecisionPolicy> policy);
		void setIntentCompletionHandler(IntentCompletionHandler handler);
		std::uint64_t submitIntent(const std::shared_ptr<IntentRequest>& intent);
		bool popCompletedIntent(IntentResult& out);
		Result tickCommandLayerForTesting(int64_t nowMs);
		Result processManagedTextFrameForTesting(const std::string& text, int64_t nowMs);
		Result run();
};
