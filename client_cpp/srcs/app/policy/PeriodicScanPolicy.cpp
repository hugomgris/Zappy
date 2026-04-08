#include "app/policy/PeriodicScanPolicy.hpp"

PeriodicScanPolicy::PeriodicScanPolicy(
	std::int64_t voirIntervalMs,
	std::int64_t inventoryInitialDelayMs,
	std::int64_t inventoryIntervalMs
)
	: _voirIntervalMs(voirIntervalMs),
	  _inventoryInitialDelayMs(inventoryInitialDelayMs),
	  _inventoryIntervalMs(inventoryIntervalMs),
	  _nextVoirAtMs(0),
	  _nextInventoryAtMs(0),
	  _initialized(false) {}

std::vector<std::shared_ptr<IntentRequest>> PeriodicScanPolicy::onTick(std::int64_t nowMs) {
	std::vector<std::shared_ptr<IntentRequest>> intents;

	if (!_initialized) {
		_nextVoirAtMs = nowMs + _voirIntervalMs;
		_nextInventoryAtMs = nowMs + _inventoryInitialDelayMs;
		_initialized = true;
	}

	if (nowMs >= _nextVoirAtMs) {
		intents.push_back(std::make_shared<RequestVoir>());
		_nextVoirAtMs = nowMs + _voirIntervalMs;
	}

	if (nowMs >= _nextInventoryAtMs) {
		intents.push_back(std::make_shared<RequestInventaire>());
		_nextInventoryAtMs = nowMs + _inventoryIntervalMs;
	}

	return intents;
}

std::vector<std::shared_ptr<IntentRequest>> PeriodicScanPolicy::onCommandEvent(
	std::int64_t /*nowMs*/,
	const CommandEvent& /*event*/,
	const std::optional<IntentResult>& /*intentResult*/
) {
	return {};
}
