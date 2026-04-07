#include "gtest/gtest.h"
#include "app/CommandManager.hpp"

#include <vector>

TEST(CommandManagerTest, TickDispatchesQueuedCommandWhenIdle) {
	std::vector<CommandRequest> dispatched;
	CommandManager mgr([&dispatched](const CommandRequest& req) {
		dispatched.push_back(req);
		return Result::success();
	});

	mgr.enqueue(CommandType::Voir, 1000);
	const Result tickRes = mgr.tick(1000);

	ASSERT_TRUE(tickRes.ok());
	ASSERT_EQ(dispatched.size(), 1u);
	EXPECT_EQ(dispatched[0].type, CommandType::Voir);
	EXPECT_TRUE(mgr.hasInFlight());
}

TEST(CommandManagerTest, TimeoutRetriesThenCompletesAsTimeout) {
	std::vector<CommandRequest> dispatched;
	CommandManager mgr([&dispatched](const CommandRequest& req) {
		dispatched.push_back(req);
		return Result::success();
	});

	mgr.enqueue(CommandType::Prend, 1000, "nourriture");
	ASSERT_TRUE(mgr.tick(1000).ok());
	ASSERT_EQ(dispatched.size(), 1u);

	ASSERT_TRUE(mgr.tick(5000).ok());
	ASSERT_EQ(dispatched.size(), 2u);
	ASSERT_TRUE(mgr.hasInFlight());

	ASSERT_TRUE(mgr.tick(9000).ok());
	ASSERT_EQ(dispatched.size(), 3u);
	ASSERT_TRUE(mgr.hasInFlight());

	ASSERT_TRUE(mgr.tick(13000).ok());
	EXPECT_FALSE(mgr.hasInFlight());

	CommandResult completed;
	ASSERT_TRUE(mgr.popCompleted(completed));
	EXPECT_EQ(completed.type, CommandType::Prend);
	EXPECT_EQ(completed.status, CommandStatus::Timeout);
}

TEST(CommandManagerTest, MatchingReplyCompletesCommandAsSuccess) {
	CommandManager mgr([](const CommandRequest&) {
		return Result::success();
	});

	mgr.enqueue(CommandType::Inventaire, 1000);
	ASSERT_TRUE(mgr.tick(1000).ok());

	const bool consumed = mgr.onServerTextFrame("{\"type\":\"response\",\"cmd\":\"inventaire\",\"arg\":{\"nourriture\":5}}");
	ASSERT_TRUE(consumed);
	EXPECT_FALSE(mgr.hasInFlight());

	CommandResult completed;
	ASSERT_TRUE(mgr.popCompleted(completed));
	EXPECT_EQ(completed.type, CommandType::Inventaire);
	EXPECT_EQ(completed.status, CommandStatus::Success);
}

TEST(CommandManagerTest, KoReplyCompletesCommandAsServerError) {
	CommandManager mgr([](const CommandRequest&) {
		return Result::success();
	});

	mgr.enqueue(CommandType::Prend, 1000, "nourriture");
	ASSERT_TRUE(mgr.tick(1000).ok());

	const bool consumed = mgr.onServerTextFrame("{\"type\":\"response\",\"cmd\":\"prend\",\"arg\":\"ko\"}");
	ASSERT_TRUE(consumed);
	EXPECT_FALSE(mgr.hasInFlight());

	CommandResult completed;
	ASSERT_TRUE(mgr.popCompleted(completed));
	EXPECT_EQ(completed.type, CommandType::Prend);
	EXPECT_EQ(completed.status, CommandStatus::ServerError);
}
