#pragma once

#include "app/policy/DecisionPolicy.hpp"

class PeriodicScanPolicy : public DecisionPolicy {
	private:
		std::int64_t _voirIntervalMs;
		std::int64_t _inventoryInitialDelayMs;
		std::int64_t _inventoryIntervalMs;
		std::int64_t _nextVoirAtMs;
		std::int64_t _nextInventoryAtMs;
		bool _initialized;

	public:
		PeriodicScanPolicy(
			std::int64_t voirIntervalMs,
			std::int64_t inventoryInitialDelayMs,
			std::int64_t inventoryIntervalMs
		);

		std::vector<std::shared_ptr<IntentRequest>> onTick(std::int64_t nowMs) override;
		std::vector<std::shared_ptr<IntentRequest>> onCommandEvent(
			std::int64_t nowMs,
			const CommandEvent& event,
			const std::optional<IntentResult>& intentResult
		) override;
};
