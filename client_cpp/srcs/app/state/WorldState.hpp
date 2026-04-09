#pragma once

#include "app/command/ResourceType.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class WorldState {
	public:
		struct Pose {
			int x = 0;
			int y = 0;
			int orientation = 1;
		};

		struct VisionTile {
			std::size_t index = 0;
			int localX = 0;
			int localY = 0;
			std::int64_t seenAtMs = -1;
			int playerCount = 0;
			std::vector<ResourceType> resources;

			bool hasResource(ResourceType resource) const;
		};

	private:
		std::string _lastVisionPayload;
		std::string _lastInventoryPayload;
		std::string _lastBroadcastPayload;
		std::optional<int> _lastBroadcastDirection;
		std::map<ResourceType, int> _inventoryCounts;
		std::vector<VisionTile> _visionTiles;
		Pose _pose;
		bool _hasPose;
		std::int64_t _lastVisionAtMs;
		std::int64_t _lastInventoryAtMs;
		std::int64_t _lastBroadcastAtMs;
		std::int64_t _lastPoseAtMs;
		std::int64_t _lastLevelUpAtMs;
		int _playerLevel;

	private:
		static std::optional<int> parseResourceCount(const std::string& payload, const std::string& resourceName);
		static std::string normalizeVisionPayload(const std::string& payload);
		static std::vector<std::string> splitVisionTiles(const std::string& payload);
		static std::vector<ResourceType> parseTileResources(const std::string& tilePayload);
		static int parseTilePlayerCount(const std::string& tilePayload);
		static std::pair<int, int> visionIndexToLocalCoordinates(std::size_t index);

	public:
		WorldState();

		void clear();
		void invalidateVision();

		void recordVision(std::int64_t nowMs, const std::string& payload);
		void recordInventory(std::int64_t nowMs, const std::string& payload);
		void recordBroadcast(std::int64_t nowMs, const std::string& payload, std::optional<int> direction = std::nullopt);
		void recordPose(std::int64_t nowMs, int x, int y, int orientation);
		void recordTurnLeft(std::int64_t nowMs);
		void recordTurnRight(std::int64_t nowMs);
		void recordForward(std::int64_t nowMs);
		int recordLevelUp(std::int64_t nowMs);

		bool hasVision() const;
		bool hasInventory() const;
		bool hasBroadcast() const;
		bool hasPose() const;
		bool hasRecentVision(std::int64_t nowMs, std::int64_t maxAgeMs) const;
		bool hasRecentInventory(std::int64_t nowMs, std::int64_t maxAgeMs) const;
		bool hasRecentPose(std::int64_t nowMs, std::int64_t maxAgeMs) const;

		std::optional<std::int64_t> lastVisionAt() const;
		std::optional<std::int64_t> lastInventoryAt() const;
		std::optional<std::int64_t> lastBroadcastAt() const;
		std::optional<int> lastBroadcastDirection() const;
		std::optional<std::int64_t> lastPoseAt() const;
		std::optional<std::int64_t> lastLevelUpAt() const;
		std::optional<Pose> pose() const;
		int playerLevel() const;

		const std::string& lastVisionPayload() const;
		const std::string& lastInventoryPayload() const;
		const std::string& lastBroadcastPayload() const;

		std::optional<int> inventoryCount(ResourceType resource) const;
		const std::map<ResourceType, int>& inventoryCounts() const;
		const std::vector<VisionTile>& visionTiles() const;
		bool currentTileHasResource(ResourceType resource) const;
		int currentTileResourceCount(ResourceType resource) const;
		std::optional<VisionTile> nearestVisionTileWith(ResourceType resource) const;
		int currentTilePlayerCount() const;
};
