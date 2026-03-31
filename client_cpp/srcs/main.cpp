#include "logger.hpp"
#include "result.hpp"
#include "../incs/DataStructs.hpp"
#include "helpers/Parser.hpp"
#include "net/WebsocketClient.hpp"

#include <iostream>
#include <vector>
#include <openssl/opensslv.h>
#include <chrono>
#include <thread>

namespace {

int64_t nowMs() {
	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

Result tickUntilConnected(WebsocketClient& ws, int timeoutMs) {
	const int64_t deadline = nowMs() + timeoutMs;

	while (nowMs() < deadline) {
		const Result tickRes = ws.tick(nowMs());
		if (!tickRes.ok()) {
			return tickRes;
		}
		if (ws.isConnected()) {
			return Result::success();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	return Result::failure(ErrorCode::NetworkError, "Timed out waiting for WebSocket connection");
}

Result tickUntilTextFrame(WebsocketClient& ws, int timeoutMs, std::string& outText) {
	const int64_t deadline = nowMs() + timeoutMs;

	while (nowMs() < deadline) {
		const Result tickRes = ws.tick(nowMs());
		if (!tickRes.ok()) {
			return tickRes;
		}

		WebSocketFrame frame;
		const IoResult recvRes = ws.recvFrame(frame);
		if (recvRes.status == NetStatus::Ok && frame.opcode == WebSocketOpcode::Text) {
			outText.assign(frame.payload.begin(), frame.payload.end());
			return Result::success();
		}
		if (recvRes.status != NetStatus::WouldBlock && recvRes.status != NetStatus::Ok) {
			return Result::failure(ErrorCode::NetworkError, "Frame receive failed: " + recvRes.message);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	return Result::failure(ErrorCode::NetworkError, "Timed out waiting for text frame");
}

std::string buildLoginPayload(const Arguments& args) {
	return std::string("{\"type\":\"login\",\"key\":\"SOME_KEY\",\"role\":\"player\",\"team-name\":\"")
		+ args.teamName + "\"}";
}

Result runTransportSmoke(const Arguments& args) {
	WebsocketClient ws;

	Result connectRes = ws.connect(args.hostname, args.port, args.insecure);
	if (!connectRes.ok()) {
		return Result::failure(connectRes.code, "Connect start failed: " + connectRes.message);
	}

	connectRes = tickUntilConnected(ws, 5000);
	if (!connectRes.ok()) {
		return connectRes;
	}

	std::string firstServerMsg;
	Result recvRes = tickUntilTextFrame(ws, 3000, firstServerMsg);
	if (!recvRes.ok()) {
		return Result::failure(recvRes.code, "Did not receive initial server message: " + recvRes.message);
	}
	Logger::info("Initial server frame: " + firstServerMsg);

	const std::string loginPayload = buildLoginPayload(args);
	const IoResult sendRes = ws.sendText(loginPayload);
	if (sendRes.status != NetStatus::Ok) {
		return Result::failure(ErrorCode::NetworkError, "Failed to queue login frame: " + sendRes.message);
	}

	std::string loginReply;
	recvRes = tickUntilTextFrame(ws, 3000, loginReply);
	if (!recvRes.ok()) {
		return Result::failure(recvRes.code, "Did not receive login reply: " + recvRes.message);
	}
	Logger::info("Login reply frame: " + loginReply);

	ws.close();
	return Result::success();
}

} // namespace

Result bootstrap(char **argv, Arguments& parsedArguments) {
	Logger::setLevel(LogLevel::Debug);
	Logger::info("C++ client bootstrap initialized");

	// 1 Argunemnt parsing and evaluation
	Result parseResult = Parser::parseArguments(argv, parsedArguments);
	if (!parseResult.ok()) {
		return Result::failure(parseResult.code, parseResult.message);
	}

	parseResult = Parser::evaluateArguments(parsedArguments);
	if (!parseResult.ok()) {
		return Result::failure(parseResult.code, parseResult.message);
	}

	// 2 Initialize transport and run connection smoke
	Logger::info("OpenSSL version: " + std::string(OPENSSL_VERSION_TEXT));
	const Result transportResult = runTransportSmoke(parsedArguments);
	if (!transportResult.ok()) {
		return transportResult;
	}

	return Result::success();
}


int main(int argc, char **argv) {
	if (argc < 9 || argc > 11 || argc == 10) {
		Logger::error("Error: Invalid arguments");
		Parser::printUsage();
		return static_cast<int>(ErrorCode::InvalidArgs);
	}

	Arguments parsedArguments;
	const Result res = bootstrap(argv, parsedArguments);

	// DEBUG
	Parser::printParsedArguments(parsedArguments);

	if (!res.ok()) {
		Logger::error("Bootstrap failed: " + res.message);
		if (res.code == ErrorCode::InvalidArgs) Parser::printUsage();
		return static_cast<int>(res.code);
	}

	std::cout << "client_cpp bootstrap and transport smoke ok" << std::endl;
	return 0;
}