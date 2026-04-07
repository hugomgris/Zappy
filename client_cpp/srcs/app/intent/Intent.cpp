#include "app/intent/Intent.hpp"

std::string RequestTake::description() const {
	return "RequestTake(" + std::string(toProtocolString(resource)) + ")";
}

std::string RequestPlace::description() const {
	return "RequestPlace(" + std::string(toProtocolString(resource)) + ")";
}

std::string RequestBroadcast::description() const {
	return "RequestBroadcast(\"" + message + "\")";
}
