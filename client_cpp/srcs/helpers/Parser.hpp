#pragma once

#include "../../incs/result.hpp"
#include "../../incs/DataStructs.hpp"
#include "../../incs/logger.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <sstream>

class Parser {
	public:
		static Result parseArguments(char** argv, Arguments& parsedArguments);
		static Result evaluateArguments(Arguments& parsedArguments);
		static void printParsedArguments(Arguments& parsedArguments);
		static void printUsage();
};