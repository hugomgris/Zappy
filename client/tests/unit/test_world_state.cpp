#include <gtest/gtest.h>
#include "app/WorldState.hpp"

using namespace zappy;

class WorldStateTest : public ::testing::Test {
protected:
	void SetUp() override {
		state_ = std::make_unique<WorldState>();
	}
	
	void TearDown() override {
		state_.reset();
	}
	
	std::unique_ptr<WorldState> state_;
	
	ServerMessage createWelcomeMessage(int remaining, int mapX, int mapY) {
		ServerMessage msg;
		msg.type = ServerMessageType::Welcome;
		msg.remainingClients = remaining;
		msg.mapSize = MapSize{mapX, mapY};
		return msg;
	}
	
	ServerMessage createVisionResponse(const std::vector<VisionTile>& tiles) {
		ServerMessage msg;
		msg.type = ServerMessageType::Response;
		msg.cmd = "voir";
		msg.vision = tiles;
		return msg;
	}
	
	ServerMessage createInventoryResponse(const std::map<std::string, int>& inv) {
		ServerMessage msg;
		msg.type = ServerMessageType::Response;
		msg.cmd = "inventaire";
		msg.inventory = inv;
		return msg;
	}
};

TEST_F(WorldStateTest, InitialState) {
	EXPECT_FALSE(state_->isConnected());
	EXPECT_FALSE(state_->hasMapSize());
	EXPECT_EQ(state_->getFood(), 10); // Default food
	EXPECT_EQ(state_->getLevel(), 1);
	EXPECT_EQ(state_->getForkCount(), 0);
	EXPECT_EQ(state_->getLevelUpCount(), 0);
}

TEST_F(WorldStateTest, OnWelcomeMessage) {
	auto msg = createWelcomeMessage(5, 20, 15);
	state_->onWelcome(msg);
	
	EXPECT_TRUE(state_->isConnected());
	EXPECT_TRUE(state_->hasMapSize());
	EXPECT_EQ(state_->getMapSize().x, 20);
	EXPECT_EQ(state_->getMapSize().y, 15);
}

TEST_F(WorldStateTest, OnVisionResponse) {
	std::vector<VisionTile> vision;
	
	VisionTile tile0;
	tile0.distance = 0;
	tile0.playerCount = 1;
	tile0.items = {"nourriture"};
	vision.push_back(tile0);
	
	VisionTile tile1;
	tile1.distance = 1;
	tile1.items = {"linemate", "sibur"};
	vision.push_back(tile1);
	
	auto msg = createVisionResponse(vision);
	state_->onResponse(msg);
	
	EXPECT_EQ(state_->getVision().size(), 2u);
	EXPECT_EQ(state_->getPlayersOnTile(), 1);
	EXPECT_TRUE(state_->seesItem("linemate"));
	EXPECT_TRUE(state_->seesItem("nourriture"));
	EXPECT_FALSE(state_->seesItem("phiras"));
}

TEST_F(WorldStateTest, GetNearestItem) {
	std::vector<VisionTile> vision;
	
	VisionTile tile0;
	tile0.distance = 0;
	tile0.items = {};
	vision.push_back(tile0);
	
	VisionTile tile1;
	tile1.distance = 1;
	tile1.items = {"nourriture"};
	vision.push_back(tile1);
	
	VisionTile tile2;
	tile2.distance = 2;
	tile2.items = {"linemate"};
	vision.push_back(tile2);
	
	state_->onResponse(createVisionResponse(vision));
	
	auto nearest = state_->getNearestItem("linemate");
	EXPECT_TRUE(nearest.has_value());
	EXPECT_EQ(nearest->distance, 2);
	EXPECT_TRUE(nearest->hasItem("linemate"));
	
	auto not_found = state_->getNearestItem("phiras");
	EXPECT_FALSE(not_found.has_value());
}

TEST_F(WorldStateTest, GetTilesWithItem) {
	std::vector<VisionTile> vision;
	
	VisionTile tile0;
	tile0.distance = 0;
	tile0.items = {"nourriture"};
	vision.push_back(tile0);
	
	VisionTile tile1;
	tile1.distance = 1;
	tile1.items = {"nourriture", "linemate"};
	vision.push_back(tile1);
	
	state_->onResponse(createVisionResponse(vision));
	
	auto tiles = state_->getTilesWithItem("nourriture");
	EXPECT_EQ(tiles.size(), 2u);
}

TEST_F(WorldStateTest, OnInventoryResponse) {
	std::map<std::string, int> inv = {
		{"nourriture", 15},
		{"linemate", 3},
		{"sibur", 2}
	};
	
	auto msg = createInventoryResponse(inv);
	state_->onResponse(msg);
	
	EXPECT_EQ(state_->getFood(), 15);
	EXPECT_EQ(state_->getInventory().at("linemate"), 3);
}

TEST_F(WorldStateTest, LevelRequirements) {
	// Level 1 -> 2
	auto req1 = state_->getLevelRequirement(1);
	EXPECT_EQ(req1.playersNeeded, 1);
	EXPECT_EQ(req1.stonesNeeded.at("linemate"), 1);
	
	// Level 2 -> 3
	auto req2 = state_->getLevelRequirement(2);
	EXPECT_EQ(req2.playersNeeded, 2);
	EXPECT_EQ(req2.stonesNeeded.at("linemate"), 1);
	EXPECT_EQ(req2.stonesNeeded.at("deraumere"), 1);
	EXPECT_EQ(req2.stonesNeeded.at("sibur"), 1);
	
	// Invalid level
	auto req_invalid = state_->getLevelRequirement(99);
	EXPECT_EQ(req_invalid.playersNeeded, 1);
	EXPECT_TRUE(req_invalid.stonesNeeded.empty());
}

TEST_F(WorldStateTest, CanIncantate) {
	// Setup welcome for map size
	state_->onWelcome(createWelcomeMessage(5, 10, 10));
	
	// Setup inventory with required stones
	std::map<std::string, int> inv = {
		{"nourriture", 10},
		{"linemate", 2}
	};
	state_->onResponse(createInventoryResponse(inv));
	
	// Setup vision with enough players on tile
	std::vector<VisionTile> vision;
	VisionTile tile0;
	tile0.distance = 0;
	tile0.playerCount = 2; // Enough for level 1->2 (needs 1)
	vision.push_back(tile0);
	state_->onResponse(createVisionResponse(vision));
	
	// Should be able to incantate for level 1
	EXPECT_TRUE(state_->canIncantate());
}

TEST_F(WorldStateTest, GetMissingStones) {
	// Setup inventory with some stones
	std::map<std::string, int> inv = {
		{"nourriture", 10},
		{"linemate", 1}
	};
	state_->onResponse(createInventoryResponse(inv));
	
	// Setup vision with some stones on tile
	std::vector<VisionTile> vision;
	VisionTile tile0;
	tile0.distance = 0;
	tile0.items = {"linemate"};
	vision.push_back(tile0);
	state_->onResponse(createVisionResponse(vision));
	
	auto missing = state_->getMissingStones();
	
	// Level 1 needs 1 linemate. We have 1 in inventory and 1 on tile,
	// so we should have enough.
	EXPECT_TRUE(missing.empty());
}

TEST_F(WorldStateTest, OnLevelUpEvent) {
	ServerMessage event;
	event.type = ServerMessageType::Event;
	event.arg = "Level up!";
	
	EXPECT_EQ(state_->getLevel(), 1);
	EXPECT_EQ(state_->getLevelUpCount(), 0);
	
	state_->onEvent(event);
	
	EXPECT_EQ(state_->getLevel(), 2);
	EXPECT_EQ(state_->getLevelUpCount(), 1);
}

TEST_F(WorldStateTest, OnDeathEvent) {
	// Setup connected state
	state_->onWelcome(createWelcomeMessage(5, 10, 10));
	EXPECT_TRUE(state_->isConnected());
	
	ServerMessage event;
	event.type = ServerMessageType::Event;
	event.arg = "die";
	
	state_->onEvent(event);
	
	EXPECT_FALSE(state_->isConnected());
}

TEST_F(WorldStateTest, OnForkResponse) {
	ServerMessage response;
	response.type = ServerMessageType::Response;
	response.cmd = "fork";
	response.status = "ok";
	
	EXPECT_EQ(state_->getForkCount(), 0);
	
	state_->onResponse(response);
	
	EXPECT_EQ(state_->getForkCount(), 1);
}

TEST_F(WorldStateTest, ClearState) {
	state_->onWelcome(createWelcomeMessage(5, 10, 10));
	state_->clear();
	
	EXPECT_FALSE(state_->isConnected());
	EXPECT_FALSE(state_->hasMapSize());
	EXPECT_EQ(state_->getForkCount(), 0);
	EXPECT_EQ(state_->getLevelUpCount(), 0);
	EXPECT_TRUE(state_->getVision().empty());
}