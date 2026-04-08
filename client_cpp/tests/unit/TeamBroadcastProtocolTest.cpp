#include <gtest/gtest.h>

#include "app/policy/TeamBroadcastProtocol.hpp"

TEST(TeamBroadcastProtocolTest, ParsesLegacySummonMessageAsNeedPlayers) {
	const TeamSignal signal = TeamBroadcastProtocol::parse("need_players_for_incantation");
	EXPECT_EQ(signal.kind, TeamSignalKind::NeedPlayers);
}

TEST(TeamBroadcastProtocolTest, ParsesRoleMessages) {
	EXPECT_EQ(TeamBroadcastProtocol::parse("team:role:scout").kind, TeamSignalKind::RoleScout);
	EXPECT_EQ(TeamBroadcastProtocol::parse("team:role:gatherer").kind, TeamSignalKind::RoleGatherer);
	EXPECT_EQ(TeamBroadcastProtocol::parse("team:role:assembler").kind, TeamSignalKind::RoleAssembler);
}

TEST(TeamBroadcastProtocolTest, LeavesUnknownMessagesAsUnknown) {
	const TeamSignal signal = TeamBroadcastProtocol::parse("hello from teammate");
	EXPECT_EQ(signal.kind, TeamSignalKind::Unknown);
}
