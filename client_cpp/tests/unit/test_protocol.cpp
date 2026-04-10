#include <gtest/gtest.h>
#include "app/ProtocolTypes.hpp"
#include <cJSON.h>

using namespace zappy;

class ProtocolTest : public ::testing::Test {
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(ProtocolTest, ParseBienvenueMessage) {
	std::string json = R"({
		"type": "bienvenue",
		"msg": "Welcome to Zappy!"
	})";
	
	ServerMessage msg = parseServerMessage(json);
	
	EXPECT_EQ(msg.type, ServerMessageType::Bienvenue);
	EXPECT_EQ(msg.raw, json);
}

TEST_F(ProtocolTest, ParseWelcomeMessage) {
	std::string json = R"({
		"type": "welcome",
		"remaining_clients": 5,
		"map_size": {"x": 10, "y": 10}
	})";
	
	ServerMessage msg = parseServerMessage(json);
	
	EXPECT_EQ(msg.type, ServerMessageType::Welcome);
	EXPECT_TRUE(msg.remainingClients.has_value());
	EXPECT_EQ(*msg.remainingClients, 5);
	EXPECT_TRUE(msg.mapSize.has_value());
	EXPECT_EQ(msg.mapSize->x, 10);
	EXPECT_EQ(msg.mapSize->y, 10);
}

TEST_F(ProtocolTest, ParseVisionResponse) {
	std::string json = R"({
		"type": "response",
		"cmd": "voir",
		"vision": [
			["player"],
			["linemate", "nourriture"],
			["sibur", "player", "player"],
			[]
		]
	})";
	
	ServerMessage msg = parseServerMessage(json);
	
	EXPECT_EQ(msg.type, ServerMessageType::Response);
	EXPECT_EQ(msg.cmd, "voir");
	EXPECT_TRUE(msg.vision.has_value());
	
	auto& vision = *msg.vision;
	ASSERT_EQ(vision.size(), 4u);
	
	// Tile 0 (distance 0)
	EXPECT_EQ(vision[0].distance, 0);
	EXPECT_EQ(vision[0].localX, 0);
	EXPECT_EQ(vision[0].localY, 0);
	EXPECT_EQ(vision[0].playerCount, 1);
	EXPECT_EQ(vision[0].items.size(), 0u);
	
	// Tile 1 (distance 1, row 1, position -1)
	EXPECT_EQ(vision[1].distance, 1);
	EXPECT_EQ(vision[1].localX, -1);
	EXPECT_EQ(vision[1].localY, 1);
	EXPECT_EQ(vision[1].playerCount, 0);
	ASSERT_EQ(vision[1].items.size(), 2u);
	EXPECT_EQ(vision[1].items[0], "linemate");
	EXPECT_EQ(vision[1].items[1], "nourriture");
	
	// Tile 2 (distance 1, row 1, position 0)
	EXPECT_EQ(vision[2].distance, 2);
	EXPECT_EQ(vision[2].localX, 0);
	EXPECT_EQ(vision[2].localY, 1);
	EXPECT_EQ(vision[2].playerCount, 2);
	ASSERT_EQ(vision[2].items.size(), 1u);
	EXPECT_EQ(vision[2].items[0], "sibur");
}

TEST_F(ProtocolTest, ParseVisionCoordinateCalculation) {
	// Test that coordinates are correctly calculated for various vision sizes
	std::string json = R"({
		"type": "response",
		"cmd": "voir",
		"vision": [
			[], [], [], [], [], [], [], [], [], [], [], [], []
		]
	})";
	
	ServerMessage msg = parseServerMessage(json);
	ASSERT_TRUE(msg.vision.has_value());
	auto& vision = *msg.vision;
	
	// Expected pattern: row 0 (1 tile), row 1 (3 tiles), row 2 (5 tiles), row 3 (7 tiles)
	// Tile 0: distance=0, localX=0, localY=0
	EXPECT_EQ(vision[0].distance, 0);
	EXPECT_EQ(vision[0].localX, 0);
	EXPECT_EQ(vision[0].localY, 0);
	
	// Tile 1: distance=1, localX=-1, localY=1
	EXPECT_EQ(vision[1].distance, 1);
	EXPECT_EQ(vision[1].localX, -1);
	EXPECT_EQ(vision[1].localY, 1);
	
	// Tile 2: distance=1, localX=0, localY=1
	EXPECT_EQ(vision[2].distance, 2);
	EXPECT_EQ(vision[2].localX, 0);
	EXPECT_EQ(vision[2].localY, 1);
	
	// Tile 3: distance=1, localX=1, localY=1
	EXPECT_EQ(vision[3].distance, 3);
	EXPECT_EQ(vision[3].localX, 1);
	EXPECT_EQ(vision[3].localY, 1);
	
	// Tile 4: distance=2, localX=-2, localY=2
	EXPECT_EQ(vision[4].distance, 4);
	EXPECT_EQ(vision[4].localX, -2);
	EXPECT_EQ(vision[4].localY, 2);
}

TEST_F(ProtocolTest, ParseInventoryResponse) {
	std::string json = R"({
		"type": "response",
		"cmd": "inventaire",
		"inventaire": {
			"nourriture": 10,
			"linemate": 5,
			"deraumere": 3,
			"sibur": 2,
			"mendiane": 1,
			"phiras": 0,
			"thystame": 4
		}
	})";
	
	ServerMessage msg = parseServerMessage(json);
	
	EXPECT_EQ(msg.type, ServerMessageType::Response);
	EXPECT_EQ(msg.cmd, "inventaire");
	EXPECT_TRUE(msg.inventory.has_value());
	
	auto& inv = *msg.inventory;
	EXPECT_EQ(inv["nourriture"], 10);
	EXPECT_EQ(inv["linemate"], 5);
	EXPECT_EQ(inv["deraumere"], 3);
	EXPECT_EQ(inv["sibur"], 2);
	EXPECT_EQ(inv["mendiane"], 1);
	EXPECT_EQ(inv["phiras"], 0);
	EXPECT_EQ(inv["thystame"], 4);
}

TEST_F(ProtocolTest, ParseEventLevelUp) {
	std::string json = R"({
		"type": "event",
		"event": "Level up!"
	})";
	
	ServerMessage msg = parseServerMessage(json);
	
	EXPECT_EQ(msg.type, ServerMessageType::Event);
	EXPECT_TRUE(msg.isLevelUp());
	EXPECT_FALSE(msg.isDeath());
}

TEST_F(ProtocolTest, ParseEventDeath) {
	std::string json = R"({
		"type": "event",
		"arg": "die"
	})";
	
	ServerMessage msg = parseServerMessage(json);
	
	EXPECT_EQ(msg.type, ServerMessageType::Event);
	EXPECT_TRUE(msg.isDeath());
	EXPECT_FALSE(msg.isLevelUp());
}

TEST_F(ProtocolTest, ParseBroadcastMessage) {
	std::string json = R"({
		"type": "message",
		"arg": "Hello world!",
		"status": "3"
	})";
	
	ServerMessage msg = parseServerMessage(json);
	
	EXPECT_EQ(msg.type, ServerMessageType::Message);
	EXPECT_TRUE(msg.messageText.has_value());
	EXPECT_EQ(*msg.messageText, "Hello world!");
	EXPECT_TRUE(msg.direction.has_value());
	EXPECT_EQ(*msg.direction, 3);
}

TEST_F(ProtocolTest, ParseDeplacementEvent) {
	std::string json = R"({
		"type": "response",
		"cmd": "deplacement",
		"status": "5"
	})";
	
	ServerMessage msg = parseServerMessage(json);
	
	EXPECT_EQ(msg.type, ServerMessageType::Response);
	EXPECT_EQ(msg.cmd, "deplacement");
	EXPECT_TRUE(msg.direction.has_value());
	EXPECT_EQ(*msg.direction, 5);
}

TEST_F(ProtocolTest, OrientationConversion) {
	EXPECT_EQ(orientationFromString("N"), 1);
	EXPECT_EQ(orientationFromString("north"), 1);
	EXPECT_EQ(orientationFromString("E"), 2);
	EXPECT_EQ(orientationFromString("east"), 2);
	EXPECT_EQ(orientationFromString("S"), 3);
	EXPECT_EQ(orientationFromString("south"), 3);
	EXPECT_EQ(orientationFromString("W"), 4);
	EXPECT_EQ(orientationFromString("west"), 4);
	EXPECT_EQ(orientationFromString("invalid"), 1);
	
	EXPECT_EQ(orientationToString(1), "N");
	EXPECT_EQ(orientationToString(2), "E");
	EXPECT_EQ(orientationToString(3), "S");
	EXPECT_EQ(orientationToString(4), "W");
	EXPECT_EQ(orientationToString(5), "N");
}

TEST_F(ProtocolTest, ApplyTurn) {
	int orientation = 1; // North
	
	applyTurn(orientation, true); // Right -> East
	EXPECT_EQ(orientation, 2);
	
	applyTurn(orientation, true); // Right -> South
	EXPECT_EQ(orientation, 3);
	
	applyTurn(orientation, false); // Left -> East
	EXPECT_EQ(orientation, 2);
	
	applyTurn(orientation, false); // Left -> North
	EXPECT_EQ(orientation, 1);
	
	applyTurn(orientation, false); // Left -> West
	EXPECT_EQ(orientation, 4);
	
	applyTurn(orientation, true); // Right -> North
	EXPECT_EQ(orientation, 1);
}

TEST_F(ProtocolTest, ApplyMove) {
	int x = 5, y = 5;
	int mapWidth = 10, mapHeight = 10;
	
	applyMove(x, y, 1, mapWidth, mapHeight); // North
	EXPECT_EQ(x, 5);
	EXPECT_EQ(y, 4);
	
	applyMove(x, y, 2, mapWidth, mapHeight); // East
	EXPECT_EQ(x, 6);
	EXPECT_EQ(y, 4);
	
	applyMove(x, y, 3, mapWidth, mapHeight); // South
	EXPECT_EQ(x, 6);
	EXPECT_EQ(y, 5);
	
	applyMove(x, y, 4, mapWidth, mapHeight); // West
	EXPECT_EQ(x, 5);
	EXPECT_EQ(y, 5);
	
	// Test wrap-around
	x = 0; y = 0;
	applyMove(x, y, 1, mapWidth, mapHeight); // North from 0,0
	EXPECT_EQ(x, 0);
	EXPECT_EQ(y, 9);
	
	x = 9; y = 9;
	applyMove(x, y, 3, mapWidth, mapHeight); // South from 9,9
	EXPECT_EQ(x, 9);
	EXPECT_EQ(y, 0);
}

TEST_F(ProtocolTest, VisionTileHelpers) {
	VisionTile tile;
	tile.items = {"linemate", "nourriture", "linemate", "sibur"};
	tile.playerCount = 2;
	
	EXPECT_TRUE(tile.hasItem("linemate"));
	EXPECT_TRUE(tile.hasItem("nourriture"));
	EXPECT_FALSE(tile.hasItem("phiras"));
	EXPECT_EQ(tile.countItem("linemate"), 2);
	EXPECT_EQ(tile.countItem("nourriture"), 1);
}

TEST_F(ProtocolTest, PlayerStateHelpers) {
	PlayerState player;
	player.inventory["nourriture"] = 10;
	player.inventory["linemate"] = 5;
	
	EXPECT_EQ(player.getFood(), 10);
	EXPECT_TRUE(player.hasItem("linemate", 3));
	EXPECT_FALSE(player.hasItem("linemate", 6));
	EXPECT_FALSE(player.hasItem("phiras"));
}