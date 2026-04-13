#include "helpers/Logger.hpp"
#include "helpers/Parser.hpp"
#include "../incs/Result.hpp"
#include <iostream>
#include <csignal>
#include <cstring>
#include <getopt.h>

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	Logger::info("hello zappy");
	Logger::setLevel(LogLevel::Debug);

	const char *inputArguments[12] = {"Client", "-n", "alpha", "-p", "8674", "-h", "localhost", "-c", "1", "--insecure", "true"};

	Arguments args;
	Result res = Parser::parseArguments(const_cast<char**>(inputArguments), args);

	Logger::info(std::to_string(static_cast<int>(res.code)));

	return 0;
}