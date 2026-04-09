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
		struct FailedTakeContext {
			int x = 0;
			int y = 0;
			std::optional<ResourceType> resource;
			std::int64_t atMs = 0;
			bool needsFreshVision = false;
		};

		static constexpr std::int64_t kTakeRetryCooldownMs = 1200;
		static constexpr std::int64_t kTakeVisionFreshnessMs = 1500;

		WorldState _state;
		NavigationPlanner _planner;
		ResourceStrategy _resourceStrategy;
		IncantationStrategy _incantationStrategy;
		std::deque<NavigationStep> _activePlan;
		std::optional<NavigationTarget> _activeHarvestTarget;
		std::optional<FailedTakeContext> _lastFailedTake;
		std::optional<int> _meetingDirection;
		std::int64_t _visionRefreshMs;
		std::int64_t _inventoryRefreshMs;
		std::int64_t _incantationRetryDelayMs;
		std::int64_t _summonCooldownMs;
		std::int64_t _teamResponseCooldownMs;
		std::int64_t _roleBroadcastIntervalMs;
		std::int64_t _lastIncantationAtMs;
		std::int64_t _lastSummonAtMs;
		std::int64_t _lastForkAtMs;
		std::int64_t _lastTeamResponseAtMs;
		std::int64_t _lastRoleBroadcastAtMs;
		std::int64_t _lastConnectNbrAtMs;
		std::int64_t _meetingUntilAtMs;
		bool _visionRefreshRequested;
		bool _inventoryRefreshRequested;
		bool _pendingConnectNbrPoll;
		int _teamSlotsAvailable;
		int _assistFoodThreshold;
		int _shareFoodThreshold;
		int _forkFoodThreshold;
		std::int64_t _forkCooldownMs;
		int _forksIssued;
		int _maxForks;
		bool _victoryReached;

	public:
		explicit WorldModelPolicy(
			std::int64_t visionRefreshMs = 5000,
			std::int64_t inventoryRefreshMs = 7000,
			std::int64_t incantationRetryDelayMs = 1000,
			std::int64_t summonCooldownMs = 5000,
			int incantationMinFood = 2,
			int incantationMinPlayers = 1,
			std::int64_t teamResponseCooldownMs = 4000,
			std::int64_t roleBroadcastIntervalMs = 30000,
			int assistFoodThreshold = 10,
			int shareFoodThreshold = 16,
			int forkFoodThreshold = 10,
			std::int64_t forkCooldownMs = 60000,
			int maxForks = 1,
			int initialTeamSlots = -1
		);

		const WorldState& state() const;
		bool victoryReached() const;

		std::vector<std::shared_ptr<IntentRequest>> onTick(std::int64_t nowMs) override;
		std::vector<std::shared_ptr<IntentRequest>> onCommandEvent(
			std::int64_t nowMs,
			const CommandEvent& event,
			const std::optional<IntentResult>& intentResult
		) override;
};
