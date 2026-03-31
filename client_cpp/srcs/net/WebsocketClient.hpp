#pragma once

#include "SecureSocket.hpp"
#include "FrameCodec.hpp"
#include "../../incs/result.hpp"
#include <deque>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

class WebsocketClient {
private:
    // State machine
    enum class WsState {
        Disconnected,
        Connecting,
        Handshaking,
        Connected,
        Closed
    };

    std::unique_ptr<SecureSocket> _secure_socket;
    WsState _state = WsState::Disconnected;
    std::string _host;
    int _port = -1;
    bool _insecure = false;

    // Buffers
    std::vector<std::uint8_t> _read_buffer;
    std::deque<std::vector<std::uint8_t>> _send_queue;

    // Frame state
    std::size_t _frame_parse_offset = 0;

    // Ping/pong
    static constexpr int PING_INTERVAL_SECONDS = 45;
    int64_t _last_ping_time_ms = 0;

    // Error tracking
    std::string _last_error;
    int _last_errno = 0;

    // Private helpers
    Result performHandshake();
    Result flushSendQueue();
    Result processPendingFrames();
    void updatePingTimer(int64_t now_ms);
    bool shouldSendPing(int64_t now_ms);

public:
    WebsocketClient();
    ~WebsocketClient();

    WebsocketClient(const WebsocketClient&) = delete;
    WebsocketClient& operator=(const WebsocketClient&) = delete;

    // Lifecycle
    Result connect(const std::string& host, int port, bool insecure = false);
    Result tick(int64_t now_ms);
    void close();

    // Status
    bool isConnected() const;
    bool isConnecting() const;
    bool isOpen() const;

    // Send operation (enqueues frame, returns immediately)
    IoResult sendText(const std::string& text);
    IoResult sendPing();
    IoResult sendPong();

    // Receive (returns next complete frame, or WantMore)
    IoResult recvFrame(WebSocketFrame& out_frame);

    // Error info
    std::string lastErrorString() const;
    int lastErrno() const;

    // Pending send queue size (for backpressure monitoring)
    std::size_t sendQueueSize() const;
};

