#pragma once

#include "CommandType.hpp"

#include <cstdint>
#include <string>

enum class CommandStatus {
	Success = 0,
	Timeout,
	ProtocolError,
	ServerError,
	NetworkError,
	Retrying
};

struct CommandResult {
	std::uint64_t	id = 0;
	CommandType		type = CommandType::Unknown;
	CommandStatus	status = CommandStatus::Success;
	std::string		details;
};