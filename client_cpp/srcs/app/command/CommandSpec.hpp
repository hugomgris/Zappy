#pragma once

#include "CommandType.hpp"

struct CommandSpec {
	int		timeoutMs = 3000;
	int		maxRetries = 0;
	bool	expectsReply = true;

	static CommandSpec forType(CommandType type) {
		switch (type) {
			case CommandType::Login: return {4000, 1, true};
			case CommandType::Voir: return {8000, 1, true};
			case CommandType::Inventaire: return {4000, 1, true};
			case CommandType::PrendNourriture: return {4000, 2, true};
			default: return {3000, 0, true};
		}
	}
};