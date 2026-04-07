#pragma once

#include "app/command/CommandRequest.hpp"
#include "app/command/CommandResult.hpp"
#include "app/command/CommandReplyMatcher.hpp"
#include "app/event/CommandEvent.hpp"
#include "result.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

class CommandManager {
	using DispatchFn = std::function<Result(const CommandRequest&)>;

	private:
		static constexpr std::size_t	MAX_PENDING_COMMANDS = 32;
		static constexpr std::int64_t	STALE_INFLIGHT_MS = 300000; // 5 minutes

	private:
		DispatchFn						_dispatch;
		CommandEventHandler				_eventHandler; // Optional observer for command events
		std::deque<CommandRequest>		_pending;
		std::optional<CommandRequest>	_inFlight;
		std::deque<CommandResult>		_completed;
		std::uint64_t					_nextId;

	private:
		Result	dispatchInFlight();
		void	startNextIfIdle();
		void	completeInFlight(CommandStatus status, const std::string& details);
		void	checkStaleFlight(std::int64_t nowMs); // Guard against dangling in-flight
		void	notifyCompletion(const CommandEvent& event); // Notify observers

	public:

		explicit CommandManager(DispatchFn dispatch);

		std::uint64_t	enqueue(CommandType type, std::int64_t nowMs, const std::string& arg = "");
		Result			tick(std::int64_t nowMs);
		bool			onServerTextFrame(const std::string& text);

		bool			hasInFlight() const;
		std::size_t		queuedCount() const;
		bool			isFull() const;
		bool			popCompleted(CommandResult& out);

		// Observer pattern: register callback for command completion events
		void			setEventHandler(CommandEventHandler handler) { _eventHandler = handler; }
};
