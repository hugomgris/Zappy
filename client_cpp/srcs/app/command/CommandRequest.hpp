#pragma once

#include "CommandSpec.hpp"
#include "CommandType.hpp"

#include <cstdint>
#include <string>

struct CommandRequest {
	std::uint64_t	id = 0;
	CommandType		type = CommandType::Unknown;
	std::string		arg;
	CommandSpec		spec{};
	std::int64_t	enqueuedAtMs = 0;
	std::int64_t	deadlineAtMs = 0;
	int				retryCount = 0;

	static CommandRequest make(std::uint64_t id, CommandType type, std::int64_t nowMs, const std::string& arg = "") {
		CommandRequest req;
		req.id = id;
		req.type = type;
		req.arg = arg;
		req.spec = CommandSpec::forType(type);
		req.enqueuedAtMs = nowMs;
		req.deadlineAtMs = nowMs + req.spec.timeoutMs;
		return req;
	}
};