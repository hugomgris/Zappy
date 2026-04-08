#include "app/policy/WorldModelPolicy.hpp"

#include "app/command/CommandType.hpp"
#include "helpers/Logger.hpp"

WorldModelPolicy::WorldModelPolicy(
	std::int64_t visionRefreshMs,
	std::int64_t inventoryRefreshMs,
	std::int64_t incantationRetryDelayMs,
	std::int64_t summonCooldownMs,
	int incantationMinFood,
	int incantationMinPlayers,
	std::int64_t teamResponseCooldownMs,
	std::int64_t roleBroadcastIntervalMs,
	int assistFoodThreshold,
	int shareFoodThreshold
)
	: _incantationStrategy(incantationMinFood, incantationMinPlayers),
	  _visionRefreshMs(visionRefreshMs),
	  _inventoryRefreshMs(inventoryRefreshMs),
	  _incantationRetryDelayMs(incantationRetryDelayMs),
	  _summonCooldownMs(summonCooldownMs),
	  _teamResponseCooldownMs(teamResponseCooldownMs),
	  _roleBroadcastIntervalMs(roleBroadcastIntervalMs),
	  _lastIncantationAtMs(-1),
	  _lastSummonAtMs(-1),
	  _lastTeamResponseAtMs(-1),
	  _lastRoleBroadcastAtMs(-1),
	  _assistFoodThreshold(assistFoodThreshold),
	  _shareFoodThreshold(shareFoodThreshold) {}

const WorldState& WorldModelPolicy::state() const {
	return _state;
}

std::vector<std::shared_ptr<IntentRequest>> WorldModelPolicy::onTick(std::int64_t nowMs) {
	std::vector<std::shared_ptr<IntentRequest>> intents;

	if (!_activePlan.empty()) {
		return intents;
	}

	if (!_state.hasRecentVision(nowMs, _visionRefreshMs)) {
		intents.push_back(std::make_shared<RequestVoir>());
	}

	if (!_state.hasRecentInventory(nowMs, _inventoryRefreshMs)) {
		intents.push_back(std::make_shared<RequestInventaire>());
	}

	if (!intents.empty()) {
		return intents;
	}

	if (
		nowMs >= _roleBroadcastIntervalMs
		&& (_lastRoleBroadcastAtMs < 0 || (nowMs - _lastRoleBroadcastAtMs) >= _roleBroadcastIntervalMs)
	) {
		Logger::info("WorldModelPolicy: broadcasting role intent team:role:gatherer");
		intents.push_back(std::make_shared<RequestBroadcast>("team:role:gatherer"));
		_lastRoleBroadcastAtMs = nowMs;
		return intents;
	}

	const IncantationAction action = _incantationStrategy.decide(_state);
	if (
		action == IncantationAction::Incantate
		&& (_lastIncantationAtMs < 0 || (nowMs - _lastIncantationAtMs) >= _incantationRetryDelayMs)
	) {
		intents.push_back(std::make_shared<RequestIncantation>());
		_lastIncantationAtMs = nowMs;
		return intents;
	}

	if (
		action == IncantationAction::Summon
		&& (_lastSummonAtMs < 0 || (nowMs - _lastSummonAtMs) >= _summonCooldownMs)
	) {
		intents.push_back(std::make_shared<RequestBroadcast>("need_players_for_incantation"));
		_lastSummonAtMs = nowMs;
		return intents;
	}

	const std::vector<ResourceType> priority = _resourceStrategy.buildPriority(_state);
	const std::vector<NavigationStep> plannedSteps = _planner.buildPlan(_state, priority);
	for (const NavigationStep& step : plannedSteps) {
		_activePlan.push_back(step);
		intents.push_back(step.intent);
	}

	return intents;
}

std::vector<std::shared_ptr<IntentRequest>> WorldModelPolicy::onCommandEvent(
	std::int64_t nowMs,
	const CommandEvent& event,
	const std::optional<IntentResult>& intentResult
) {
	if (!event.isSuccess()) {
		if (!_activePlan.empty() && _activePlan.front().commandType == event.commandType) {
			_activePlan.clear();
		}
		return {};
	}

	switch (event.commandType) {
		case CommandType::Voir:
			_state.recordVision(nowMs, event.details);
			_activePlan.clear();
			break;

		case CommandType::Inventaire:
			_state.recordInventory(nowMs, event.details);
			break;

		case CommandType::Droite:
			_state.recordTurnRight(nowMs);
			break;

		case CommandType::Gauche:
			_state.recordTurnLeft(nowMs);
			break;

		case CommandType::Avance:
			_state.recordForward(nowMs);
			break;

		case CommandType::Broadcast:
			if (intentResult.has_value()) {
				_state.recordBroadcast(nowMs, intentResult->intentType);
			} else {
				_state.recordBroadcast(nowMs, event.details);

			const TeamSignal signal = TeamBroadcastProtocol::parse(event.details);
			if (
				signal.kind != TeamSignalKind::Unknown
				&& (_lastTeamResponseAtMs < 0 || (nowMs - _lastTeamResponseAtMs) >= _teamResponseCooldownMs)
			) {
				const int food = _state.inventoryCount(ResourceType::Nourriture).value_or(0);
				if (signal.kind == TeamSignalKind::NeedPlayers && food >= _assistFoodThreshold) {
					Logger::info("WorldModelPolicy: team need players received, acknowledging assistance");
					_lastTeamResponseAtMs = nowMs;
					return {std::make_shared<RequestBroadcast>("team:on_my_way")};
				}

				if (signal.kind == TeamSignalKind::NeedFood && food >= _shareFoodThreshold) {
					Logger::info("WorldModelPolicy: team need food received, advertising food support");
					_lastTeamResponseAtMs = nowMs;
					return {std::make_shared<RequestBroadcast>("team:offer:food")};
				}
			}
			}
			break;

		default:
			break;
	}

	if (!_activePlan.empty() && _activePlan.front().commandType == event.commandType) {
		_activePlan.pop_front();
	}

	return {};
}
