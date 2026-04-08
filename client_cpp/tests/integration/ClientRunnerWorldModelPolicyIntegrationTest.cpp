#include <gtest/gtest.h>

#include "app/ClientRunner.hpp"
#include "app/command/CommandRequest.hpp"
#include "app/policy/WorldModelPolicy.hpp"

#include <vector>

TEST(ClientRunnerWorldModelPolicyIntegrationTest, RunnerUpdatesWorldModelStateFromEvents) {
	Arguments args;
	std::vector<CommandRequest> dispatched;
	ClientRunner runner(args, [&dispatched](const CommandRequest& req) {
		dispatched.push_back(req);
		return Result::success();
	});

	auto policy = std::make_unique<WorldModelPolicy>(5000, 7000);
	WorldModelPolicy* policyPtr = policy.get();
	runner.setDecisionPolicy(std::move(policy));

	ASSERT_TRUE(runner.tickCommandLayerForTesting(100).ok());
	ASSERT_GE(dispatched.size(), 1UL);
	EXPECT_EQ(dispatched[0].type, CommandType::Voir);

	ASSERT_TRUE(runner.processManagedTextFrameForTesting("{\"type\":\"response\",\"cmd\":\"voir\",\"arg\":\"[{\\\"tile\\\":0}]\"}", 100).ok());
	EXPECT_TRUE(policyPtr->state().hasVision());
	EXPECT_EQ(policyPtr->state().lastVisionPayload(), "{\"type\":\"response\",\"cmd\":\"voir\",\"arg\":\"[{\\\"tile\\\":0}]\"}");

	ASSERT_TRUE(runner.tickCommandLayerForTesting(200).ok());
	ASSERT_GE(dispatched.size(), 2UL);
	EXPECT_EQ(dispatched[1].type, CommandType::Inventaire);

	ASSERT_TRUE(runner.processManagedTextFrameForTesting("{\"type\":\"response\",\"cmd\":\"inventaire\",\"arg\":{\"nourriture\":8,\"linemate\":1}}", 200).ok());
	EXPECT_TRUE(policyPtr->state().hasInventory());
	EXPECT_EQ(policyPtr->state().inventoryCount(ResourceType::Nourriture), 8);
	EXPECT_EQ(policyPtr->state().inventoryCount(ResourceType::Linemate), 1);

	auto freshIntents = policyPtr->onTick(2500);
	ASSERT_EQ(freshIntents.size(), 2UL);
	EXPECT_EQ(freshIntents[0]->description(), "RequestTurnLeft");
	EXPECT_EQ(freshIntents[1]->description(), "RequestMove");
}
