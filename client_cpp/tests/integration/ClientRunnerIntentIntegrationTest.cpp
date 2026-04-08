#include <gtest/gtest.h>

#include "app/ClientRunner.hpp"
#include "app/command/CommandRequest.hpp"

#include <vector>

TEST(ClientRunnerIntentIntegrationTest, SubmitIntentToCompletionAcrossMultipleCommandFamilies) {
	Arguments args;
	std::vector<CommandRequest> dispatched;
	ClientRunner runner(args, [&dispatched](const CommandRequest& req) {
		dispatched.push_back(req);
		return Result::success();
	});

	const std::uint64_t moveId = runner.submitIntent(std::make_shared<RequestMove>());
	const std::uint64_t turnId = runner.submitIntent(std::make_shared<RequestTurnRight>());
	const std::uint64_t broadcastId = runner.submitIntent(std::make_shared<RequestBroadcast>("hi"));

	ASSERT_NE(moveId, 0UL);
	ASSERT_NE(turnId, 0UL);
	ASSERT_NE(broadcastId, 0UL);

	ASSERT_TRUE(runner.tickCommandLayerForTesting(10).ok());
	ASSERT_TRUE(runner.processManagedTextFrameForTesting("{\"type\":\"response\",\"cmd\":\"avance\",\"arg\":\"ok\"}", 10).ok());

	ASSERT_TRUE(runner.tickCommandLayerForTesting(20).ok());
	ASSERT_TRUE(runner.processManagedTextFrameForTesting("{\"type\":\"response\",\"cmd\":\"droite\",\"arg\":\"ok\"}", 20).ok());

	ASSERT_TRUE(runner.tickCommandLayerForTesting(30).ok());
	ASSERT_TRUE(runner.processManagedTextFrameForTesting("{\"type\":\"response\",\"cmd\":\"broadcast\",\"arg\":\"ok\"}", 30).ok());

	ASSERT_EQ(dispatched.size(), 3UL);
	EXPECT_EQ(dispatched[0].type, CommandType::Avance);
	EXPECT_EQ(dispatched[1].type, CommandType::Droite);
	EXPECT_EQ(dispatched[2].type, CommandType::Broadcast);
	EXPECT_EQ(dispatched[2].arg, "hi");

	IntentResult first;
	IntentResult second;
	IntentResult third;
	ASSERT_TRUE(runner.popCompletedIntent(first));
	ASSERT_TRUE(runner.popCompletedIntent(second));
	ASSERT_TRUE(runner.popCompletedIntent(third));

	EXPECT_EQ(first.id, moveId);
	EXPECT_EQ(first.intentType, "RequestMove");
	EXPECT_TRUE(first.succeeded);

	EXPECT_EQ(second.id, turnId);
	EXPECT_EQ(second.intentType, "RequestTurnRight");
	EXPECT_TRUE(second.succeeded);

	EXPECT_EQ(third.id, broadcastId);
	EXPECT_EQ(third.intentType, "RequestBroadcast(\"hi\")");
	EXPECT_TRUE(third.succeeded);
}

TEST(ClientRunnerIntentIntegrationTest, IntentCompletionHandlerReceivesCorrelatedResults) {
	Arguments args;
	std::vector<IntentResult> completed;
	ClientRunner runner(args, [](const CommandRequest& /*req*/) {
		return Result::success();
	});
	runner.setIntentCompletionHandler([&completed](const IntentResult& result) {
		completed.push_back(result);
	});

	const std::uint64_t id = runner.submitIntent(std::make_shared<RequestPlace>(ResourceType::Sibur));
	ASSERT_NE(id, 0UL);

	ASSERT_TRUE(runner.tickCommandLayerForTesting(1).ok());
	ASSERT_TRUE(runner.processManagedTextFrameForTesting("{\"type\":\"response\",\"cmd\":\"pose\",\"arg\":\"ok\"}", 1).ok());

	ASSERT_EQ(completed.size(), 1UL);
	EXPECT_EQ(completed[0].id, id);
	EXPECT_EQ(completed[0].intentType, "RequestPlace(sibur)");
	EXPECT_TRUE(completed[0].succeeded);
}

TEST(ClientRunnerIntentIntegrationTest, SubmitIncantationIntentToCompletion) {
	Arguments args;
	std::vector<CommandRequest> dispatched;
	ClientRunner runner(args, [&dispatched](const CommandRequest& req) {
		dispatched.push_back(req);
		return Result::success();
	});

	const std::uint64_t incantId = runner.submitIntent(std::make_shared<RequestIncantation>());
	ASSERT_NE(incantId, 0UL);

	ASSERT_TRUE(runner.tickCommandLayerForTesting(10).ok());
	ASSERT_TRUE(runner.processManagedTextFrameForTesting("{\"type\":\"response\",\"cmd\":\"incantation\",\"arg\":\"ok\"}", 10).ok());

	ASSERT_EQ(dispatched.size(), 1UL);
	EXPECT_EQ(dispatched[0].type, CommandType::Incantation);

	IntentResult done;
	ASSERT_TRUE(runner.popCompletedIntent(done));
	EXPECT_EQ(done.id, incantId);
	EXPECT_EQ(done.intentType, "RequestIncantation");
	EXPECT_TRUE(done.succeeded);
}

TEST(ClientRunnerIntentIntegrationTest, RejectsQueueSaturationAfterMaxPendingCommands) {
	Arguments args;
	ClientRunner runner(args, [](const CommandRequest& /*req*/) {
		return Result::success();
	});

	std::uint64_t lastAcceptedId = 0;
	for (int index = 0; index < 32; ++index) {
		lastAcceptedId = runner.submitIntent(std::make_shared<RequestMove>());
		EXPECT_NE(lastAcceptedId, 0UL);
	}

	const std::uint64_t rejectedId = runner.submitIntent(std::make_shared<RequestMove>());
	EXPECT_EQ(rejectedId, 0UL);
}

TEST(ClientRunnerIntentIntegrationTest, SustainsHundredCommandCyclesWithoutCorruption) {
	Arguments args;
	std::vector<CommandRequest> dispatched;
	ClientRunner runner(args, [&dispatched](const CommandRequest& req) {
		dispatched.push_back(req);
		return Result::success();
	});

	std::vector<std::uint64_t> acceptedIds;
	acceptedIds.reserve(100);
	for (int cycle = 0; cycle < 100; ++cycle) {
		std::shared_ptr<IntentRequest> intent;
		if (cycle % 2 == 0) {
			intent = std::make_shared<RequestMove>();
		} else {
			intent = std::make_shared<RequestInventaire>();
		}
		const std::uint64_t id = runner.submitIntent(intent);
		ASSERT_NE(id, 0UL);
		acceptedIds.push_back(id);

		ASSERT_TRUE(runner.tickCommandLayerForTesting(1000 + cycle).ok());

		const std::string reply = (cycle % 2 == 0)
			? "{\"type\":\"response\",\"cmd\":\"avance\",\"arg\":\"ok\"}"
			: "{\"type\":\"response\",\"cmd\":\"inventaire\",\"arg\":[]}";
		ASSERT_TRUE(runner.processManagedTextFrameForTesting(reply, 1000 + cycle).ok());
	}

	EXPECT_EQ(dispatched.size(), 100UL);

	IntentResult completed;
	std::size_t completedCount = 0;
	while (runner.popCompletedIntent(completed)) {
		ASSERT_TRUE(completed.succeeded);
		EXPECT_EQ(completed.id, acceptedIds[completedCount]);
		++completedCount;
	}

	EXPECT_EQ(completedCount, 100UL);
	EXPECT_EQ(completedCount, dispatched.size());
}
