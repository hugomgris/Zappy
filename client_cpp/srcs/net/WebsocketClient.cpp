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

WebsocketClient::WebsocketClient()
    : _secure_socket(std::make_unique<SecureSocket>()) {
}

WebsocketClient::~WebsocketClient() {
    close();
}

Result WebsocketClient::connect(const std::string& host, int port, bool insecure) {
    if (host.empty() || port <= 0 || port > 65535) {
        return Result::failure(ErrorCode::InvalidArgs, "Invalid host or port");
    }

    _host = host;
    _port = port;
    _insecure = insecure;
    _state = WsState::Connecting;
    _last_ping_time_ms = 0;

    // this is where the TLS connection gets started
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

    // this is were a connecting status is polled to finish connectiona ttempts
    if (_state == WsState::Connecting) {
        Result res = _secure_socket->pollConnect(0);
        if (!res.ok()) {
            _state = WsState::Disconnected;
            return res;
        }

        if (_secure_socket->isConnected()) {
            // if this point is reached, TSL handshake succedded and the scoket goes to HTTP upgrade
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

    Result flush_res = flushSendQueue();
    if (!flush_res.ok()) {
        _state = WsState::Closed;
        return flush_res;
    }

    // read
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

    (void)now_ms;

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

bool WebsocketClient::isConnected() const {
    return _state == WsState::Connected;
}

bool WebsocketClient::isConnecting() const {
    return _state == WsState::Connecting || _state == WsState::Handshaking;
}

bool WebsocketClient::isOpen() const {
    return _state != WsState::Disconnected && _state != WsState::Closed;
}

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
        // need more data typa check
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

    // When this point is reached, a frame has been successfully decoded
    // buffer needs to be cleaned
    if (_frame_parse_offset > 0) {
        _read_buffer.erase(_read_buffer.begin(), _read_buffer.begin() + _frame_parse_offset);
        _frame_parse_offset = 0;
    }

    res.status = NetStatus::Ok;
    res.bytes = out_frame.payload.size();

    return res;
}

std::string WebsocketClient::lastErrorString() const {
    return _last_error;
}

int WebsocketClient::lastErrno() const {
    return _last_errno;
}

std::size_t WebsocketClient::sendQueueSize() const {
    return _send_queue.size();
}

Result WebsocketClient::performHandshake() {
    if (!_secure_socket || !_secure_socket->isConnected()) {
        return Result::failure(ErrorCode::InternalError, "SecureSocket not connected");
    }

    // random key for handshake
    std::random_device rd;
    std::vector<uint8_t> random_bytes(16);
    for (auto& byte : random_bytes) {
        byte = rd() % 256;
    }
    std::string ws_key = base64Encode(random_bytes);

    // HTTP upgrade request
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

    Logger::debug("WebSocket: Sending HTTP upgrade request (" + std::to_string(request_bytes.size()) + " bytes)");
    IoResult write_res = _secure_socket->tlsWrite(request_bytes, 0);
    if (write_res.status != NetStatus::Ok) {
        return Result::failure(ErrorCode::NetworkError, "Failed to send WebSocket upgrade: " + write_res.message);
    }
    Logger::debug("WebSocket: HTTP upgrade request sent successfully");

    // HTTP response recieve
    Logger::debug("WebSocket: Waiting for HTTP 101 response...");
    std::vector<std::uint8_t> response_buf;
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::vector<std::uint8_t> tmp;
        IoResult read_res = _secure_socket->tlsRead(tmp, 1024);

        if (read_res.status == NetStatus::Ok && read_res.bytes > 0) {
            response_buf.insert(response_buf.end(), tmp.begin(), tmp.end());
            Logger::debug("WebSocket: Attempt " + std::to_string(attempt) + " read " + std::to_string(read_res.bytes) + " bytes, total: " + std::to_string(response_buf.size()));

            std::string response_str(response_buf.begin(), response_buf.end());
            size_t term_pos = response_str.find("\r\n\r\n");
            Logger::debug("WebSocket: Searching for terminator in " + std::to_string(response_str.size()) + " bytes, found at: " + (term_pos == std::string::npos ? "npos" : std::to_string(term_pos)));
            
            if (term_pos != std::string::npos) {
                size_t header_end = term_pos + 4;
                Logger::debug("WebSocket: Found terminator! Trimming from " + std::to_string(response_buf.size()) + " to " + std::to_string(header_end) + " bytes");
                response_buf.resize(header_end);
                break;  // full header
            }
        } else if (read_res.status == NetStatus::ConnectionClosed) {
            Logger::debug("WebSocket: Connection closed on attempt " + std::to_string(attempt));
            return Result::failure(ErrorCode::NetworkError, "Peer closed during handshake");
        } else if (read_res.status == NetStatus::WouldBlock) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            Logger::debug("WebSocket: Read error on attempt " + std::to_string(attempt) + ": " + read_res.message);
            return Result::failure(ErrorCode::NetworkError, "Read error during handshake: " + read_res.message);
        }
    }
    Logger::debug("WebSocket: Response reading loop completed, buffer size: " + std::to_string(response_buf.size()));

    // HTTP 101 response validation
    std::string response_str(response_buf.begin(), response_buf.end());
    Logger::debug("WebSocket: HTTP response received (" + std::to_string(response_buf.size()) + " bytes)");
    
    // debugging raw byte dump
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

    // Sec-Websocket-Accpet
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
        
        IoResult write_res = _secure_socket->tlsWrite(frame_bytes, 0);

        if (write_res.status == NetStatus::Ok && write_res.bytes > 0) {
            if (write_res.bytes == frame_bytes.size()) {
                _send_queue.pop_front();
            } else {
                return Result::failure(ErrorCode::InternalError, "Partial frame send not yet supported");
            }
        } else if (write_res.status == NetStatus::WouldBlock) {
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
    (void)now_ms;
}

bool WebsocketClient::shouldSendPing(int64_t now_ms) {
    (void)now_ms;
    return false;
}