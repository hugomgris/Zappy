#include <gtest/gtest.h>

#include "app/intent/Intent.hpp"
#include "app/event/CommandEvent.hpp"
#include "app/CommandManager.hpp"
#include "app/command/CommandRequest.hpp"

class IntentFlowIntegrationTest : public ::testing::Test {
	protected:
		std::vector<CommandEvent> capturedEvents;

		void SetUp() override {
			capturedEvents.clear();
		}

		void captureEvent(const CommandEvent& event) {
			capturedEvents.push_back(event);
		}

		CommandManager createManagerWithMockDispatcher() {
			std::function<Result(const CommandRequest&)> mockDispatcher =
				[](const CommandRequest& /*req*/) {
					return Result::success();
				};
			return CommandManager(mockDispatcher);
		}
};

TEST_F(IntentFlowIntegrationTest, EventHandlerIsCalledOnCommandCompletion) {
	CommandManager manager = createManagerWithMockDispatcher();

	manager.setEventHandler([this](const CommandEvent& event) {
		this->captureEvent(event);
	});

	const std::uint64_t cmdId = manager.enqueue(CommandType::Voir, 0);
	EXPECT_NE(cmdId, 0UL);

	manager.tick(0);
	const bool matched = manager.onServerTextFrame("{\"type\":\"response\",\"cmd\":\"voir\",\"arg\":\"{player}\"}");

	EXPECT_TRUE(matched);
	EXPECT_EQ(capturedEvents.size(), 1UL);
	EXPECT_EQ(capturedEvents[0].commandId, cmdId);
	EXPECT_EQ(capturedEvents[0].commandType, CommandType::Voir);
	EXPECT_EQ(capturedEvents[0].status, CommandStatus::Success);
}

TEST_F(IntentFlowIntegrationTest, EventHandlerReceivesFailureStatus) {
	std::function<Result(const CommandRequest&)> failingDispatcher =
		[](const CommandRequest& /*req*/) {
			return Result::failure(ErrorCode::NetworkError, "Connection lost");
		};

	CommandManager manager(failingDispatcher);
	manager.setEventHandler([this](const CommandEvent& event) {
		this->captureEvent(event);
	});

	const std::uint64_t cmdId = manager.enqueue(CommandType::Voir, 0);
	manager.tick(0);

	EXPECT_EQ(capturedEvents.size(), 1UL);
	EXPECT_EQ(capturedEvents[0].commandId, cmdId);
	EXPECT_EQ(capturedEvents[0].status, CommandStatus::NetworkError);
}

TEST_F(IntentFlowIntegrationTest, EventHandlerReceivesMultipleEvents) {
	CommandManager manager = createManagerWithMockDispatcher();

	manager.setEventHandler([this](const CommandEvent& event) {
		this->captureEvent(event);
	});

	const std::uint64_t cmd1 = manager.enqueue(CommandType::Voir, 0);
	const std::uint64_t cmd2 = manager.enqueue(CommandType::Inventaire, 0);

	manager.tick(0);
	EXPECT_TRUE(manager.hasInFlight());

	manager.onServerTextFrame("{\"type\":\"response\",\"cmd\":\"voir\",\"arg\":\"{player}\"}");
	EXPECT_EQ(capturedEvents.size(), 1UL);

	manager.tick(0);
	manager.onServerTextFrame("{\"type\":\"response\",\"cmd\":\"inventaire\",\"arg\":\"nourriture 10\"}");

	EXPECT_EQ(capturedEvents.size(), 2UL);
	EXPECT_EQ(capturedEvents[0].commandId, cmd1);
	EXPECT_EQ(capturedEvents[1].commandId, cmd2);
}

TEST_F(IntentFlowIntegrationTest, IntentTypesConvertToReadableDescriptions) {
	auto vreq = std::make_unique<RequestVoir>();
	EXPECT_EQ(vreq->description(), "RequestVoir");

	auto ireq = std::make_unique<RequestInventaire>();
	EXPECT_EQ(ireq->description(), "RequestInventaire");

	auto treq = std::make_unique<RequestTake>(ResourceType::Linemate);
	EXPECT_EQ(treq->description(), "RequestTake(linemate)");

	auto preq = std::make_unique<RequestPlace>(ResourceType::Sibur);
	EXPECT_EQ(preq->description(), "RequestPlace(sibur)");

	auto mreq = std::make_unique<RequestMove>();
	EXPECT_EQ(mreq->description(), "RequestMove");

	auto rreq = std::make_unique<RequestTurnRight>();
	EXPECT_EQ(rreq->description(), "RequestTurnRight");

	auto lreq = std::make_unique<RequestTurnLeft>();
	EXPECT_EQ(lreq->description(), "RequestTurnLeft");

	auto breq = std::make_unique<RequestBroadcast>("hello world");
	EXPECT_EQ(breq->description(), "RequestBroadcast(\"hello world\")");
}

TEST_F(IntentFlowIntegrationTest, CommandEventConvenienceMethodsReturnCorrectStatus) {
	CommandEvent successEvent;
	successEvent.status = CommandStatus::Success;
	successEvent.commandId = 1;
	successEvent.commandType = CommandType::Voir;

	EXPECT_TRUE(successEvent.isSuccess());
	EXPECT_FALSE(successEvent.isFailure());
	EXPECT_EQ(successEvent.statusName(), "Success");

	CommandEvent failureEvent;
	failureEvent.status = CommandStatus::Timeout;
	failureEvent.commandId = 2;
	failureEvent.commandType = CommandType::Inventaire;

	EXPECT_FALSE(failureEvent.isSuccess());
	EXPECT_TRUE(failureEvent.isFailure());
	EXPECT_EQ(failureEvent.statusName(), "Timeout");

	CommandEvent e;
	e.status = CommandStatus::MalformedReply;
	EXPECT_EQ(e.statusName(), "MalformedReply");

	e.status = CommandStatus::UnexpectedReply;
	EXPECT_EQ(e.statusName(), "UnexpectedReply");

	e.status = CommandStatus::ServerError;
	EXPECT_EQ(e.statusName(), "ServerError");

	e.status = CommandStatus::NetworkError;
	EXPECT_EQ(e.statusName(), "NetworkError");

	e.status = CommandStatus::ProtocolError;
	EXPECT_EQ(e.statusName(), "ProtocolError");

	e.status = CommandStatus::Retrying;
	EXPECT_EQ(e.statusName(), "Retrying");
}
