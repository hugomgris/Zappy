#include "app/event/CommandEvent.hpp"

std::string CommandEvent::statusName() const {
	switch (status) {
		case CommandStatus::Success:		return "Success";
		case CommandStatus::Timeout:		return "Timeout";
		case CommandStatus::ProtocolError:	return "ProtocolError";
		case CommandStatus::ServerError:	return "ServerError";
		case CommandStatus::NetworkError:	return "NetworkError";
		case CommandStatus::Retrying:		return "Retrying";
		case CommandStatus::MalformedReply:	return "MalformedReply";
		case CommandStatus::UnexpectedReply:	return "UnexpectedReply";
		default:				return "Unknown";
	}
}
