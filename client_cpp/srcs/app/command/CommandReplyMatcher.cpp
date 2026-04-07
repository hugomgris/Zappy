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
	auto cmd = extractJsonField(text, "cmd");
	if (!cmd) {
		return MatchResult(false, CommandStatus::MalformedReply, "Missing 'cmd' field in voir reply");
	}

	if (*cmd != "voir") {
		// This is a frame for a different command
		return MatchResult(false, CommandStatus::UnexpectedReply, "Expected 'voir' command, got '" + *cmd + "'");
	}

	return MatchResult(true, CommandStatus::Success, text);
}

MatchResult CommandReplyMatcher::matchInventaireReply(const std::string& text) {
	auto cmd = extractJsonField(text, "cmd");
	if (!cmd) {
		return MatchResult(false, CommandStatus::MalformedReply, "Missing 'cmd' field in inventaire reply");
	}

	if (*cmd != "inventaire") {
		// This is a frame for a different command
		return MatchResult(false, CommandStatus::UnexpectedReply, "Expected 'inventaire' command, got '" + *cmd + "'");
	}

	return MatchResult(true, CommandStatus::Success, text);
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
