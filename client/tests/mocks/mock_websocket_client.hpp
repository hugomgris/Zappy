#pragma once

#include <gmock/gmock.h>
#include "net/WebsocketClient.hpp"

namespace zappy {

class MockWebsocketClient : public WebsocketClient {
public:
    MOCK_METHOD(Result, connect, (const std::string& host, int port, bool insecure), (override));
    MOCK_METHOD(Result, tick, (int64_t now_ms), (override));
    MOCK_METHOD(void, close, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(bool, isConnecting, (), (const, override));
    MOCK_METHOD(bool, isOpen, (), (const, override));
    MOCK_METHOD(IoResult, sendText, (const std::string& text), (override));
    MOCK_METHOD(IoResult, sendPing, (), (override));
    MOCK_METHOD(IoResult, sendPong, (), (override));
    MOCK_METHOD(IoResult, recvFrame, (WebSocketFrame& out_frame), (override));
    MOCK_METHOD(std::string, lastErrorString, (), (const, override));
    MOCK_METHOD(int, lastErrno, (), (const, override));
    MOCK_METHOD(std::size_t, sendQueueSize, (), (const, override));
    MOCK_METHOD(WsState, state, (), (const, override));
};

} // namespace zappy