#include "Navigator.hpp"

// Orientation is always 0-indexed matching the server enum: N=0,E=1,S=2,W=3. Never convert this
// NEVER CONVERT THIS
// I emphasize this to myself because converting this was a mayor pitfall in past builds
std::pair<int, int> Navigator::localToWorldDelta(Orientation facing, int localX, int localY) {
	int worldX, worldY;

	switch (facing) {
		default:
		case Orientation::N:
			worldX =  localX;
			worldY = -localY;
			break;

		case Orientation::E:
			worldX =  localY;
			worldY =  localX;
			break;

		case Orientation::S:
			worldX = -localX;
			worldY =  localY;
			break;

		case Orientation::W:
			worldX = -localY;
			worldY = -localX;
			break;
	}

	return { worldX, worldY };
}

std::vector<NavCmd> Navigator::turnToFace(Orientation current, Orientation target) {
	int diff = (static_cast<int>(target) - static_cast<int>(current) + 4) % 4;

	std::vector<NavCmd> turns;
	switch (diff) {
		case 1:
			turns.push_back(NavCmd::TurnRight);
			break;

		case 2:
			turns.push_back(NavCmd::TurnRight);
			turns.push_back(NavCmd::TurnRight);
			break;

		case 3:
			turns.push_back(NavCmd::TurnLeft);
			break;

		default: // diff == 0: already facing target, no turns needed
			break;
	}

	return turns;
}

// Plans a path from the player's current position to a tile given in local (vision) coordinates.
//
// Local coordinates are always relative to the player's view cone regardless of world orientation:
//   localY = how many rows ahead the tile is (0 = current tile, always >= 0)
//   localX = how many columns left/right (negative = left, positive = right)
//
// Strategy: move laterally (X) first, then forward (Y).
// To move laterally we turn 90°, walk |localX| steps, then turn back.
// The turn direction depends on which world axis "left/right" maps to given facing.
//
// calling localToWorldDelta gives the required world orientation for x and y
// on a pure-X delta and comparing against the four cardinal directions
//
// TODO: OPTIONAL: Move to an A* approach
std::vector<NavCmd> Navigator::planPath(Orientation facing, int localX, int localY) {
	std::vector<NavCmd> commands;

	if (localX == 0 && localY == 0)
		return commands;

	if (localX != 0) {
		// Determine which world direction corresponds to stepping "right" (+localX)
		// from the player's current facing. We do this by asking what world delta
		// a pure localX=+1 produces, then deriving the orientation from that delta.
		auto [dx, dy] = localToWorldDelta(facing, 1, 0);

		// dx/dy will be one of (1,0),(−1,0),(0,1),(0,−1) — map to Orientation
		Orientation xFacing;
		if      (dx ==  1 && dy ==  0) xFacing = Orientation::E;
		else if (dx == -1 && dy ==  0) xFacing = Orientation::W;
		else if (dx ==  0 && dy == -1) xFacing = Orientation::N;
		else                           xFacing = Orientation::S; // dx==0, dy==1
		
		// If localX is negative we want to go the opposite direction
		if (localX < 0) {
			xFacing = static_cast<Orientation>((static_cast<int>(xFacing) + 2) % 4);
		}

		// Turn to face the X direction
		auto turnCmds = turnToFace(facing, xFacing);
		commands.insert(commands.end(), turnCmds.begin(), turnCmds.end());

		// Walk the X distance
		for (int i = 0; i < std::abs(localX); ++i)
			commands.push_back(NavCmd::Forward);

		// Turn back to face forward (Y direction) — which is the original facing
		auto returnCmds = turnToFace(xFacing, facing);
		commands.insert(commands.end(), returnCmds.begin(), returnCmds.end());
	}

	// Walk the Y distance (always straight ahead in original facing direction)
	for (int i = 0; i < localY; ++i)
		commands.push_back(NavCmd::Forward);

	return commands;
}

std::vector<NavCmd> Navigator::explorationStep(int& stepCount) {
	std::vector<NavCmd> commands;
	stepCount++;

	if (stepCount % 13 == 0)
		commands.push_back(NavCmd::TurnLeft);
	else if (stepCount % 7 == 0 || stepCount == 1)
		commands.push_back(NavCmd::TurnRight);

	commands.push_back(NavCmd::Forward);
	return commands;
}

/*
In case of A*

struct PathNode {
    int x, y;
    std::vector<NavCmd> path;
};

std::vector<NavCmd> findPathToTile(Orientation facing, int startLocalX, int startLocalY, int targetLocalX, int targetLocalY);
*/
