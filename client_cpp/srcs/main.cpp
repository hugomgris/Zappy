#include "helpers/Logger.hpp"
#include "result.hpp"
#include "app/ClientBootstrap.hpp"
#include "app/ClientRunner.hpp"
#include "helpers/Parser.hpp"

#include <iostream>
int main(int argc, char **argv) {
	Logger::setLevel(LogLevel::Debug);
	Logger::info("C++ client bootstrap initialized");

	if (argc < 2) {
		Logger::error("Error: Invalid arguments");
		Parser::printUsage();
		return static_cast<int>(ErrorCode::InvalidArgs);
	}

	Arguments parsedArguments;
	ClientBootstrap bootstrap;
	const Result parseRes = bootstrap.parseAndValidate(argv, parsedArguments);

	// DEBUG
	Parser::printParsedArguments(parsedArguments);

	if (!parseRes.ok()) {
		Logger::error("Bootstrap failed: " + parseRes.message);
		if (parseRes.code == ErrorCode::InvalidArgs) Parser::printUsage();
		return static_cast<int>(parseRes.code);
	}

	ClientRunner runner(parsedArguments);
	const Result runRes = runner.run();

	if (!runRes.ok()) {
		Logger::error("Bootstrap failed: " + runRes.message);
		if (runRes.code == ErrorCode::InvalidArgs) Parser::printUsage();
		return static_cast<int>(runRes.code);
	}

	std::cout << "client_cpp bootstrap and transport smoke ok" << std::endl;
	return 0;
}