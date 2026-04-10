#pragma once

#include "WorldState.hpp"
#include "CommandSender.hpp"
#include "NavigationPlanner.hpp"

#include <string>
#include <vector>
#include <queue>
#include <cstdint>

namespace zappy {
	enum class AIState {
		Idle,
		WaitingForResponse,
		Exploring,
		Gathering,
		Returning,
		Incantating
	};

	class SimpleAI {
		private:
			WorldState&			_state;
			CommandSender&		_sender;
			NavigationPlanner	_planner;

			// config
			bool	_forkEnabled = true;
			int		_targetLevel = 8;
			int		_maxForks = 5;
			int		_forkFoodThreshold = 15;
			int		_foodEmergencyThreshold = 3;
			int		_foodComfortThreshold = 8;
			bool	_easyAscensionMode = false;

			int		_commandTimeoutMs = 30000;

			// state
			AIState	_AIstate = AIState::Idle;
			int64_t	_lastCommandTime = 0;
			int64_t	_lastVoirTime = 0;
			int64_t	_lastInventaireTime = 0;
			int64_t	_lastForkTime = 0;
			int64_t	_lastIncantationTime = 0;
			int		_forkCount = 0;

			// action queue
			std::queue<NavigationStep>	_actionQueue;
			std::string					_currentResourceTarget;
			int64_t						_pendingCommandId = 0;

			// decision making
			void decideNextAction(int64_t nowMs);
			void processResponse(const ServerMessage& msg);

			// priorities
			bool shouldGetFood() const;
			bool shouldFork(int64_t nowMs) const;
			bool shouldIncantate(int64_t nowMs) const;
			std::vector<std::string> getResourcePriorities() const;

			// Actions
			void startGathering(const std::string& resource);
			void startIncantation();
			void startFork();
			void requestVision();
			void requestInventory();

			// execution
			void executeNextAction();
			void onCommandComplete(const ServerMessage& msg);

		public:
			SimpleAI(WorldState& state, CommandSender& sender);
			~SimpleAI() = default;

			// main tick func
			void tick(int64_t nowMs);
			
			// FIXED: Handle incoming messages for coordination
			void onMessage(const ServerMessage& msg);

			// conf
			void setForkEnabled(bool enabled) { _forkEnabled = enabled; }
			void setTargetLevel(int level) { _targetLevel = level; }
			void setMaxForks(int max) { _maxForks = max; }

			// status
			AIState getState() const { return _AIstate; }
			int getForkCount() const { return _forkCount; }
			size_t getPendingActions() const { return _actionQueue.size(); }
	};
} // namespace zappy