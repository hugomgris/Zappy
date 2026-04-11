#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "app/CommandSender.hpp"
#include "mocks/mock_websocket_client.hpp"

using namespace zappy;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

class CommandSenderTest : public ::testing::Test {
protected:
	void SetUp() override {
		mock_ws_ = std::make_unique<MockWebsocketClient>();
		sender_ = std::make_unique<CommandSender>(*mock_ws_);
	}
	
	void TearDown() override {
		sender_.reset();
		mock_ws_.reset();
	}
	
	std::unique_ptr<MockWebsocketClient> mock_ws_;
	std::unique_ptr<CommandSender> sender_;
};

TEST_F(CommandSenderTest, SendLoginCommand) {
	EXPECT_CALL(*mock_ws_, sendText(_))
		.WillOnce([](const std::string& text) {
			EXPECT_TRUE(text.find("\"type\":\"login\"") != std::string::npos);
			EXPECT_TRUE(text.find("\"role\":\"player\"") != std::string::npos);
			EXPECT_TRUE(text.find("\"team-name\":\"test_team\"") != std::string::npos);
			
			IoResult res;
			res.status = NetStatus::Ok;
			res.bytes = text.size();
			return res;
		});
	
	Result res = sender_->sendLogin("test_team");
	EXPECT_TRUE(res.ok());
}

TEST_F(CommandSenderTest, SendVoirCommand) {
	EXPECT_CALL(*mock_ws_, sendText(_))
		.WillOnce([](const std::string& text) {
			EXPECT_TRUE(text.find("\"cmd\":\"voir\"") != std::string::npos);
			
			IoResult res;
			res.status = NetStatus::Ok;
			return res;
		});
	
	Result res = sender_->sendVoir();
	EXPECT_TRUE(res.ok());
}

TEST_F(CommandSenderTest, SendMovementCommands) {
	EXPECT_CALL(*mock_ws_, sendText(_))
		.Times(3)
		.WillRepeatedly([](const std::string&) {
			IoResult res;
			res.status = NetStatus::Ok;
			return res;
		});
	
	EXPECT_TRUE(sender_->sendAvance().ok());
	EXPECT_TRUE(sender_->sendDroite().ok());
	EXPECT_TRUE(sender_->sendGauche().ok());
}

TEST_F(CommandSenderTest, SendPrendCommand) {
	EXPECT_CALL(*mock_ws_, sendText(_))
		.WillOnce([](const std::string& text) {
			EXPECT_TRUE(text.find("\"cmd\":\"prend\"") != std::string::npos);
			EXPECT_TRUE(text.find("\"arg\":\"nourriture\"") != std::string::npos);
			
			IoResult res;
			res.status = NetStatus::Ok;
			return res;
		});
	
	Result res = sender_->sendPrend("nourriture");
	EXPECT_TRUE(res.ok());
}

TEST_F(CommandSenderTest, SendPoseCommand) {
	EXPECT_CALL(*mock_ws_, sendText(_))
		.WillOnce([](const std::string& text) {
			EXPECT_TRUE(text.find("\"cmd\":\"pose\"") != std::string::npos);
			EXPECT_TRUE(text.find("\"arg\":\"linemate\"") != std::string::npos);
			
			IoResult res;
			res.status = NetStatus::Ok;
			return res;
		});
	
	Result res = sender_->sendPose("linemate");
	EXPECT_TRUE(res.ok());
}

TEST_F(CommandSenderTest, SendIncantationCommand) {
	EXPECT_CALL(*mock_ws_, sendText(_))
		.WillOnce([](const std::string& text) {
			EXPECT_TRUE(text.find("\"cmd\":\"incantation\"") != std::string::npos);
			
			IoResult res;
			res.status = NetStatus::Ok;
			return res;
		});
	
	Result res = sender_->sendIncantation();
	EXPECT_TRUE(res.ok());
}

TEST_F(CommandSenderTest, SendForkCommand) {
	EXPECT_CALL(*mock_ws_, sendText(_))
		.WillOnce([](const std::string& text) {
			EXPECT_TRUE(text.find("\"cmd\":\"fork\"") != std::string::npos);
			
			IoResult res;
			res.status = NetStatus::Ok;
			return res;
		});
	
	Result res = sender_->sendFork();
	EXPECT_TRUE(res.ok());
}

TEST_F(CommandSenderTest, ExpectResponseAndCallback) {
	bool callback_called = false;
	std::string received_cmd;
	
	uint64_t id = sender_->expectResponse("voir", 
		[&](const ServerMessage& msg) {
			callback_called = true;
			received_cmd = msg.cmd;
		});
	
	EXPECT_EQ(id, 1ULL);
	EXPECT_EQ(sender_->pendingCount(), 1u);
	
	// Simulate response
	ServerMessage response;
	response.type = ServerMessageType::Response;
	response.cmd = "voir";
	response.status = "ok";
	
	sender_->processResponse(response);
	
	EXPECT_TRUE(callback_called);
	EXPECT_EQ(received_cmd, "voir");
	EXPECT_EQ(sender_->pendingCount(), 0u);
}

TEST_F(CommandSenderTest, ProcessResponseOutOfOrder) {
	bool voir_called = false;
	bool inventaire_called = false;
	
	sender_->expectResponse("voir", [&](const ServerMessage&) { voir_called = true; });
	sender_->expectResponse("inventaire", [&](const ServerMessage&) { inventaire_called = true; });
	
	EXPECT_EQ(sender_->pendingCount(), 2u);
	
	// Send inventaire response first (out of order)
	ServerMessage inv_response;
	inv_response.type = ServerMessageType::Response;
	inv_response.cmd = "inventaire";
	sender_->processResponse(inv_response);
	
	EXPECT_TRUE(inventaire_called);
	EXPECT_FALSE(voir_called);
	EXPECT_EQ(sender_->pendingCount(), 1u);
	
	// Send voir response second
	ServerMessage voir_response;
	voir_response.type = ServerMessageType::Response;
	voir_response.cmd = "voir";
	sender_->processResponse(voir_response);
	
	EXPECT_TRUE(voir_called);
	EXPECT_EQ(sender_->pendingCount(), 0u);
}

TEST_F(CommandSenderTest, ProcessResponseNoMatch) {
	bool callback_called = false;
	sender_->expectResponse("voir", [&](const ServerMessage&) { callback_called = true; });
	
	// Send wrong response
	ServerMessage response;
	response.type = ServerMessageType::Response;
	response.cmd = "avance";
	
	sender_->processResponse(response);
	
	EXPECT_FALSE(callback_called);
	EXPECT_EQ(sender_->pendingCount(), 1u);
}

TEST_F(CommandSenderTest, CheckTimeouts) {
	bool callback_called = false;
	std::string error_status;
	
	sender_->expectResponse("voir", [&](const ServerMessage& msg) {
		callback_called = true;
		error_status = msg.status;
	});
	
	// Simulate time passing (this is a bit tricky without time mocking)
	// For now, test with immediate timeout
	sender_->checkTimeouts(0); // 0ms timeout
	
	EXPECT_TRUE(callback_called);
	EXPECT_EQ(error_status, "timeout");
	EXPECT_EQ(sender_->pendingCount(), 0u);
}

TEST_F(CommandSenderTest, CancelAll) {
	bool callback1_called = false;
	bool callback2_called = false;
	
	sender_->expectResponse("voir", [&](const ServerMessage&) { callback1_called = true; });
	sender_->expectResponse("inventaire", [&](const ServerMessage&) { callback2_called = true; });
	
	EXPECT_EQ(sender_->pendingCount(), 2u);
	
	sender_->cancelAll();
	
	EXPECT_EQ(sender_->pendingCount(), 0u);
	EXPECT_FALSE(callback1_called);
	EXPECT_FALSE(callback2_called);
}

TEST_F(CommandSenderTest, SendBroadcastCommand) {
	EXPECT_CALL(*mock_ws_, sendText(_))
		.WillOnce([](const std::string& text) {
			EXPECT_TRUE(text.find("\"cmd\":\"broadcast\"") != std::string::npos);
			EXPECT_TRUE(text.find("\"arg\":\"Hello team!\"") != std::string::npos);
			
			IoResult res;
			res.status = NetStatus::Ok;
			return res;
		});
	
	Result res = sender_->sendBroadcast("Hello team!");
	EXPECT_TRUE(res.ok());
}