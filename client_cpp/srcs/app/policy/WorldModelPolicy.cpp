#include "app/policy/WorldModelPolicy.hpp"

#include "app/command/CommandType.hpp"
#include "helpers/Logger.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>
#include <vector>

namespace {
	std::optional<std::string> extractJsonStringField(const std::string& text, const std::string& fieldName) {
		const std::string keyToken = "\"" + fieldName + "\"";
		const std::size_t keyPos = text.find(keyToken);
		if (keyPos == std::string::npos) {
			return std::nullopt;
		}

		const std::size_t colonPos = text.find(':', keyPos + keyToken.size());
		if (colonPos == std::string::npos) {
			return std::nullopt;
		}

		std::size_t valuePos = colonPos + 1;
		while (valuePos < text.size() && std::isspace(static_cast<unsigned char>(text[valuePos]))) {
			++valuePos;
		}

		if (valuePos >= text.size() || text[valuePos] != '"') {
			return std::nullopt;
		}

		++valuePos;
		std::size_t closePos = valuePos;
		while (closePos < text.size()) {
			if (text[closePos] == '"' && (closePos == valuePos || text[closePos - 1] != '\\')) {
				return text.substr(valuePos, closePos - valuePos);
			}
			++closePos;
		}

		return std::nullopt;
	}

	bool isLevelUpEvent(const std::string& payload) {
		const std::optional<std::string> type = extractJsonStringField(payload, "type");
		if (!type.has_value() || *type != "event") {
			return false;
		}

		const std::optional<std::string> status = extractJsonStringField(payload, "status");
		if (!status.has_value()) {
			return false;
		}

		if (*status == "Level up!" || *status == "level up!") {
			return true;
		}

		const std::optional<std::string> event = extractJsonStringField(payload, "event");
		return event.has_value() && *event == "elevation" && *status == "ok";
	}

	std::optional<ResourceType> parseTakeResourceFromIntentType(const std::optional<IntentResult>& intentResult) {
		if (!intentResult.has_value()) {
			return std::nullopt;
		}

		const std::string prefix = "RequestTake(";
		if (intentResult->intentType.rfind(prefix, 0) != 0) {
			return std::nullopt;
		}

		const std::size_t close = intentResult->intentType.find(')', prefix.size());
		if (close == std::string::npos || close <= prefix.size()) {
			return std::nullopt;
		}

		const std::string resource = intentResult->intentType.substr(prefix.size(), close - prefix.size());
		if (resource == "nourriture") {
			return ResourceType::Nourriture;
		}
		if (resource == "linemate") {
			return ResourceType::Linemate;
		}
		if (resource == "deraumere") {
			return ResourceType::Deraumere;
		}
		if (resource == "sibur") {
			return ResourceType::Sibur;
		}
		if (resource == "mendiane") {
			return ResourceType::Mendiane;
		}
		if (resource == "phiras") {
			return ResourceType::Phiras;
		}
		if (resource == "thystame") {
			return ResourceType::Thystame;
		}

		return std::nullopt;
	}

	std::optional<int> parseIntegerPayload(const std::string& payload) {
		for (std::size_t index = 0; index < payload.size(); ++index) {
			const char current = payload[index];
			if (current == '-' || std::isdigit(static_cast<unsigned char>(current))) {
				std::size_t end = index + 1;
				while (end < payload.size() && std::isdigit(static_cast<unsigned char>(payload[end]))) {
					++end;
				}
				try {
					return std::stoi(payload.substr(index, end - index));
				} catch (...) {
					return std::nullopt;
				}
			}
		}

		return std::nullopt;
	}

	std::int64_t resolvedServerTimeUnitMs() {
		const char* value = std::getenv("ZAPPY_TIME_UNIT");
		if (!value || !*value) {
			return 126;
		}

		try {
			const int parsed = std::stoi(value);
			if (parsed < 1) {
				return 126;
			}
			return parsed;
		} catch (...) {
			return 126;
		}
	}

	std::int64_t scaleByServerTimeUnit(std::int64_t baselineMs) {
		const std::int64_t timeUnitMs = resolvedServerTimeUnitMs();
		std::int64_t scaled = (baselineMs * timeUnitMs) / 126;
		if (scaled < 100) {
			scaled = 100;
		}
		return scaled;
	}

	bool easyAscensionModeEnabled() {
		const char* value = std::getenv("ZAPPY_EASY_ASCENSION");
		return value != nullptr && std::string(value) == "1";
	}

	std::map<ResourceType, int> requiredResourcesForLevel(int playerLevel) {
		switch (playerLevel) {
			case 1:
				return {
					{ResourceType::Linemate, 1},
				};
			case 2:
				return {
					{ResourceType::Linemate, 1},
					{ResourceType::Deraumere, 1},
					{ResourceType::Sibur, 1},
				};
			case 3:
				return {
					{ResourceType::Linemate, 2},
					{ResourceType::Sibur, 1},
					{ResourceType::Phiras, 2},
				};
			case 4:
				return {
					{ResourceType::Linemate, 1},
					{ResourceType::Deraumere, 1},
					{ResourceType::Sibur, 2},
					{ResourceType::Phiras, 1},
				};
			case 5:
				return {
					{ResourceType::Linemate, 1},
					{ResourceType::Deraumere, 2},
					{ResourceType::Sibur, 1},
					{ResourceType::Mendiane, 3},
				};
			case 6:
				return {
					{ResourceType::Linemate, 1},
					{ResourceType::Deraumere, 2},
					{ResourceType::Sibur, 3},
					{ResourceType::Phiras, 1},
				};
			case 7:
				return {
					{ResourceType::Linemate, 2},
					{ResourceType::Deraumere, 2},
					{ResourceType::Sibur, 2},
					{ResourceType::Mendiane, 2},
					{ResourceType::Phiras, 2},
					{ResourceType::Thystame, 1},
				};
			default:
				return {};
		}
	}

	std::vector<std::shared_ptr<IntentRequest>> buildTileStagingIntents(const WorldState& state) {
		std::vector<std::shared_ptr<IntentRequest>> intents;
		const int playerLevel = state.playerLevel();
		if (playerLevel >= 8) {
			return intents;
		}

		const std::map<ResourceType, int> required = requiredResourcesForLevel(playerLevel);
		for (const auto& pair : required) {
			const ResourceType type = pair.first;
			const int need = pair.second;
			const int onTile = state.currentTileResourceCount(type);
			if (onTile >= need) {
				continue;
			}

			const int inInventory = state.inventoryCount(type).value_or(0);
			const int missing = need - onTile;
			const int toPlace = std::min(missing, inInventory);
			for (int count = 0; count < toPlace; ++count) {
				intents.push_back(std::make_shared<RequestPlace>(type));
			}
		}

		return intents;
	}
}

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
	int shareFoodThreshold,
	int forkFoodThreshold,
	std::int64_t forkCooldownMs,
	int maxForks,
	int initialTeamSlots
)
	: _resourceStrategy(2, 4),
	  _incantationStrategy(incantationMinFood, incantationMinPlayers),
	  _visionRefreshMs(scaleByServerTimeUnit(visionRefreshMs)),
	  _inventoryRefreshMs(scaleByServerTimeUnit(inventoryRefreshMs)),
	  _incantationRetryDelayMs(scaleByServerTimeUnit(incantationRetryDelayMs)),
	  _summonCooldownMs(scaleByServerTimeUnit(summonCooldownMs)),
	  _teamResponseCooldownMs(scaleByServerTimeUnit(teamResponseCooldownMs)),
	  _roleBroadcastIntervalMs(scaleByServerTimeUnit(roleBroadcastIntervalMs)),
	  _lastIncantationAtMs(-1),
	  _lastSummonAtMs(-1),
	  _lastForkAtMs(-1),
	  _lastTeamResponseAtMs(-1),
	  _lastRoleBroadcastAtMs(-1),
		_lastConnectNbrAtMs(-1),
		_meetingUntilAtMs(-1),
	  _visionRefreshRequested(false),
	  _inventoryRefreshRequested(false),
		_pendingConnectNbrPoll(false),
		_teamSlotsAvailable(initialTeamSlots),
	  _assistFoodThreshold(assistFoodThreshold),
	  _shareFoodThreshold(shareFoodThreshold),
	  _forkFoodThreshold(forkFoodThreshold),
	  _forkCooldownMs(scaleByServerTimeUnit(forkCooldownMs)),
	  _forksIssued(0),
		_maxForks(maxForks),
		_victoryReached(false) {}

const WorldState& WorldModelPolicy::state() const {
	return _state;
}

bool WorldModelPolicy::victoryReached() const {
	return _victoryReached;
}

std::vector<std::shared_ptr<IntentRequest>> WorldModelPolicy::onTick(std::int64_t nowMs) {
	std::vector<std::shared_ptr<IntentRequest>> intents;

	auto maybeRequestFork = [this, nowMs, &intents]() -> bool {
		if (!_state.hasInventory() || _state.playerLevel() >= 8) {
			return false;
		}

		if (_forksIssued >= _maxForks) {
			return false;
		}

		if (!(_lastForkAtMs < 0 || (nowMs - _lastForkAtMs) >= _forkCooldownMs)) {
			return false;
		}

		const int food = _state.inventoryCount(ResourceType::Nourriture).value_or(0);
		if (food < _forkFoodThreshold) {
			return false;
		}

		Logger::info("WorldModelPolicy: requesting fork to grow team population, food=" + std::to_string(food));
		intents.push_back(std::make_shared<RequestFork>());
		_lastForkAtMs = nowMs;
		_pendingConnectNbrPoll = true;
		++_forksIssued;
		return true;
	};

	if (_meetingDirection.has_value()) {
		if (_meetingUntilAtMs >= 0 && nowMs > _meetingUntilAtMs) {
			_meetingDirection.reset();
			_meetingUntilAtMs = -1;
		}
		if (_meetingDirection.has_value() && _meetingUntilAtMs >= 0 && nowMs <= _meetingUntilAtMs) {
			const std::optional<WorldState::Pose> pose = _state.pose();
			if (pose.has_value()) {
				if (*_meetingDirection == 0) {
					_meetingDirection.reset();
					_meetingUntilAtMs = -1;
				} else {
					std::vector<NavigationStep> meetingSteps = _planner.buildBroadcastApproachPlan(*pose, *_meetingDirection);
					if (!meetingSteps.empty()) {
						Logger::info("WorldModelPolicy: prioritizing meeting broadcast direction " + std::to_string(*_meetingDirection));
						for (const NavigationStep& step : meetingSteps) {
							_activePlan.push_back(step);
							intents.push_back(step.intent);
						}
						return intents;
					}
				}
			}
		}
	}

	if (_pendingConnectNbrPoll && (_lastConnectNbrAtMs < 0 || (nowMs - _lastConnectNbrAtMs) >= 2000)) {
		Logger::info("WorldModelPolicy: polling connect_nbr after fork");
		intents.push_back(std::make_shared<RequestConnectNbr>());
		_lastConnectNbrAtMs = nowMs;
		_pendingConnectNbrPoll = false;
		return intents;
	}

	if (easyAscensionModeEnabled()) {
		const IncantationAction easyAction = _incantationStrategy.decide(_state);
		if (easyAction == IncantationAction::Incantate
			&& (_lastIncantationAtMs < 0 || (nowMs - _lastIncantationAtMs) >= _incantationRetryDelayMs)) {
			_activePlan.clear();
			_activeHarvestTarget.reset();
			Logger::info("WorldModelPolicy: easy mode priority incantation request");
			intents.push_back(std::make_shared<RequestIncantation>());
			_lastIncantationAtMs = nowMs;
			return intents;
		}
	}

	if (!_activePlan.empty()) {
		if (maybeRequestFork()) {
			return intents;
		}
		return intents;
	}

	if (_activeHarvestTarget.has_value()) {
		const std::optional<WorldState::Pose> pose = _state.pose();
		if (pose.has_value() && pose->x == _activeHarvestTarget->targetX && pose->y == _activeHarvestTarget->targetY) {
			const int targetX = _activeHarvestTarget->targetX;
			const int targetY = _activeHarvestTarget->targetY;
			const ResourceType resource = _activeHarvestTarget->resource;
			_activeHarvestTarget.reset();

			if (!_state.hasRecentVision(nowMs, kTakeVisionFreshnessMs)) {
				if (!_visionRefreshRequested) {
					Logger::debug("WorldModelPolicy: arrived at harvest target with stale vision, requesting refresh");
					intents.push_back(std::make_shared<RequestVoir>());
					_visionRefreshRequested = true;
				}
				return intents;
			}

			if (_lastFailedTake.has_value() && _lastFailedTake->x == targetX && _lastFailedTake->y == targetY) {
				const bool sameResource = !_lastFailedTake->resource.has_value()
					|| _lastFailedTake->resource.value() == resource;
				const bool inCooldown = (nowMs - _lastFailedTake->atMs) < kTakeRetryCooldownMs;
				if (sameResource && (_lastFailedTake->needsFreshVision || inCooldown)) {
					if (!_visionRefreshRequested) {
						Logger::debug("WorldModelPolicy: suppressing repeated take after ko, requesting fresh vision");
						intents.push_back(std::make_shared<RequestVoir>());
						_visionRefreshRequested = true;
					}
					return intents;
				}
			}

			if (_state.currentTileHasResource(resource)) {
				Logger::info("WorldModelPolicy: arrived at harvest target, taking "
					+ std::to_string(static_cast<int>(resource)));
				intents.push_back(std::make_shared<RequestTake>(resource));
			} else if (!_visionRefreshRequested) {
				Logger::debug("WorldModelPolicy: arrived at target but resource missing, refreshing vision");
				intents.push_back(std::make_shared<RequestVoir>());
				_visionRefreshRequested = true;
			}
			return intents;
		}

		_activeHarvestTarget.reset();
	}

	const IncantationAction action = _incantationStrategy.decide(_state);
	if (action == IncantationAction::Incantate && _state.hasVision() && _state.hasInventory()) {
		std::vector<std::shared_ptr<IntentRequest>> staging = buildTileStagingIntents(_state);
		if (!staging.empty()) {
			Logger::info("WorldModelPolicy: staging resources on tile for incantation");
			return staging;
		}
	}

	if (
		action == IncantationAction::Incantate
	) {
		if (!(_lastIncantationAtMs < 0 || (nowMs - _lastIncantationAtMs) >= _incantationRetryDelayMs)) {
			Logger::debug("WorldModelPolicy: incantation ready but cooling down, elapsed="
				+ std::to_string(nowMs - _lastIncantationAtMs)
				+ "ms < retryDelay=" + std::to_string(_incantationRetryDelayMs));
		} else {
		Logger::info("WorldModelPolicy: aggressive incantation attempt requested");
		intents.push_back(std::make_shared<RequestIncantation>());
		_lastIncantationAtMs = nowMs;
		return intents;
		}
	}

	if (
		action == IncantationAction::Summon
	) {
		if (!(_lastSummonAtMs < 0 || (nowMs - _lastSummonAtMs) >= _summonCooldownMs)) {
			Logger::debug("WorldModelPolicy: summon ready but cooling down, elapsed="
				+ std::to_string(nowMs - _lastSummonAtMs)
				+ "ms < cooldown=" + std::to_string(_summonCooldownMs));
		} else {
		Logger::info("WorldModelPolicy: aggressive summon request for incantation support");
		intents.push_back(std::make_shared<RequestBroadcast>("need_players_for_incantation"));
		_lastSummonAtMs = nowMs;
		return intents;
		}
	}

	if (maybeRequestFork()) {
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

	std::vector<ResourceType> priority;
	const int playerLevel = _state.playerLevel();
	if (playerLevel < 8) {
		const std::map<ResourceType, int> required = requiredResourcesForLevel(playerLevel);
		for (const auto& pair : required) {
			const int have = _state.inventoryCount(pair.first).value_or(0);
			if (have < pair.second) {
				priority.push_back(pair.first);
			}
		}
	}

	const std::vector<ResourceType> fallbackPriority = _resourceStrategy.buildPriority(_state);
	for (ResourceType resource : fallbackPriority) {
		if (std::find(priority.begin(), priority.end(), resource) == priority.end()) {
			priority.push_back(resource);
		}
	}

	std::optional<NavigationTarget> selectedTarget;
	const std::vector<NavigationStep> plannedSteps = _planner.buildPlan(_state, priority, &selectedTarget);
	_activeHarvestTarget = selectedTarget;
	if (!plannedSteps.empty() && plannedSteps.front().commandType == CommandType::Prend) {
		if (!_state.hasRecentVision(nowMs, kTakeVisionFreshnessMs)) {
			if (!_visionRefreshRequested) {
				Logger::debug("WorldModelPolicy: delaying planner take until fresh vision is available");
				intents.push_back(std::make_shared<RequestVoir>());
				_visionRefreshRequested = true;
			}
			return intents;
		}

		const std::optional<WorldState::Pose> pose = _state.pose();
		const RequestTake* take = dynamic_cast<const RequestTake*>(plannedSteps.front().intent.get());
		const std::optional<ResourceType> plannedResource =
			take != nullptr ? std::optional<ResourceType>(take->resource) : std::nullopt;

		if (pose.has_value() && _lastFailedTake.has_value() && _lastFailedTake->x == pose->x && _lastFailedTake->y == pose->y) {
			const bool sameResource = !_lastFailedTake->resource.has_value()
				|| !plannedResource.has_value()
				|| _lastFailedTake->resource.value() == plannedResource.value();
			const bool inCooldown = (nowMs - _lastFailedTake->atMs) < kTakeRetryCooldownMs;
			if (sameResource && (_lastFailedTake->needsFreshVision || inCooldown)) {
				if (!_visionRefreshRequested) {
					Logger::debug("WorldModelPolicy: suppressing planner take after ko, requesting fresh vision");
					intents.push_back(std::make_shared<RequestVoir>());
					_visionRefreshRequested = true;
				}
				return intents;
			}
		}
	}

	for (const NavigationStep& step : plannedSteps) {
		_activePlan.push_back(step);
		intents.push_back(step.intent);
	}

	if (!_state.hasRecentVision(nowMs, _visionRefreshMs) && !_visionRefreshRequested) {
		intents.push_back(std::make_shared<RequestVoir>());
		_visionRefreshRequested = true;
	}

	if (!_state.hasRecentInventory(nowMs, _inventoryRefreshMs) && !_inventoryRefreshRequested) {
		intents.push_back(std::make_shared<RequestInventaire>());
		_inventoryRefreshRequested = true;
	}

	return intents;
}

std::vector<std::shared_ptr<IntentRequest>> WorldModelPolicy::onCommandEvent(
	std::int64_t nowMs,
	const CommandEvent& event,
	const std::optional<IntentResult>& intentResult
) {
	std::vector<std::shared_ptr<IntentRequest>> followUps;

	if (!event.isSuccess()) {
		if (event.commandType == CommandType::Voir) {
			_visionRefreshRequested = false;
		}
		if (event.commandType == CommandType::Inventaire) {
			_inventoryRefreshRequested = false;
		}
		if (event.commandType == CommandType::Prend) {
			std::optional<ResourceType> failedResource = parseTakeResourceFromIntentType(intentResult);
			const std::optional<WorldState::Pose> pose = _state.pose();
			if (pose.has_value()) {
				_lastFailedTake = FailedTakeContext{
					pose->x,
					pose->y,
					failedResource,
					nowMs,
					true,
				};
			}
			_activeHarvestTarget.reset();
			_state.invalidateVision();
			_visionRefreshRequested = false;
		}
		if (!_activePlan.empty() && _activePlan.front().commandType == event.commandType) {
			_activePlan.clear();
		}
		return {};
	}

	switch (event.commandType) {
		case CommandType::Voir:
			_state.recordVision(nowMs, event.details);
			_visionRefreshRequested = false;
			if (_lastFailedTake.has_value()) {
				_lastFailedTake->needsFreshVision = false;
			}
			break;

		case CommandType::Inventaire:
			_state.recordInventory(nowMs, event.details);
			_inventoryRefreshRequested = false;
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

		case CommandType::Incantation: {
			if (isLevelUpEvent(event.details)) {
				const int playerLevel = _state.recordLevelUp(nowMs);
				Logger::info("WorldModelPolicy: player level advanced to " + std::to_string(playerLevel));
				if (playerLevel >= 8) {
					_victoryReached = true;
					Logger::info("WorldModelPolicy: victory threshold reached");
				}
			} else {
				Logger::info("WorldModelPolicy: incantation acknowledged, awaiting level-up event");
			}
			break;
		}

		case CommandType::Fork:
			if (event.isSuccess()) {
				Logger::info("WorldModelPolicy: fork acknowledged by server");
				_pendingConnectNbrPoll = true;
				followUps.push_back(std::make_shared<RequestConnectNbr>());
			}
			break;

		case CommandType::ConnectNbr: {
			_pendingConnectNbrPoll = false;
			const std::optional<std::string> slotsText = extractJsonStringField(event.details, "arg");
			const std::optional<int> parsedSlots = slotsText.has_value()
				? parseIntegerPayload(*slotsText)
				: parseIntegerPayload(event.details);
			if (parsedSlots.has_value()) {
				_teamSlotsAvailable = *parsedSlots;
				Logger::info("WorldModelPolicy: connect_nbr reported slots=" + std::to_string(_teamSlotsAvailable));
			} else {
				Logger::warn("WorldModelPolicy: failed to parse connect_nbr payload: " + event.details);
			}
			break;
		}

		case CommandType::Prend:
			_activeHarvestTarget.reset();
			_pendingConnectNbrPoll = false;
			_lastFailedTake.reset();
			_state.invalidateVision();
			_visionRefreshRequested = false;
			break;

		case CommandType::Broadcast:
			if (intentResult.has_value()) {
				_state.recordBroadcast(nowMs, event.details);
			} else {
				const TeamSignal signal = TeamBroadcastProtocol::parse(event.details);
				_state.recordBroadcast(nowMs, signal.rawMessage, signal.direction);

				if (signal.kind == TeamSignalKind::NeedPlayers) {
					_meetingDirection = signal.direction;
					_meetingUntilAtMs = nowMs + 12000;
					Logger::info("WorldModelPolicy: meeting request received, direction="
						+ std::to_string(signal.direction.value_or(0)));
				}

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

	if (event.commandType == CommandType::Prend) {
		_activeHarvestTarget.reset();
		_state.invalidateVision();
		_visionRefreshRequested = false;
	}

	return followUps;
}
