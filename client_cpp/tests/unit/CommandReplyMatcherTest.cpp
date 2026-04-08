#include <gtest/gtest.h>

#include "app/command/CommandReplyMatcher.hpp"

class CommandReplyMatcherTest : public ::testing::Test {
	protected:
		CommandReplyMatcherTest() = default;
};

// ============================================================================
// LOGIN TESTS
// ============================================================================

TEST_F(CommandReplyMatcherTest, LoginAcceptsValidReply) {
	const std::string frame = R"({"type":"welcome"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Login, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, LoginRejectsErrorFrame) {
	const std::string frame = R"({"type":"error","message":"invalid"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Login, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::ServerError);
}

TEST_F(CommandReplyMatcherTest, LoginRejectsMalformedFrame) {
	const std::string frame = R"(invalid json)";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Login, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::MalformedReply);
}

// ============================================================================
// VOIR TESTS
// ============================================================================

TEST_F(CommandReplyMatcherTest, VoirAcceptsValidReply) {
	const std::string frame = R"({"cmd":"voir","arg":"data"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, VoirAcceptsValidReplyWithSpacing) {
	const std::string frame = R"({"cmd": "voir", "arg": "data"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, VoirRejectsWrongCommandType) {
	const std::string frame = R"({"cmd":"inventaire","arg":"data"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
	EXPECT_TRUE(result.details.find("Expected 'voir'") != std::string::npos);
}

TEST_F(CommandReplyMatcherTest, VoirRejectsMissingCmdField) {
	const std::string frame = R"({"arg":"data"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::MalformedReply);
	EXPECT_TRUE(result.details.find("Missing") != std::string::npos);
}

TEST_F(CommandReplyMatcherTest, VoirRejectsMalformedJson) {
	const std::string frame = R"({"cmd":"voir")";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::MalformedReply);
}

TEST_F(CommandReplyMatcherTest, VoirRejectsErrorFrame) {
	const std::string frame = R"({"type":"error","message":"failed"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::ServerError);
}

// ============================================================================
// INVENTAIRE TESTS
// ============================================================================

TEST_F(CommandReplyMatcherTest, InventaireAcceptsValidReply) {
	const std::string frame = R"({"cmd":"inventaire","arg":[["nourriture",10]]})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Inventaire, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, InventaireAcceptsValidReplyWithSpacing) {
	const std::string frame = R"({"cmd": "inventaire", "arg": []})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Inventaire, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, InventaireRejectsWrongCommandType) {
	const std::string frame = R"({"cmd":"prend","arg":"ok"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Inventaire, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
}

TEST_F(CommandReplyMatcherTest, InventaireRejectsMissingCmdField) {
	const std::string frame = R"({"arg":[]})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Inventaire, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::MalformedReply);
}

TEST_F(CommandReplyMatcherTest, InventaireRejectsErrorFrame) {
	const std::string frame = R"({"type":"error"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Inventaire, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::ServerError);
}

// ============================================================================
// PREND TESTS
// ============================================================================

TEST_F(CommandReplyMatcherTest, PrendAcceptsOkReply) {
	const std::string frame = R"({"cmd":"prend","arg":"ok"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, PrendAcceptsOkReplyWithSpacing) {
	const std::string frame = R"({"cmd": "prend", "arg": "ok"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, PrendAcceptsPrettyPrintedReplyWithVariableWhitespace) {
	const std::string frame =
		"{\n"
		"\t\"type\": \"response\",\n"
		"\t\"cmd\":  \"prend\",\n"
		"\t\"arg\":\t\"ok\",\n"
		"\t\"status\":\t\"ok\"\n"
		"}";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, PrendAcceptsStatusOkWithResourceArg) {
	const std::string frame = R"({"type":"response","cmd":"prend","arg":"nourriture","status":"ok"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, PrendRejectsStatusKoWithResourceArg) {
	const std::string frame = R"({"type":"response","cmd":"prend","arg":"nourriture","status":"ko"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::ServerError);
}

TEST_F(CommandReplyMatcherTest, PrendRejectsKoReply) {
	const std::string frame = R"({"cmd":"prend","arg":"ko"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::ServerError);
}

TEST_F(CommandReplyMatcherTest, PrendRejectsWrongArg) {
	const std::string frame = R"({"cmd":"prend","arg":"maybe"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
}

TEST_F(CommandReplyMatcherTest, PrendRejectsWrongCommandType) {
	const std::string frame = R"({"cmd":"voir","arg":"data"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
}

TEST_F(CommandReplyMatcherTest, PrendRejectsMissingCmdField) {
	const std::string frame = R"({"arg":"ok"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::MalformedReply);
}

TEST_F(CommandReplyMatcherTest, PrendRejectsMissingArgField) {
	const std::string frame = R"({"cmd":"prend"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::MalformedReply);
}

TEST_F(CommandReplyMatcherTest, PrendRejectsErrorFrame) {
	const std::string frame = R"({"type":"error","message":"forbidden"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::ServerError);
}

// ============================================================================
// ADDITIONAL COMMAND TESTS
// ============================================================================

TEST_F(CommandReplyMatcherTest, AvanceAcceptsMatchingReply) {
	const std::string frame = R"({"type":"response","cmd":"avance","arg":"ok"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Avance, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, BroadcastRejectsWrongCommandReply) {
	const std::string frame = R"({"type":"response","cmd":"expulse","arg":"ok"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Broadcast, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
}

TEST_F(CommandReplyMatcherTest, BroadcastAcceptsMessageStyleReply) {
	const std::string frame = R"({"type":"message","arg":"team:role:gatherer","status":0})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Broadcast, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, BroadcastRejectsMessageStyleReplyWithoutArg) {
	const std::string frame = R"({"type":"message","status":0})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Broadcast, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::MalformedReply);
}

TEST_F(CommandReplyMatcherTest, VoirTreatsMessageStyleFrameAsUnexpectedReply) {
	const std::string frame = R"({"type":"message","arg":"team:role:gatherer","status":0})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
}

TEST_F(CommandReplyMatcherTest, PrendTreatsMessageStyleFrameAsUnexpectedReply) {
	const std::string frame = R"({"type":"message","arg":"team:role:gatherer","status":0})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
}

TEST_F(CommandReplyMatcherTest, PoseAcceptsOkReply) {
	const std::string frame = R"({"cmd":"pose","arg":"ok"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Pose, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, PoseRejectsNonOkReply) {
	const std::string frame = R"({"cmd":"pose","arg":"invalid"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Pose, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
}

// ============================================================================
// CROSS-COMMAND REJECTION TESTS
// ============================================================================

TEST_F(CommandReplyMatcherTest, DontMatchVoirFrameForInventaireCommand) {
	const std::string frame = R"({"cmd":"voir","arg":"data"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Inventaire, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
}

TEST_F(CommandReplyMatcherTest, DontMatchPrendFrameForVoirCommand) {
	const std::string frame = R"({"cmd":"prend","arg":"ok"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
}

TEST_F(CommandReplyMatcherTest, DontMatchInventaireFrameForPrendCommand) {
	const std::string frame = R"({"cmd":"inventaire","arg":[]})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST_F(CommandReplyMatcherTest, EmptyFrameRejected) {
	const std::string frame = "";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::MalformedReply);
}

TEST_F(CommandReplyMatcherTest, NoJsonBracesRejected) {
	const std::string frame = "just some text";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Login, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::MalformedReply);
}

TEST_F(CommandReplyMatcherTest, PartialJsonRejected) {
	const std::string frame = R"({"cmd":"voir")";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::MalformedReply);
}

TEST_F(CommandReplyMatcherTest, ExtraFieldsIgnored) {
	const std::string frame = R"({"cmd":"voir","arg":"data","extra":"field","another":123})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, FieldsOutOfOrderAccepted) {
	const std::string frame = R"({"arg":"ok","cmd":"prend"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Prend, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, MixedSpacingAccepted) {
	const std::string frame = R"({"cmd": "voir","arg":"data"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_TRUE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::Success);
}

TEST_F(CommandReplyMatcherTest, ErrorFrameWithExtraWhitespaceClassifiedAsServerError) {
	const std::string frame =
		"{\n"
		"  \"type\"  :   \"error\",\n"
		"  \"message\":  \"invalid\"\n"
		"}";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Voir, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::ServerError);
}

TEST_F(CommandReplyMatcherTest, UnknownCommandTypeRejected) {
	const std::string frame = R"({"cmd":"unknown"})";
	MatchResult result = CommandReplyMatcher::validateReply(CommandType::Unknown, frame);
	EXPECT_FALSE(result.isMatch);
	EXPECT_EQ(result.status, CommandStatus::UnexpectedReply);
}
