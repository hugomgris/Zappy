#include "app/ClientBootstrap.hpp"
#include "helpers/Parser.hpp"

Result ClientBootstrap::parseAndValidate(char** argv, Arguments& parsedArguments) const {
	Result parseResult = Parser::parseArguments(argv, parsedArguments);
	if (!parseResult.ok()) {
		return Result::failure(parseResult.code, parseResult.message);
	}

	parseResult = Parser::evaluateArguments(parsedArguments);
	if (!parseResult.ok()) {
		return Result::failure(parseResult.code, parseResult.message);
	}

	return Result::success();
}
