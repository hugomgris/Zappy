#include "gtest/gtest.h"
#include "app/CommandManager.hpp"

#include <vector>

TEST(CommandManagerFlowIntegrationTest, EnqueueDispatchResolveAndPopInOrder) {
	std::vector<CommandRequest> dispatched;
	CommandManager manager([&dispatched](const CommandRequest& req) {
		dispatched.push_back(req);
		return Result::success();
	});

	const std::int64_t t0 = 1000;
	const std::uint64_t voirId = manager.enqueue(CommandType::Voir, t0);
	const std::uint64_t invId = manager.enqueue(CommandType::Inventaire, t0 + 1);

	ASSERT_TRUE(manager.tick(t0).ok());
	ASSERT_EQ(dispatched.size(), 1u);
	EXPECT_EQ(dispatched[0].id, voirId);
	EXPECT_EQ(dispatched[0].type, CommandType::Voir);

	const bool voirConsumed = manager.onServerTextFrame("{\"type\":\"response\",\"cmd\":\"voir\",\"arg\":\"{player}\"}");
	ASSERT_TRUE(voirConsumed);
	ASSERT_EQ(dispatched.size(), 2u);
	EXPECT_EQ(dispatched[1].id, invId);
	EXPECT_EQ(dispatched[1].type, CommandType::Inventaire);

	const bool invConsumed = manager.onServerTextFrame("{\"type\":\"response\",\"cmd\":\"inventaire\",\"arg\":{\"nourriture\":4}}");
	ASSERT_TRUE(invConsumed);

	CommandResult first;
	ASSERT_TRUE(manager.popCompleted(first));
	EXPECT_EQ(first.id, voirId);
	EXPECT_EQ(first.type, CommandType::Voir);
	EXPECT_EQ(first.status, CommandStatus::Success);

	CommandResult second;
	ASSERT_TRUE(manager.popCompleted(second));
	EXPECT_EQ(second.id, invId);
	EXPECT_EQ(second.type, CommandType::Inventaire);
	EXPECT_EQ(second.status, CommandStatus::Success);

	CommandResult none;
	EXPECT_FALSE(manager.popCompleted(none));
}
