#include <gtest/gtest.h>

#include "app/ClientRunner.hpp"
#include "app/command/CommandRequest.hpp"
#include "app/policy/DecisionPolicy.hpp"

#include <optional>
#include <vector>

namespace {
	class BroadcastEchoPolicy : public DecisionPolicy {
		public:
			std::vector<std::shared_ptr<IntentRequest>> onTick(std::int64_t /*nowMs*/) override {
				return {};
			}

			std::vector<std::shared_ptr<IntentRequest>> onCommandEvent(
				std::int64_t /*nowMs*/,
				const CommandEvent& event,
				const std::optional<IntentResult>& intentResult
			) override {
				if (
					event.commandType == CommandType::Broadcast
					&& !intentResult.has_value()
					&& event.details == "team:need:food"
				) {
					return {std::make_shared<RequestBroadcast>("team:offer:food")};
				}

				return {};
			}
	};
}

TEST(ClientRunnerTeamMessageIntegrationTest, RoutesIncomingMessageFrameToPolicyAndDispatchesFollowUp) {
	Arguments args;
	std::vector<CommandRequest> dispatched;
	ClientRunner runner(args, [&dispatched](const CommandRequest& req) {
		dispatched.push_back(req);
		return Result::success();
	});

	auto policy = std::make_unique<BroadcastEchoPolicy>();
	runner.setDecisionPolicy(std::move(policy));

	ASSERT_TRUE(runner.processManagedTextFrameForTesting(
		"{\"type\":\"message\",\"status\":3,\"arg\":\"team:need:food\"}",
		100
	).ok());

	ASSERT_TRUE(runner.tickCommandLayerForTesting(101).ok());
	ASSERT_EQ(dispatched.size(), 1UL);
	EXPECT_EQ(dispatched[0].type, CommandType::Broadcast);
	EXPECT_EQ(dispatched[0].arg, "team:offer:food");
}
