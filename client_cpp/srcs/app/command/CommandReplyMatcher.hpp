#pragma once

#include "CommandType.hpp"
#include "app/command/CommandResult.hpp"

#include <string>
#include <optional>

struct MatchResult {
	bool			isMatch;
	CommandStatus	status;
	std::string		details;

	MatchResult() : isMatch(false), status(CommandStatus::Success) {}
	MatchResult(bool match, CommandStatus st, const std::string& det)
		: isMatch(match), status(st), details(det) {}
};

class CommandReplyMatcher {
	public:
		CommandReplyMatcher() = delete;

		// Validate if a text frame matches the expected in-flight command
		// Returns detailed status and match result
		static MatchResult validateReply(CommandType expectedCmd, const std::string& text);

	private:
		// Helper to extract JSON field value (handles both spacing variants)
		static std::optional<std::string> extractJsonField(const std::string& text, const std::string& fieldName);

		// Check if JSON is structurally valid (contains expected field syntax)
		static bool isValidJsonStructure(const std::string& text);

		// Check for error/ko frames
		static bool isErrorFrame(const std::string& text);
		static bool isKoFrame(const std::string& text);

		// Type-specific matchers
		static MatchResult matchLoginReply(const std::string& text);
		static MatchResult matchVoirReply(const std::string& text);
		static MatchResult matchInventaireReply(const std::string& text);
		static MatchResult matchPrendReply(const std::string& text);
		static MatchResult matchPoseReply(const std::string& text);
		static MatchResult matchAvanceReply(const std::string& text);
		static MatchResult matchDroiteReply(const std::string& text);
		static MatchResult matchGaucheReply(const std::string& text);
		static MatchResult matchExpulseReply(const std::string& text);
		static MatchResult matchBroadcastReply(const std::string& text);
		static MatchResult matchIncantationReply(const std::string& text);
		static MatchResult matchForkReply(const std::string& text);
		static MatchResult matchConnectNbrReply(const std::string& text);
};
