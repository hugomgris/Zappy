#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "../srcs/protocol/Sender.hpp"
#include "../../incs/third_party/json.hpp"

using json = nlohmann::json;
using namespace testing;

class MockWebsocketClient : public WebsocketClient {
public:
    MOCK_METHOD(IoResult, sendText, (const std::string& text), (override));
};

TEST(SenderTest, SendVoirSendsCorrectJson) {
    MockWebsocketClient ws;
    Sender sender(ws);

    IoResult ok{};
    ok.status = NetStatus::Ok;

    std::string captured;
    EXPECT_CALL(ws, sendText(_))
        .WillOnce([&](const std::string& text) {
            captured = text;
            return ok;
        });

    Result res = sender.sendVoir();

    EXPECT_TRUE(res.ok());
    auto j = json::parse(captured);
    EXPECT_EQ(j["type"], "cmd");
    EXPECT_EQ(j["cmd"],  "voir");
}

TEST(SenderTest, ProcessResponseFiresCallback) {
    MockWebsocketClient ws;
    Sender sender(ws);

    bool fired = false;
    ServerMessage received;

    sender.expect("voir", [&](const ServerMessage& msg) {
        fired = true;
        received = msg;
    });

    ServerMessage response;
    response.type   = MsgType::Response;
    response.cmd    = "voir";
    response.status = "ok";

    sender.processResponse(response);

    EXPECT_TRUE(fired);
    EXPECT_EQ(received.cmd, "voir");
    EXPECT_TRUE(received.isOk());
}

TEST(SenderTest, IncantationInProgressDoesNotRemovePending) {
    MockWebsocketClient ws;
    Sender sender(ws);

    int fireCount = 0;

    sender.expect("incantation", [&](const ServerMessage& msg) {
        (void)msg;
        fireCount++;
    });

    ServerMessage inProgress;
    inProgress.type   = MsgType::Response;
    inProgress.cmd    = "incantation";
    inProgress.status = "in_progress";

    sender.processResponse(inProgress);
    EXPECT_EQ(fireCount, 1);

    ServerMessage levelUp;
    levelUp.type   = MsgType::Response;
    levelUp.cmd    = "incantation";
    levelUp.status = "level_up";

    sender.processResponse(levelUp);
    EXPECT_EQ(fireCount, 2);
}

TEST(SenderTest, TimedOutEntryFiresErrorCallback) {
    MockWebsocketClient ws;
    Sender sender(ws);

    bool fired = false;
    ServerMessage received;

    sender.expect("voir", [&](const ServerMessage& msg) {
        fired = true;
        received = msg;
    });

    sender.checkTimeouts(0);

    EXPECT_TRUE(fired);
    EXPECT_EQ(received.cmd, "voir");
    EXPECT_EQ(received.status, "timeout");
    EXPECT_EQ(received.type, MsgType::Error);
}