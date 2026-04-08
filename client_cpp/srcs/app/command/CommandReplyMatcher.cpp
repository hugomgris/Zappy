#include "app/command/CommandReplyMatcher.hpp"

#include <cstring>
#include <cctype>

namespace {
	// Helper to extract quoted string value from JSON
	std::optional<std::string> extractQuotedValue(const std::string& text, const std::string& fieldName) {
		const std::string keyToken = "\"" + fieldName + "\"";
		size_t keyPos = text.find(keyToken);
		if (keyPos == std::string::npos) {
			return std::nullopt;
		}

		size_t colonPos = text.find(':', keyPos + keyToken.length());
		if (colonPos == std::string::npos) {
			return std::nullopt;
		}

		size_t valuePos = colonPos + 1;
		while (valuePos < text.size() && std::isspace(static_cast<unsigned char>(text[valuePos]))) {
			++valuePos;
		}

		if (valuePos >= text.size() || text[valuePos] != '"') {
			return std::nullopt;
		}

		++valuePos;
		size_t closePos = valuePos;
		while (closePos < text.size()) {
			if (text[closePos] == '"' && (closePos == valuePos || text[closePos - 1] != '\\')) {
				return text.substr(valuePos, closePos - valuePos);
			}
			++closePos;
		}

		return std::nullopt;
	}

	MatchResult matchReplyByCmd(const std::string& text, const std::string& expectedCmd) {
		auto cmd = extractQuotedValue(text, "cmd");
		if (!cmd) {
			return MatchResult(false, CommandStatus::MalformedReply,
				"Missing 'cmd' field in " + expectedCmd + " reply");
		}

		if (*cmd != expectedCmd) {
			return MatchResult(false, CommandStatus::UnexpectedReply,
				"Expected '" + expectedCmd + "' command, got '" + *cmd + "'");
		}

		return MatchResult(true, CommandStatus::Success, text);
	}
}

MatchResult CommandReplyMatcher::validateReply(CommandType expectedCmd, const std::string& text) {
	// Check for structural errors first
	if (!isValidJsonStructure(text)) {
		return MatchResult(false, CommandStatus::MalformedReply, "Invalid JSON structure");
	}

	// Check for error/ko frames (these match any command but complete as ServerError)
	if (isErrorFrame(text) || isKoFrame(text)) {
		return MatchResult(false, CommandStatus::ServerError, text);
	}

	// Type-specific matching
	switch (expectedCmd) {
		case CommandType::Login:
			return matchLoginReply(text);

		case CommandType::Voir:
			return matchVoirReply(text);

		case CommandType::Inventaire:
			return matchInventaireReply(text);

		case CommandType::Prend:
			return matchPrendReply(text);

		case CommandType::Pose:
			return matchPoseReply(text);

		case CommandType::Avance:
			return matchAvanceReply(text);

		case CommandType::Droite:
			return matchDroiteReply(text);

		case CommandType::Gauche:
			return matchGaucheReply(text);

		case CommandType::Expulse:
			return matchExpulseReply(text);

		case CommandType::Broadcast:
			return matchBroadcastReply(text);

		case CommandType::Incantation:
			return matchIncantationReply(text);

		case CommandType::Fork:
			return matchForkReply(text);

		case CommandType::ConnectNbr:
			return matchConnectNbrReply(text);

		default:
			return MatchResult(false, CommandStatus::UnexpectedReply, "Unknown command type");
	}
}

std::optional<std::string> CommandReplyMatcher::extractJsonField(const std::string& text, const std::string& fieldName) {
	return extractQuotedValue(text, fieldName);
}

bool CommandReplyMatcher::isValidJsonStructure(const std::string& text) {
	// Basic check: should at least look like JSON with braces
	if (text.empty()) {
		return false;
	}
	if (text.find('{') == std::string::npos || text.find('}') == std::string::npos) {
		return false;
	}
	return true;
}

bool CommandReplyMatcher::isErrorFrame(const std::string& text) {
	auto type = extractJsonField(text, "type");
	return type && *type == "error";
}

bool CommandReplyMatcher::isKoFrame(const std::string& text) {
	auto arg = extractJsonField(text, "arg");
	return arg && *arg == "ko";
}

MatchResult CommandReplyMatcher::matchLoginReply(const std::string& text) {
	// Login replies don't have a "cmd" field, just need valid JSON structure
	// Any non-error, validly-structured reply is acceptable for login
	if (isErrorFrame(text)) {
		return MatchResult(false, CommandStatus::ServerError, text);
	}
	// Login replies have different structure (e.g., {"type":"welcome"})
	// so just confirm it's valid JSON and not an error
	return MatchResult(true, CommandStatus::Success, text);
}

MatchResult CommandReplyMatcher::matchVoirReply(const std::string& text) {
	return matchReplyByCmd(text, "voir");
}

MatchResult CommandReplyMatcher::matchInventaireReply(const std::string& text) {
	return matchReplyByCmd(text, "inventaire");
}

MatchResult CommandReplyMatcher::matchPrendReply(const std::string& text) {
	auto cmd = extractJsonField(text, "cmd");
	if (!cmd) {
		return MatchResult(false, CommandStatus::MalformedReply, "Missing 'cmd' field in prend reply");
	}

	if (*cmd != "prend") {
		// This is a frame for a different command
		return MatchResult(false, CommandStatus::UnexpectedReply, "Expected 'prend' command, got '" + *cmd + "'");
	}

	// For prend, also check that arg is "ok" (not "ko")
	auto arg = extractJsonField(text, "arg");
	if (!arg) {
		return MatchResult(false, CommandStatus::MalformedReply, "Missing 'arg' field in prend reply");
	}

	if (*arg == "ko") {
		// This is a ko reply, will be caught by isKoFrame but double-check
		return MatchResult(false, CommandStatus::ServerError, text);
	}

	if (*arg != "ok") {
		return MatchResult(false, CommandStatus::UnexpectedReply, "Expected 'ok' in prend reply, got '" + *arg + "'");
	}

	return MatchResult(true, CommandStatus::Success, text);
}

MatchResult CommandReplyMatcher::matchPoseReply(const std::string& text) {
	auto cmd = extractJsonField(text, "cmd");
	if (!cmd) {
		return MatchResult(false, CommandStatus::MalformedReply, "Missing 'cmd' field in pose reply");
	}

	if (*cmd != "pose") {
		return MatchResult(false, CommandStatus::UnexpectedReply, "Expected 'pose' command, got '" + *cmd + "'");
	}

	auto arg = extractJsonField(text, "arg");
	if (!arg) {
		return MatchResult(false, CommandStatus::MalformedReply, "Missing 'arg' field in pose reply");
	}

	if (*arg == "ko") {
		return MatchResult(false, CommandStatus::ServerError, text);
	}

	if (*arg != "ok") {
		return MatchResult(false, CommandStatus::UnexpectedReply, "Expected 'ok' in pose reply, got '" + *arg + "'");
	}

	return MatchResult(true, CommandStatus::Success, text);
}

MatchResult CommandReplyMatcher::matchAvanceReply(const std::string& text) {
	return matchReplyByCmd(text, "avance");
}

MatchResult CommandReplyMatcher::matchDroiteReply(const std::string& text) {
	return matchReplyByCmd(text, "droite");
}

MatchResult CommandReplyMatcher::matchGaucheReply(const std::string& text) {
	return matchReplyByCmd(text, "gauche");
}

MatchResult CommandReplyMatcher::matchExpulseReply(const std::string& text) {
	return matchReplyByCmd(text, "expulse");
}

MatchResult CommandReplyMatcher::matchBroadcastReply(const std::string& text) {
	return matchReplyByCmd(text, "broadcast");
}

MatchResult CommandReplyMatcher::matchIncantationReply(const std::string& text) {
	return matchReplyByCmd(text, "incantation");
}

MatchResult CommandReplyMatcher::matchForkReply(const std::string& text) {
	return matchReplyByCmd(text, "fork");
}

MatchResult CommandReplyMatcher::matchConnectNbrReply(const std::string& text) {
	return matchReplyByCmd(text, "connect_nbr");
}
