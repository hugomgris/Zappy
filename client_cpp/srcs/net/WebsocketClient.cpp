#include "WebsocketClient.hpp"

#include "../helpers/Logger.hpp"
#include <openssl/sha.h>
#include <cstring>
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <random>

// Base64 encoding for WebSocket key
static std::string base64Encode(const std::vector<std::uint8_t>& data) {
    static constexpr const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    int val = 0;
    int valb = 0;

    for (uint8_t byte : data) {
        val = (val << 8) + byte;
        valb += 8;
        while (valb >= 6) {
            valb -= 6;
            result.push_back(base64_chars[(val >> valb) & 0x3F]);
        }
    }

    if (valb > 0) {
        result.push_back(base64_chars[(val << (6 - valb)) & 0x3F]);
    }

    while (result.size() % 4) {
        result.push_back('=');
    }

    return result;
}

// SHA-1 hash for WebSocket handshake validation
static std::string sha1Hash(const std::string& input) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash);

    std::vector<std::uint8_t> hash_vec(hash, hash + SHA_DIGEST_LENGTH);
    return base64Encode(hash_vec);
}

// Constructor / Destructor
WebsocketClient::WebsocketClient()
    : _secure_socket(std::make_unique<SecureSocket>()) {
}

WebsocketClient::~WebsocketClient() {
    close();
}

// Public Lifecycle
Result WebsocketClient::connect(const std::string& host, int port, bool insecure) {
    if (host.empty() || port <= 0 || port > 65535) {
        return Result::failure(ErrorCode::InvalidArgs, "Invalid host or port");
    }

    _host = host;
    _port = port;
    _insecure = insecure;
    _state = WsState::Connecting;
    _last_ping_time_ms = 0;

    // Start TLS connection
    Result res = _secure_socket->connectTo(host, port, insecure);
    if (!res.ok()) {
        _state = WsState::Disconnected;
        _last_error = res.message;
        return res;
    }

    Logger::info("WebSocket: Starting connection to " + host + ":" + std::to_string(port));
    return Result::success();
}

Result WebsocketClient::tick(int64_t now_ms) {
    if (_state == WsState::Disconnected || _state == WsState::Closed) {
        return Result::failure(ErrorCode::InternalError, "WebSocket not connected");
    }

    // Poll TCP/TLS connection if still connecting
    if (_state == WsState::Connecting) {
        Result res = _secure_socket->pollConnect(0);
        if (!res.ok()) {
            _state = WsState::Disconnected;
            return res;
        }

        if (_secure_socket->isConnected()) {
            // TLS handshake is complete, proceed to WebSocket HTTP upgrade
            _state = WsState::Handshaking;
        }

        if (_state == WsState::Handshaking) {
            Result res = performHandshake();
            if (!res.ok()) {
                _state = WsState::Disconnected;
                return res;
            }
            _state = WsState::Connected;
            Logger::info("WebSocket: Handshake complete, connection established");
        }
    }

    if (_state != WsState::Connected) {
        return Result::success();
    }

    // Flush pending sends
    Result flush_res = flushSendQueue();
    if (!flush_res.ok()) {
        _state = WsState::Closed;
        return flush_res;
    }

    // Read incoming data
    std::vector<std::uint8_t> tmp_buf;
    IoResult read_res = _secure_socket->tlsRead(tmp_buf, 4096);

    if (read_res.status == NetStatus::Ok && read_res.bytes > 0) {
        _read_buffer.insert(_read_buffer.end(), tmp_buf.begin(), tmp_buf.end());
    } else if (read_res.status == NetStatus::ConnectionClosed) {
        _state = WsState::Closed;
        return Result::failure(ErrorCode::NetworkError, "Peer closed connection");
    } else if (read_res.status != NetStatus::WouldBlock && read_res.status != NetStatus::Ok) {
        _state = WsState::Closed;
        _last_error = read_res.message;
        return Result::failure(ErrorCode::NetworkError, read_res.message);
    }

    // Ping keepalive
    updatePingTimer(now_ms);
    if (shouldSendPing(now_ms)) {
        sendPing();
    }

    return Result::success();
}

void WebsocketClient::close() {
    _send_queue.clear();
    _read_buffer.clear();
    _frame_parse_offset = 0;

    if (_secure_socket) {
        _secure_socket->close();
    }

    _state = WsState::Closed;
}

// Public Status
bool WebsocketClient::isConnected() const {
    return _state == WsState::Connected;
}

bool WebsocketClient::isConnecting() const {
    return _state == WsState::Connecting || _state == WsState::Handshaking;
}

bool WebsocketClient::isOpen() const {
    return _state != WsState::Disconnected && _state != WsState::Closed;
}

// Public Send/Receive
IoResult WebsocketClient::sendText(const std::string& text) {
    IoResult res{};

    if (!isConnected()) {
        res.status = NetStatus::InvalidState;
        res.message = "WebSocket not connected";
        return res;
    }

    WebSocketFrame frame = FrameCodec::createTextFrame(text);
    std::vector<std::uint8_t> encoded;

    Result encode_res = FrameCodec::encodeFrame(frame, encoded);
    if (!encode_res.ok()) {
        res.status = NetStatus::NetworkError;
        res.message = encode_res.message;
        return res;
    }

    _send_queue.push_back(encoded);
    res.status = NetStatus::Ok;
    res.bytes = encoded.size();

    Logger::debug("WebSocket: Queued text frame (" + std::to_string(encoded.size()) + " bytes)");
    return res;
}

IoResult WebsocketClient::sendPing() {
    IoResult res{};

    if (!isConnected()) {
        res.status = NetStatus::InvalidState;
        res.message = "WebSocket not connected";
        return res;
    }

    WebSocketFrame frame = FrameCodec::createPingFrame();
    std::vector<std::uint8_t> encoded;

    Result encode_res = FrameCodec::encodeFrame(frame, encoded);
    if (!encode_res.ok()) {
        res.status = NetStatus::NetworkError;
        res.message = encode_res.message;
        return res;
    }

    _send_queue.push_back(encoded);
    res.status = NetStatus::Ok;
    res.bytes = encoded.size();

    Logger::debug("WebSocket: Queued ping frame");
    return res;
}

IoResult WebsocketClient::sendPong() {
    IoResult res{};

    if (!isConnected()) {
        res.status = NetStatus::InvalidState;
        res.message = "WebSocket not connected";
        return res;
    }

    WebSocketFrame frame = FrameCodec::createPongFrame();
    std::vector<std::uint8_t> encoded;

    Result encode_res = FrameCodec::encodeFrame(frame, encoded);
    if (!encode_res.ok()) {
        res.status = NetStatus::NetworkError;
        res.message = encode_res.message;
        return res;
    }

    _send_queue.push_back(encoded);
    res.status = NetStatus::Ok;
    res.bytes = encoded.size();

    return res;
}

IoResult WebsocketClient::recvFrame(WebSocketFrame& out_frame) {
    IoResult res{};

    if (!isConnected()) {
        res.status = NetStatus::InvalidState;
        res.message = "WebSocket not connected";
        return res;
    }

    Result decode_res = FrameCodec::decodeFrame(_read_buffer, _frame_parse_offset, out_frame);

    if (!decode_res.ok()) {
        // Check if it's a "need more data" situation
        if (decode_res.message.find("Not enough data") != std::string::npos ||
            decode_res.message.find("Incomplete") != std::string::npos) {
            res.status = NetStatus::WouldBlock;
            res.message = "Waiting for more frame data";
            return res;
        }

        res.status = NetStatus::NetworkError;
        res.message = decode_res.message;
        return res;
    }

    // Successfully decoded a frame
    // Clean up read buffer if we've consumed some data
    if (_frame_parse_offset > 0) {
        _read_buffer.erase(_read_buffer.begin(), _read_buffer.begin() + _frame_parse_offset);
        _frame_parse_offset = 0;
    }

    res.status = NetStatus::Ok;
    res.bytes = out_frame.payload.size();

    return res;
}

// Query
std::string WebsocketClient::lastErrorString() const {
    return _last_error;
}

int WebsocketClient::lastErrno() const {
    return _last_errno;
}

std::size_t WebsocketClient::sendQueueSize() const {
    return _send_queue.size();
}

// Private Helpers
Result WebsocketClient::performHandshake() {
    if (!_secure_socket || !_secure_socket->isConnected()) {
        return Result::failure(ErrorCode::InternalError, "SecureSocket not connected");
    }

    // Generate random key for handshake
    std::random_device rd;
    std::vector<uint8_t> random_bytes(16);
    for (auto& byte : random_bytes) {
        byte = rd() % 256;
    }
    std::string ws_key = base64Encode(random_bytes);

    // Build HTTP upgrade request
    std::ostringstream oss;
    oss << "GET / HTTP/1.1\r\n"
        << "Host: " << _host << ":" << _port << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << ws_key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "User-Agent: zappy-client\r\n"
        << "\r\n";

    std::string request = oss.str();
    std::vector<std::uint8_t> request_bytes(request.begin(), request.end());

    // Send HTTP upgrade request
    Logger::debug("WebSocket: Sending HTTP upgrade request (" + std::to_string(request_bytes.size()) + " bytes)");
    IoResult write_res = _secure_socket->tlsWrite(request_bytes, 0);
    if (write_res.status != NetStatus::Ok) {
        return Result::failure(ErrorCode::NetworkError, "Failed to send WebSocket upgrade: " + write_res.message);
    }
    Logger::debug("WebSocket: HTTP upgrade request sent successfully");

    // Receive HTTP response
    Logger::debug("WebSocket: Waiting for HTTP 101 response...");
    std::vector<std::uint8_t> response_buf;
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::vector<std::uint8_t> tmp;  // Start EMPTY, not with 1024 zeros!
        IoResult read_res = _secure_socket->tlsRead(tmp, 1024);

        if (read_res.status == NetStatus::Ok && read_res.bytes > 0) {
            // tmp now contains the data read (APPENDED by tlsRead)
            response_buf.insert(response_buf.end(), tmp.begin(), tmp.end());
            Logger::debug("WebSocket: Attempt " + std::to_string(attempt) + " read " + std::to_string(read_res.bytes) + " bytes, total: " + std::to_string(response_buf.size()));

            // Check if we have the full HTTP response (ends with \r\n\r\n)
            std::string response_str(response_buf.begin(), response_buf.end());
            size_t term_pos = response_str.find("\r\n\r\n");
            Logger::debug("WebSocket: Searching for terminator in " + std::to_string(response_str.size()) + " bytes, found at: " + (term_pos == std::string::npos ? "npos" : std::to_string(term_pos)));
            
            if (term_pos != std::string::npos) {
                // Found terminator, trim buffer
                size_t header_end = term_pos + 4;  // Include the \r\n\r\n
                Logger::debug("WebSocket: Found terminator! Trimming from " + std::to_string(response_buf.size()) + " to " + std::to_string(header_end) + " bytes");
                response_buf.resize(header_end);
                break;  // Got full header
            }
        } else if (read_res.status == NetStatus::ConnectionClosed) {
            Logger::debug("WebSocket: Connection closed on attempt " + std::to_string(attempt));
            return Result::failure(ErrorCode::NetworkError, "Peer closed during handshake");
        } else if (read_res.status == NetStatus::WouldBlock) {
            // Logger::debug("WebSocket: WouldBlock on attempt " + std::to_string(attempt));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            Logger::debug("WebSocket: Read error on attempt " + std::to_string(attempt) + ": " + read_res.message);
            return Result::failure(ErrorCode::NetworkError, "Read error during handshake: " + read_res.message);
        }
    }
    Logger::debug("WebSocket: Response reading loop completed, buffer size: " + std::to_string(response_buf.size()));

    // Validate HTTP 101 response
    std::string response_str(response_buf.begin(), response_buf.end());
    Logger::debug("WebSocket: HTTP response received (" + std::to_string(response_buf.size()) + " bytes)");
    
    // Raw byte dump for debugging
    std::ostringstream byte_dump;
    for (size_t i = 0; i < std::min(response_buf.size(), size_t(140)); ++i) {
        uint8_t b = response_buf[i];
        if (b >= 32 && b < 127) {
            byte_dump << (char)b;
        } else if (b == '\r') {
            byte_dump << "\\r";
        } else if (b == '\n') {
            byte_dump << "\\n";
        } else {
            byte_dump << "[" << (int)b << "]";
        }
    }
    Logger::debug("WebSocket: Bytes: " + byte_dump.str());
    
    if (response_str.find("101") == std::string::npos) {
        Logger::error("WebSocket: Response does not contain '101'.");
        return Result::failure(ErrorCode::ProtocolError, "Server did not respond with HTTP 101");
    }

    // Calculate expected Sec-WebSocket-Accept
    std::string challenge = ws_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string expected_accept = sha1Hash(challenge);

    if (response_str.find("Sec-WebSocket-Accept: " + expected_accept) == std::string::npos) {
        Logger::warn("WebSocket: Server Sec-WebSocket-Accept mismatch, but continuing");
    }

    Logger::info("WebSocket: HTTP 101 Upgrade successful");
    return Result::success();
}

Result WebsocketClient::flushSendQueue() {
    while (!_send_queue.empty()) {
        const auto& frame_bytes = _send_queue.front();
        
        // Try to send the frame
        // For simplicity, send all at once; real impl would handle partial sends
        IoResult write_res = _secure_socket->tlsWrite(frame_bytes, 0);

        if (write_res.status == NetStatus::Ok && write_res.bytes > 0) {
            if (write_res.bytes == frame_bytes.size()) {
                // Fully sent
                _send_queue.pop_front();
            } else {
                // Partial send - would need to track offset, but for now bail
                return Result::failure(ErrorCode::InternalError, "Partial frame send not yet supported");
            }
        } else if (write_res.status == NetStatus::WouldBlock) {
            // Can't send more right now, try again later
            break;
        } else if (write_res.status == NetStatus::ConnectionClosed) {
            return Result::failure(ErrorCode::NetworkError, "Connection closed while sending");
        } else {
            return Result::failure(ErrorCode::NetworkError, "Write error: " + write_res.message);
        }
    }

    return Result::success();
}

void WebsocketClient::updatePingTimer(int64_t now_ms) {
    if (_last_ping_time_ms == 0) {
        _last_ping_time_ms = now_ms;
    }
}

bool WebsocketClient::shouldSendPing(int64_t now_ms) {
    if (_last_ping_time_ms == 0) {
        return false;
    }

    int64_t elapsed_ms = now_ms - _last_ping_time_ms;
    if (elapsed_ms > PING_INTERVAL_SECONDS * 1000) {
        _last_ping_time_ms = now_ms;
        return true;
    }

    return false;
}

