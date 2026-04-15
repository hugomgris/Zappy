#pragma once

#include "../protocol/Message.hpp"
#include "State.hpp"
#include "../protocol/Sender.hpp"
#include "Navigator.hpp"

#include <cstdint>
#include <deque>

enum class AIState {
	Idle,
	CollectFood
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

		static constexpr int FOOD_SAFE					= 12;
		static constexpr int FOOD_CRITICAL				= 4;

		void executeNavCmd(NavCmd cmd);

	public:
		Behavior(Sender& sender, WorldState& state);
		~Behavior() = default;
	
		void tick(int64_t nowMs);
		void onResponse(const ServerMessage& msg);

		bool hasCommandInFlight() const { return _commandInFlight; }
		bool isVisionStale()      const { return _staleVision; }
		bool isInventoryStale()   const { return _staleInventory; }

		// Mark vision stale — does NOT clear the nav plan.
		// The nav plan is cleared in the voir callback only if the target is gone,
		// or explicitly via clearNavPlan() when the situation changes.
		void setVisionStale()    { _staleVision = true; }
		void setInventoryStale() { _staleInventory = true; }
		void clearNavPlan()      { _navPlan.clear(); _navTarget.clear(); }
};
