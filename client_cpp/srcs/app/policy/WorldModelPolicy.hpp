#pragma once

#include "app/policy/DecisionPolicy.hpp"
#include "app/policy/IncantationStrategy.hpp"
#include "app/policy/NavigationPlanner.hpp"
#include "app/policy/ResourceStrategy.hpp"
#include "app/policy/TeamBroadcastProtocol.hpp"
#include "app/state/WorldState.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

class WorldModelPolicy : public DecisionPolicy {
	private:
		WorldState _state;
		NavigationPlanner _planner;
		ResourceStrategy _resourceStrategy;
		IncantationStrategy _incantationStrategy;
		std::deque<NavigationStep> _activePlan;
		std::int64_t _visionRefreshMs;
		std::int64_t _inventoryRefreshMs;
		std::int64_t _incantationRetryDelayMs;
		std::int64_t _summonCooldownMs;
		std::int64_t _teamResponseCooldownMs;
		std::int64_t _roleBroadcastIntervalMs;
		std::int64_t _lastIncantationAtMs;
		std::int64_t _lastSummonAtMs;
		std::int64_t _lastTeamResponseAtMs;
		std::int64_t _lastRoleBroadcastAtMs;
		int _assistFoodThreshold;
		int _shareFoodThreshold;

	public:
		explicit WorldModelPolicy(
			std::int64_t visionRefreshMs = 5000,
			std::int64_t inventoryRefreshMs = 7000,
			std::int64_t incantationRetryDelayMs = 1000,
			std::int64_t summonCooldownMs = 5000,
			int incantationMinFood = 2,
			int incantationMinPlayers = 2,
			std::int64_t teamResponseCooldownMs = 4000,
			std::int64_t roleBroadcastIntervalMs = 30000,
			int assistFoodThreshold = 10,
			int shareFoodThreshold = 16
		);

		const WorldState& state() const;

		std::vector<std::shared_ptr<IntentRequest>> onTick(std::int64_t nowMs) override;
		std::vector<std::shared_ptr<IntentRequest>> onCommandEvent(
			std::int64_t nowMs,
			const CommandEvent& event,
			const std::optional<IntentResult>& intentResult
		) override;
};
