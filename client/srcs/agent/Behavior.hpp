#pragma once

#include "../protocol/Message.hpp"
#include "State.hpp"
#include "../protocol/Sender.hpp"
#include "Navigator.hpp"

#include <cstdint>
#include <deque>

enum class AIState {
	Idle,
	CollectFood,
	CollectStones,
	Incantating,
	Leading,
	MovingToRally,
	Rallying
};

struct LevelReq {
	int			players;
	Inventory	stones;
};

class Behavior {
	private:
		Sender&				_sender;
		WorldState&			_state;
		bool				_commandInFlight = false;
		bool				_staleVision = true;
		bool				_staleInventory = true;
		std::deque<NavCmd>	_navPlan;
		std::string			_navTarget;
		int					_explorationStep = 0;

		AIState	_aiState = AIState::CollectFood;
		bool	_easyMode = false;
		bool	_pendingLevelUp = false;

		bool	_isLeader = false;
		bool	_isMovingToRally = false;
		bool	_isRallying = false;
		int		_rallyLevel = 0;
		int		_broadcastDirection = 0;
		int		_peerConfirmedCount = 0;
		int64_t	_lastRallyBroadcastMs = 0;
		int64_t _leadingTimeoutMs = 0;
		int64_t _movingToRallyTimeoutMs = 0;
		int64_t _rallyingTimeoutMs = 0;

		bool _forkInProgress = false;

		std::vector<std::string>	_stonesNeeded;
		bool						_incantationReady;
		bool						_stonesPlaced;

		static constexpr int FOOD_FORK		= 24;
		static constexpr int FOOD_SAFE		= 16;
		static constexpr int FOOD_CRITICAL	= 4;

		void executeNavCmd(NavCmd cmd);

	public:
		Behavior(Sender& sender, WorldState& state);
		~Behavior() = default;
	
		void tick(int64_t nowMs);
		void tickCollectFood();
		void tickCollectStones();
		void tickIdle();
		void tickIncantating();
		void tickLeading(int64_t nowMs);
		void tickMovingToRally(int64_t nowMs);
		void tickRallying(int64_t nowMs);

		void refreshVision();
		void refreshInventory();

		void onBroadcast(const ServerMessage& msg);

		AIState getState() const { return _aiState; } // for loop status periodic print

		bool hasCommandInFlight() const { return _commandInFlight; }
		bool isVisionStale()      const { return _staleVision; }
		bool isInventoryStale()   const { return _staleInventory; }

		std::vector<std::string>& getStonesNeeded() { return _stonesNeeded; }

		void setVisionStale()    { _staleVision = true; }
		void setInventoryStale() { _staleInventory = true; }
		void clearNavPlan()      { _navPlan.clear(); _navTarget.clear(); }

		void computeMissingStones();
		VisionTile getNearestTileWithNeededResource();
		void setPendingLevelUp(bool val) { _pendingLevelUp = val; }
		void setEasyMode(bool enabled) { _easyMode = enabled; }
};
