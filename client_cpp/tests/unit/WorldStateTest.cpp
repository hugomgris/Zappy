#include <gtest/gtest.h>

#include "app/state/WorldState.hpp"

TEST(WorldStateTest, RecordsVisionInventoryAndBroadcastPayloads) {
	WorldState state;

	state.recordVision(100, "[{\"tile\":0}]"
	);
	state.recordInventory(200, R"({"type":"response","cmd":"inventaire","arg":{"nourriture":7,"linemate":3,"sibur":1}})");
	state.recordBroadcast(300, "RequestBroadcast(\"help\")");

	EXPECT_TRUE(state.hasVision());
	EXPECT_TRUE(state.hasInventory());
	EXPECT_TRUE(state.hasBroadcast());
	EXPECT_EQ(state.lastVisionAt(), 100);
	EXPECT_EQ(state.lastInventoryAt(), 200);
	EXPECT_EQ(state.lastBroadcastAt(), 300);
	EXPECT_EQ(state.lastVisionPayload(), "[{\"tile\":0}]");
	EXPECT_EQ(state.lastInventoryPayload(), R"({"type":"response","cmd":"inventaire","arg":{"nourriture":7,"linemate":3,"sibur":1}})");
	EXPECT_EQ(state.lastBroadcastPayload(), "RequestBroadcast(\"help\")");
	EXPECT_EQ(state.inventoryCount(ResourceType::Nourriture), 7);
	EXPECT_EQ(state.inventoryCount(ResourceType::Linemate), 3);
	EXPECT_EQ(state.inventoryCount(ResourceType::Sibur), 1);
}

TEST(WorldStateTest, RecentChecksRespectAgeThresholds) {
	WorldState state;
	state.recordVision(1000, "vision");
	state.recordInventory(1000, "inventory");

	EXPECT_TRUE(state.hasRecentVision(4000, 5000));
	EXPECT_FALSE(state.hasRecentVision(7001, 5000));
	EXPECT_TRUE(state.hasRecentInventory(5000, 5000));
	EXPECT_FALSE(state.hasRecentInventory(7001, 5000));
}

TEST(WorldStateTest, TracksPoseAndVisionTiles) {
	WorldState state;

	state.recordPose(50, 3, 4, 2);
	state.recordTurnLeft(60);
	state.recordForward(70);
	state.recordVision(100, R"([["nourriture"],[],["linemate"],["sibur"]])");

	EXPECT_TRUE(state.hasPose());
	EXPECT_TRUE(state.hasVision());
	EXPECT_EQ(state.lastPoseAt(), 70);
	ASSERT_TRUE(state.pose().has_value());
	EXPECT_EQ(state.pose()->x, 3);
	EXPECT_EQ(state.pose()->y, 5);
	EXPECT_EQ(state.pose()->orientation, 1);
	ASSERT_EQ(state.visionTiles().size(), 4UL);

	const std::optional<WorldState::VisionTile> linemateTile = state.nearestVisionTileWith(ResourceType::Linemate);
	ASSERT_TRUE(linemateTile.has_value());
	EXPECT_EQ(linemateTile->index, 2UL);
	EXPECT_EQ(linemateTile->localX, 0);
	EXPECT_EQ(linemateTile->localY, 1);
	EXPECT_TRUE(linemateTile->hasResource(ResourceType::Linemate));
	EXPECT_FALSE(linemateTile->hasResource(ResourceType::Sibur));
}

TEST(WorldStateTest, ParsesCurrentTilePlayerCountFromVision) {
	WorldState state;
	state.recordVision(100, R"([["player","player","nourriture"],[],[]])");

	EXPECT_EQ(state.currentTilePlayerCount(), 2);
	ASSERT_FALSE(state.visionTiles().empty());
	EXPECT_EQ(state.visionTiles()[0].playerCount, 2);
}

TEST(WorldStateTest, ClearResetsStoredState) {
	WorldState state;
	state.recordVision(100, "vision");
	state.recordInventory(200, "inventory");
	state.recordBroadcast(300, "broadcast");

	state.clear();

	EXPECT_FALSE(state.hasVision());
	EXPECT_FALSE(state.hasInventory());
	EXPECT_FALSE(state.hasBroadcast());
	EXPECT_FALSE(state.lastVisionAt().has_value());
	EXPECT_FALSE(state.lastInventoryAt().has_value());
	EXPECT_FALSE(state.lastBroadcastAt().has_value());
	EXPECT_TRUE(state.lastVisionPayload().empty());
	EXPECT_TRUE(state.lastInventoryPayload().empty());
	EXPECT_TRUE(state.lastBroadcastPayload().empty());
	EXPECT_TRUE(state.inventoryCounts().empty());
	EXPECT_TRUE(state.visionTiles().empty());
	EXPECT_FALSE(state.pose().has_value());
}
