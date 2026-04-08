#include <gtest/gtest.h>

#include "app/policy/ResourceStrategy.hpp"

TEST(ResourceStrategyTest, PrioritizesOnlyFoodInEmergency) {
	WorldState state;
	state.recordInventory(10, R"({"type":"response","cmd":"inventaire","arg":{"nourriture":2,"linemate":0,"deraumere":0,"sibur":0}})");

	ResourceStrategy strategy;
	const std::vector<ResourceType> priority = strategy.buildPriority(state);

	ASSERT_EQ(priority.size(), 1UL);
	EXPECT_EQ(priority[0], ResourceType::Nourriture);
}

TEST(ResourceStrategyTest, PrioritizesFoodThenStoneDeficitsWhenStable) {
	WorldState state;
	state.recordInventory(10, R"({"type":"response","cmd":"inventaire","arg":{"nourriture":7,"linemate":0,"deraumere":1,"sibur":0}})");

	ResourceStrategy strategy;
	const std::vector<ResourceType> priority = strategy.buildPriority(state);

	ASSERT_EQ(priority.size(), 3UL);
	EXPECT_EQ(priority[0], ResourceType::Nourriture);
	EXPECT_EQ(priority[1], ResourceType::Linemate);
	EXPECT_EQ(priority[2], ResourceType::Sibur);
}

TEST(ResourceStrategyTest, FallsBackToGeneralPriorityWhenTargetsMet) {
	WorldState state;
	state.recordInventory(10, R"({"type":"response","cmd":"inventaire","arg":{"nourriture":15,"linemate":2,"deraumere":1,"sibur":1}})");

	ResourceStrategy strategy;
	const std::vector<ResourceType> priority = strategy.buildPriority(state);

	ASSERT_GE(priority.size(), 4UL);
	EXPECT_EQ(priority[0], ResourceType::Nourriture);
	EXPECT_EQ(priority[1], ResourceType::Linemate);
	EXPECT_EQ(priority[2], ResourceType::Deraumere);
	EXPECT_EQ(priority[3], ResourceType::Sibur);
}
