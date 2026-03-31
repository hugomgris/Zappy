#include "Parser.hpp"

Result Parser::parseArguments(char** argv, Arguments& parsedArguments) {
	std::vector<std::string> args = {};

	for (int i = 1; argv[i]; ++i) {
		args.push_back(std::string(argv[i]));
	}

	if (args.size() < 8 || args.size() > 10 || args.size() == 9) {
		// Double check, just in case
		return Result::failure(ErrorCode::InvalidArgs, "Error: Invalid arguments");
	}

	for (int i = 0; i < 8; ++i) {
		if (args[i][0] == '-') {
			if (args[i][1] == 'n') {
				parsedArguments.teamName = args[i + 1];
			} else if (args[i][1] == 'p') {
				parsedArguments.port = std::stoi(args[i + 1]);
			} else if (args[i][1] == 'h') {
				parsedArguments.hostname = args[i + 1];
			} else if (args[i][1] == 'c') {
				parsedArguments.clientCount = std::stoi(args[i + 1]);
			} else {
				return Result::failure(ErrorCode::InvalidArgs, "Error: Unrecognized argument: " + args[i]);
			}
		}
	}

	if (args.size() == 10) {
		if (args[8] != "--insecure") {
			return Result::failure(ErrorCode::InvalidArgs, "Error: Unrecognized argument: " + args[9]);
		}
		parsedArguments.insecure = (args[9] == "true") ? true : false;
	}

	return Result::success();
}

Result Parser::evaluateArguments(Arguments& parsedArguments) {
	if (parsedArguments.teamName.empty()) {
		return Result::failure(ErrorCode::MissingArgs, "Error: Missing required argument: team name");
	} else if (parsedArguments.port == -1) {
		return Result::failure(ErrorCode::MissingArgs, "Error: Missing required argument: port");
	}
	
	return Result::success();
}

void Parser::printParsedArguments(Arguments& parsedArguments) {
	Logger::debug("Parsed arguments:");
	Logger::debug("teamname: " + parsedArguments.teamName);
	Logger::debug("port: " + std::to_string(parsedArguments.port));
	Logger::debug("hostname: " + parsedArguments.hostname);
	Logger::debug("client count: " + std::to_string(parsedArguments.clientCount));
	Logger::debug(std::string("insecure: ") + (parsedArguments.insecure ? "true" : "false"));
}

void Parser::printUsage() {
	std::cout << "Usage: ./client -n <team> -p <port> [-h <hostname>]" << std::endl
				<< "-n <teamName>  : Name of the team (required)" << std::endl
				<< "-p <port>       : Port number (required)" << std::endl
				<< "-h <hostname>   : Hostname (default: localhost)" << std::endl
				<< "-c <client_cnt> : Number of clients to run (default: 1)" << std::endl;
}