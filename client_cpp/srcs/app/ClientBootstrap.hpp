#pragma once

#include "DataStructs.hpp"
#include "result.hpp"

class ClientBootstrap {
	public:
		Result parseAndValidate(char** argv, Arguments& parsedArguments) const;
};
