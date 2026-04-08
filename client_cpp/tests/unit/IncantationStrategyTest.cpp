#include <gtest/gtest.h>

#include "app/policy/IncantationStrategy.hpp"

TEST(IncantationStrategyTest, ReturnsNoneWhenFoodIsTooLow) {
	WorldState state;
	state.recordInventory(10, R"({"type":"response","cmd":"inventaire","arg":{"nourriture":4,"linemate":1,"deraumere":1,"sibur":1}})");
	state.recordVision(10, R"([["player"]])");

	IncantationStrategy strategy;
	EXPECT_EQ(strategy.decide(state), IncantationAction::None);
}

TEST(IncantationStrategyTest, ReturnsSummonWhenResourcesReadyButNotEnoughPlayers) {
	WorldState state;
	state.recordInventory(10, R"({"type":"response","cmd":"inventaire","arg":{"nourriture":15,"linemate":1,"deraumere":1,"sibur":1}})");
	state.recordVision(10, R"([[]])");

	IncantationStrategy strategy(10, 1);
	EXPECT_EQ(strategy.decide(state), IncantationAction::Summon);
}

TEST(IncantationStrategyTest, ReturnsIncantateWhenAllRequirementsAreMet) {
	WorldState state;
	state.recordInventory(10, R"({"type":"response","cmd":"inventaire","arg":{"nourriture":15,"linemate":1,"deraumere":1,"sibur":1}})");
	state.recordVision(10, R"([["player"]])");

	IncantationStrategy strategy(10, 1);
	EXPECT_EQ(strategy.decide(state), IncantationAction::Incantate);
}
