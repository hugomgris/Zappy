#pragma once

#include "app/event/CommandEvent.hpp"
#include "app/intent/Intent.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class DecisionPolicy {
	public:
		virtual ~DecisionPolicy() = default;

		virtual std::vector<std::shared_ptr<IntentRequest>> onTick(std::int64_t nowMs) = 0;
		virtual std::vector<std::shared_ptr<IntentRequest>> onCommandEvent(
			std::int64_t nowMs,
			const CommandEvent& event,
			const std::optional<IntentResult>& intentResult
		) = 0;
};
