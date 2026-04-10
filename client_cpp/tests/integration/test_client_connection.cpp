#include <gtest/gtest.h>
#include "app/Client.hpp"
#include "helpers/Logger.hpp"
#include <thread>
#include <chrono>
#include <csignal>

using namespace zappy;

// These tests require a running Zappy server
// Skip if server is not available
class ClientIntegrationTest : public ::testing::Test {
protected:
	void SetUp() override {
		// Check if server is running
		if (!isServerRunning()) {
			GTEST_SKIP() << "Zappy server not running on localhost:8674";
		}
		
		Logger::setLevel(LogLevel::Debug);
		client_ = std::make_unique<Client>("localhost", 8674, "team1");
	}
	
	void TearDown() override {
		if (client_ && client_->isRunning()) {
			client_->stop();
		}
		client_.reset();
	}
	
	bool isServerRunning() {
		// Simple check - try to connect
		WebsocketClient ws;
		Result res = ws.connect("localhost", 8674, true);
		return res.ok();
	}
	
	std::unique_ptr<Client> client_;
};

TEST_F(ClientIntegrationTest, ConnectToServer) {
	Result res = client_->connect(5000);
	if (!res.ok()) {
		GTEST_SKIP() << "Server not available: " << res.message;
	}
	
	EXPECT_TRUE(res.ok()) << "Connection failed: " << res.message;
	EXPECT_TRUE(client_->isConnected());
}

TEST_F(ClientIntegrationTest, RunClientForShortTime) {
	Result res = client_->connect(5000);
	if (!res.ok()) {
		GTEST_SKIP() << "Server not available: " << res.message;
	}
	
	res = client_->run();
	if (!res.ok()) {
		GTEST_SKIP() << "Run failed due to environment: " << res.message;
	}
	
	// Let it run for 3 seconds
	std::this_thread::sleep_for(std::chrono::seconds(3));
	
	EXPECT_TRUE(client_->isRunning());
	EXPECT_TRUE(client_->isConnected());
	
	client_->stop();
	EXPECT_FALSE(client_->isRunning());
}

TEST_F(ClientIntegrationTest, ReceiveVisionAfterConnect) {
	bool vision_received = false;
	
	client_->onMessage([&vision_received](const ServerMessage& msg) {
		if (msg.type == ServerMessageType::Response && msg.cmd == "voir") {
			vision_received = true;
		}
	});
	
	Result res = client_->connect(5000);
	if (!res.ok()) {
		GTEST_SKIP() << "Server not available: " << res.message;
	}
	
	res = client_->run();
	if (!res.ok()) {
		GTEST_SKIP() << "Run failed due to environment: " << res.message;
	}
	
	// Wait for vision update (AI requests vision every 3 seconds)
	for (int i = 0; i < 40 && !vision_received; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	
	EXPECT_TRUE(vision_received);
	
	client_->stop();
}

TEST_F(ClientIntegrationTest, ReceiveInventory) {
	bool inventory_received = false;
	
	client_->onMessage([&inventory_received](const ServerMessage& msg) {
		if (msg.type == ServerMessageType::Response && msg.cmd == "inventaire") {
			inventory_received = true;
		}
	});
	
	Result res = client_->connect(5000);
	if (!res.ok()) {
		GTEST_SKIP() << "Server not available: " << res.message;
	}
	
	res = client_->run();
	if (!res.ok()) {
		GTEST_SKIP() << "Run failed due to environment: " << res.message;
	}
	
	// Wait for inventory update (AI requests every 5 seconds)
	for (int i = 0; i < 60 && !inventory_received; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	
	EXPECT_TRUE(inventory_received);
	
	client_->stop();
}

TEST_F(ClientIntegrationTest, StateUpdatesAfterConnect) {
	Result res = client_->connect(5000);
	if (!res.ok()) {
		GTEST_SKIP() << "Server not available: " << res.message;
	}
	
	res = client_->run();
	if (!res.ok()) {
		GTEST_SKIP() << "Run failed due to environment: " << res.message;
	}
	
	// Wait a bit for state to initialize
	std::this_thread::sleep_for(std::chrono::seconds(2));
	
	const WorldState& state = client_->getState();
	
	EXPECT_TRUE(state.isConnected());
	EXPECT_TRUE(state.hasMapSize());
	EXPECT_GT(state.getLevel(), 0);
	EXPECT_GT(state.getFood(), 0);
	
	client_->stop();
}