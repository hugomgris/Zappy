#pragma once

#include "app/command/ResourceType.hpp"
#include <cstdint>
#include <string>
#include <variant>
#include <memory>
#include <functional>

// Forward declare
class IntentRequest;

// Abstract base for all intent requests
class IntentRequest {
	public:
		virtual ~IntentRequest() = default;
		virtual std::string description() const = 0;
};

// Concrete intent: Request a view scan
class RequestVoir : public IntentRequest {
	public:
		RequestVoir() = default;
		std::string description() const override { return "RequestVoir"; }
};

// Concrete intent: Request inventory check
class RequestInventaire : public IntentRequest {
	public:
		RequestInventaire() = default;
		std::string description() const override { return "RequestInventaire"; }
};

// Concrete intent: Request to take a resource
class RequestTake : public IntentRequest {
	public:
		explicit RequestTake(ResourceType resource) : resource(resource) {}
		std::string description() const override;

		ResourceType resource;
};

// Concrete intent: Request to place a resource
class RequestPlace : public IntentRequest {
	public:
		explicit RequestPlace(ResourceType resource) : resource(resource) {}
		std::string description() const override;

		ResourceType resource;
};

// Concrete intent: Move forward
class RequestMove : public IntentRequest {
	public:
		RequestMove() = default;
		std::string description() const override { return "RequestMove"; }
};

// Concrete intent: Turn right
class RequestTurnRight : public IntentRequest {
	public:
		RequestTurnRight() = default;
		std::string description() const override { return "RequestTurnRight"; }
};

// Concrete intent: Turn left
class RequestTurnLeft : public IntentRequest {
	public:
		RequestTurnLeft() = default;
		std::string description() const override { return "RequestTurnLeft"; }
};

// Concrete intent: Broadcast a message
class RequestBroadcast : public IntentRequest {
	public:
		explicit RequestBroadcast(const std::string& msg) : message(msg) {}
		std::string description() const override;

		std::string message;
};

// Concrete intent: Trigger incantation
class RequestIncantation : public IntentRequest {
	public:
		RequestIncantation() = default;
		std::string description() const override { return "RequestIncantation"; }
};

// Concrete intent: Request fork (egg creation)
class RequestFork : public IntentRequest {
	public:
		RequestFork() = default;
		std::string description() const override { return "RequestFork"; }
};

// Concrete intent: Request available connection slots
class RequestConnectNbr : public IntentRequest {
	public:
		RequestConnectNbr() = default;
		std::string description() const override { return "RequestConnectNbr"; }
};

// Intent result: Outcome of a completed intent
struct IntentResult {
	std::uint64_t	id = 0;
	std::string		intentType;
	bool			succeeded = false;
	std::string		details;

	IntentResult() = default;
	IntentResult(std::uint64_t id, const std::string& type, bool success, const std::string& det)
		: id(id), intentType(type), succeeded(success), details(det) {}
};

// Observer callback for intent completion
using IntentCompletionHandler = std::function<void(const IntentResult&)>;
