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
		int					_explorationStep = 0;

		void executeNavCmd(NavCmd cmd);

	public:
		Behavior(Sender& sender, WorldState& state);
		~Behavior() = default;
	
		void tick(int64_t nowMs);
		void onResponse(const ServerMessage& msg);

		bool hasCommandInFlight() const { return _commandInFlight; }
		bool isVisionStale() const { return _staleVision; }
		bool isInventoryStale() const { return _staleInventory; }
		void setVisionStale() { _staleVision = true; _navPlan.clear(); }
		void setInventoryStale() { _staleInventory = true; }
};
