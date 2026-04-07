#pragma once

#include "app/command/CommandResult.hpp"

#include <cstdint>
#include <functional>
#include <string>

// Exposed event emitted when a command reaches completion.
struct CommandEvent {
	std::uint64_t	commandId = 0;
	CommandType		commandType = CommandType::Unknown;
	CommandStatus	status = CommandStatus::Success;
	std::string		details;

	bool isSuccess() const { return status == CommandStatus::Success; }
	bool isFailure() const { return !isSuccess(); }
	std::string statusName() const;
};

using CommandEventHandler = std::function<void(const CommandEvent&)>;
