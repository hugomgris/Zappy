#include <gtest/gtest.h>

#include "app/policy/NavigationPlanner.hpp"

TEST(NavigationPlannerTest, PlansTakeForResourceOnCurrentTile) {
	WorldState state;
	state.recordVision(100, R"([["nourriture"],[],[]])");

	NavigationPlanner planner;
	const std::vector<NavigationStep> plan = planner.buildPlan(state);

	ASSERT_EQ(plan.size(), 1UL);
	EXPECT_EQ(plan[0].commandType, CommandType::Prend);
	ASSERT_TRUE(plan[0].intent);
	EXPECT_EQ(plan[0].intent->description(), "RequestTake(nourriture)");
}

TEST(NavigationPlannerTest, PlansMovementTowardVisibleResource) {
	WorldState state;
	state.recordPose(10, 0, 0, 1);
	state.recordVision(20, R"([[],[],[],["nourriture"]])");

	NavigationPlanner planner;
	const std::vector<NavigationStep> plan = planner.buildPlan(state);

	ASSERT_EQ(plan.size(), 4UL);
	EXPECT_EQ(plan[0].commandType, CommandType::Droite);
	EXPECT_EQ(plan[1].commandType, CommandType::Avance);
	EXPECT_EQ(plan[2].commandType, CommandType::Gauche);
	EXPECT_EQ(plan[3].commandType, CommandType::Avance);
	ASSERT_TRUE(plan[0].intent);
	ASSERT_TRUE(plan[1].intent);
	ASSERT_TRUE(plan[2].intent);
	ASSERT_TRUE(plan[3].intent);
	EXPECT_EQ(plan[0].intent->description(), "RequestTurnRight");
	EXPECT_EQ(plan[1].intent->description(), "RequestMove");
	EXPECT_EQ(plan[2].intent->description(), "RequestTurnLeft");
	EXPECT_EQ(plan[3].intent->description(), "RequestMove");
}

TEST(NavigationPlannerTest, FallsBackToExplorationWhenNoResourceExists) {
	WorldState state;
	state.recordVision(5, R"([[],[],[]])");

	NavigationPlanner planner;
	const std::vector<NavigationStep> plan = planner.buildPlan(state);

	ASSERT_EQ(plan.size(), 2UL);
	EXPECT_EQ(plan[0].commandType, CommandType::Gauche);
	EXPECT_EQ(plan[1].commandType, CommandType::Avance);
	ASSERT_TRUE(plan[0].intent);
	ASSERT_TRUE(plan[1].intent);
	EXPECT_EQ(plan[0].intent->description(), "RequestTurnLeft");
	EXPECT_EQ(plan[1].intent->description(), "RequestMove");
}

TEST(NavigationPlannerTest, HonorsProvidedResourcePriority) {
	WorldState state;
	state.recordVision(100, R"([["linemate","nourriture"],[],[]])");

	NavigationPlanner planner;
	const std::vector<NavigationStep> plan = planner.buildPlan(
		state,
		{ResourceType::Linemate, ResourceType::Nourriture}
	);

	ASSERT_EQ(plan.size(), 1UL);
	EXPECT_EQ(plan[0].commandType, CommandType::Prend);
	ASSERT_TRUE(plan[0].intent);
	EXPECT_EQ(plan[0].intent->description(), "RequestTake(linemate)");
}