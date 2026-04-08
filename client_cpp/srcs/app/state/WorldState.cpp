#include "app/state/WorldState.hpp"

#include <array>
#include <utility>
#include <regex>

namespace {
	struct ResourceNameEntry {
		ResourceType type;
		const char* name;
	};

	constexpr std::array<ResourceNameEntry, 7> RESOURCE_NAMES = {{
		{ResourceType::Nourriture, "nourriture"},
		{ResourceType::Linemate, "linemate"},
		{ResourceType::Deraumere, "deraumere"},
		{ResourceType::Sibur, "sibur"},
		{ResourceType::Mendiane, "mendiane"},
		{ResourceType::Phiras, "phiras"},
		{ResourceType::Thystame, "thystame"},
	}};

	std::string unescapeJsonString(const std::string& payload) {
		std::string out;
		out.reserve(payload.size());

		for (std::size_t index = 0; index < payload.size(); ++index) {
			if (payload[index] == '\\' && index + 1 < payload.size()) {
				const char next = payload[index + 1];
				if (next == '"' || next == '\\') {
					out.push_back(next);
					++index;
					continue;
				}
			}

			out.push_back(payload[index]);
		}

		return out;
	}
}

WorldState::WorldState()
	: _pose{}, _hasPose(false), _lastVisionAtMs(-1), _lastInventoryAtMs(-1), _lastBroadcastAtMs(-1), _lastPoseAtMs(-1),
	  _lastLevelUpAtMs(-1), _playerLevel(1) {}

bool WorldState::VisionTile::hasResource(ResourceType resource) const {
	for (ResourceType current : resources) {
		if (current == resource) {
			return true;
		}
	}

	return false;
}

void WorldState::clear() {
	_lastVisionPayload.clear();
	_lastInventoryPayload.clear();
	_lastBroadcastPayload.clear();
	_inventoryCounts.clear();
	_visionTiles.clear();
	_pose = Pose{};
	_hasPose = false;
	_lastVisionAtMs = -1;
	_lastInventoryAtMs = -1;
	_lastBroadcastAtMs = -1;
	_lastPoseAtMs = -1;
	_lastLevelUpAtMs = -1;
	_playerLevel = 1;
}

void WorldState::recordVision(std::int64_t nowMs, const std::string& payload) {
	_lastVisionAtMs = nowMs;
	_lastVisionPayload = payload;
	_visionTiles.clear();

	const std::string normalized = normalizeVisionPayload(payload);
	const std::vector<std::string> tilePayloads = splitVisionTiles(normalized);
	for (std::size_t index = 0; index < tilePayloads.size(); ++index) {
		VisionTile tile;
		tile.index = index;
		const std::pair<int, int> coordinates = visionIndexToLocalCoordinates(index);
		tile.localX = coordinates.first;
		tile.localY = coordinates.second;
		tile.seenAtMs = nowMs;
		tile.playerCount = parseTilePlayerCount(tilePayloads[index]);
		tile.resources = parseTileResources(tilePayloads[index]);
		_visionTiles.push_back(tile);
	}
}

void WorldState::recordInventory(std::int64_t nowMs, const std::string& payload) {
	_lastInventoryAtMs = nowMs;
	_lastInventoryPayload = payload;
	_inventoryCounts.clear();

	for (const ResourceNameEntry& entry : RESOURCE_NAMES) {
		const std::optional<int> count = parseResourceCount(payload, entry.name);
		if (count.has_value()) {
			_inventoryCounts[entry.type] = *count;
		}
	}
}

void WorldState::recordBroadcast(std::int64_t nowMs, const std::string& payload) {
	_lastBroadcastAtMs = nowMs;
	_lastBroadcastPayload = payload;
}

void WorldState::recordPose(std::int64_t nowMs, int x, int y, int orientation) {
	_pose.x = x;
	_pose.y = y;
	_pose.orientation = orientation;
	_hasPose = true;
	_lastPoseAtMs = nowMs;
}

void WorldState::recordTurnLeft(std::int64_t nowMs) {
	if (!_hasPose) {
		recordPose(nowMs, _pose.x, _pose.y, 1);
	}

	_pose.orientation = (_pose.orientation == 1) ? 4 : (_pose.orientation - 1);
	_lastPoseAtMs = nowMs;
}

void WorldState::recordTurnRight(std::int64_t nowMs) {
	if (!_hasPose) {
		recordPose(nowMs, _pose.x, _pose.y, 1);
	}

	_pose.orientation = (_pose.orientation == 4) ? 1 : (_pose.orientation + 1);
	_lastPoseAtMs = nowMs;
}

void WorldState::recordForward(std::int64_t nowMs) {
	if (!_hasPose) {
		recordPose(nowMs, _pose.x, _pose.y, 1);
	}

	switch (_pose.orientation) {
		case 1: _pose.y += 1; break;
		case 2: _pose.x += 1; break;
		case 3: _pose.y -= 1; break;
		case 4: _pose.x -= 1; break;
		default: break;
	}
	_lastPoseAtMs = nowMs;
}

int WorldState::recordLevelUp(std::int64_t nowMs) {
	_lastLevelUpAtMs = nowMs;
	++_playerLevel;
	return _playerLevel;
}

bool WorldState::hasVision() const {
	return _lastVisionAtMs >= 0;
}

bool WorldState::hasInventory() const {
	return _lastInventoryAtMs >= 0;
}

bool WorldState::hasBroadcast() const {
	return _lastBroadcastAtMs >= 0;
}

bool WorldState::hasPose() const {
	return _hasPose;
}

bool WorldState::hasRecentVision(std::int64_t nowMs, std::int64_t maxAgeMs) const {
	return hasVision() && (nowMs - _lastVisionAtMs) <= maxAgeMs;
}

bool WorldState::hasRecentInventory(std::int64_t nowMs, std::int64_t maxAgeMs) const {
	return hasInventory() && (nowMs - _lastInventoryAtMs) <= maxAgeMs;
}

bool WorldState::hasRecentPose(std::int64_t nowMs, std::int64_t maxAgeMs) const {
	return hasPose() && _lastPoseAtMs >= 0 && (nowMs - _lastPoseAtMs) <= maxAgeMs;
}

std::optional<std::int64_t> WorldState::lastVisionAt() const {
	if (!hasVision()) {
		return std::nullopt;
	}
	return _lastVisionAtMs;
}

std::optional<std::int64_t> WorldState::lastInventoryAt() const {
	if (!hasInventory()) {
		return std::nullopt;
	}
	return _lastInventoryAtMs;
}

std::optional<std::int64_t> WorldState::lastBroadcastAt() const {
	if (!hasBroadcast()) {
		return std::nullopt;
	}
	return _lastBroadcastAtMs;
}

std::optional<std::int64_t> WorldState::lastPoseAt() const {
	if (!hasPose()) {
		return std::nullopt;
	}
	return _lastPoseAtMs;
}

std::optional<std::int64_t> WorldState::lastLevelUpAt() const {
	if (_lastLevelUpAtMs < 0) {
		return std::nullopt;
	}
	return _lastLevelUpAtMs;
}

std::optional<WorldState::Pose> WorldState::pose() const {
	if (!hasPose()) {
		return std::nullopt;
	}
	return _pose;
}

int WorldState::playerLevel() const {
	return _playerLevel;
}

const std::string& WorldState::lastVisionPayload() const {
	return _lastVisionPayload;
}

const std::string& WorldState::lastInventoryPayload() const {
	return _lastInventoryPayload;
}

const std::string& WorldState::lastBroadcastPayload() const {
	return _lastBroadcastPayload;
}

std::optional<int> WorldState::inventoryCount(ResourceType resource) const {
	const auto it = _inventoryCounts.find(resource);
	if (it == _inventoryCounts.end()) {
		return std::nullopt;
	}
	return it->second;
}

const std::map<ResourceType, int>& WorldState::inventoryCounts() const {
	return _inventoryCounts;
}

const std::vector<WorldState::VisionTile>& WorldState::visionTiles() const {
	return _visionTiles;
}

bool WorldState::currentTileHasResource(ResourceType resource) const {
	for (const VisionTile& tile : _visionTiles) {
		if (tile.index == 0) {
			return tile.hasResource(resource);
		}
	}

	return false;
}

std::optional<WorldState::VisionTile> WorldState::nearestVisionTileWith(ResourceType resource) const {
	for (const VisionTile& tile : _visionTiles) {
		if (tile.hasResource(resource)) {
			return tile;
		}
	}

	return std::nullopt;
}

int WorldState::currentTilePlayerCount() const {
	for (const VisionTile& tile : _visionTiles) {
		if (tile.index == 0) {
			return tile.playerCount;
		}
	}

	return 0;
}

std::optional<int> WorldState::parseResourceCount(const std::string& payload, const std::string& resourceName) {
	const std::regex pattern("\"" + resourceName + "\"\\s*[^0-9-]*(-?[0-9]+)");
	std::smatch match;
	if (!std::regex_search(payload, match, pattern) || match.size() < 2) {
		return std::nullopt;
	}

	try {
		return std::stoi(match[1].str());
	} catch (...) {
		return std::nullopt;
	}
}

std::string WorldState::normalizeVisionPayload(const std::string& payload) {
	const std::size_t start = payload.find('[');
	const std::size_t end = payload.rfind(']');
	if (start == std::string::npos || end == std::string::npos || end <= start) {
		return {};
	}

	return unescapeJsonString(payload.substr(start, end - start + 1));
}

std::vector<std::string> WorldState::splitVisionTiles(const std::string& payload) {
	std::vector<std::string> tiles;
	if (payload.empty()) {
		return tiles;
	}

	int depth = 0;
	bool inString = false;
	bool escape = false;
	std::size_t tileStart = std::string::npos;

	for (std::size_t index = 0; index < payload.size(); ++index) {
		const char current = payload[index];
		if (inString) {
			if (escape) {
				escape = false;
			} else if (current == '\\') {
				escape = true;
			} else if (current == '"') {
				inString = false;
			}
			continue;
		}

		if (current == '"') {
			inString = true;
			continue;
		}

		if (current == '[') {
			++depth;
			if (depth == 2) {
				tileStart = index;
			}
			continue;
		}

		if (current == ']') {
			if (depth == 2 && tileStart != std::string::npos) {
				tiles.push_back(payload.substr(tileStart, index - tileStart + 1));
				tileStart = std::string::npos;
			}
			if (depth > 0) {
				--depth;
			}
		}
	}

	return tiles;
}

std::vector<ResourceType> WorldState::parseTileResources(const std::string& tilePayload) {
	std::vector<ResourceType> resources;
	for (const ResourceNameEntry& entry : RESOURCE_NAMES) {
		const std::string token = std::string("\"") + entry.name + "\"";
		if (tilePayload.find(token) != std::string::npos) {
			resources.push_back(entry.type);
		}
	}

	return resources;
}

int WorldState::parseTilePlayerCount(const std::string& tilePayload) {
	int count = 0;
	std::size_t offset = 0;
	while (true) {
		const std::size_t found = tilePayload.find("\"player\"", offset);
		if (found == std::string::npos) {
			break;
		}
		++count;
		offset = found + 1;
	}

	return count;
}

std::pair<int, int> WorldState::visionIndexToLocalCoordinates(std::size_t index) {
	std::size_t row = 0;
	while ((row + 1) * (row + 1) <= index) {
		++row;
	}

	const std::size_t rowStart = row * row;
	const int lateral = static_cast<int>(index - rowStart) - static_cast<int>(row);
	return {lateral, static_cast<int>(row)};
}
