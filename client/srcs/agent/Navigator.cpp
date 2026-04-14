#include "Navigator.hpp"

std::pair<int, int> Navigator::localToWorldDelta(Orientation facing, int localX, int localY) {
	int worldX, worldY;

	switch (facing) {
		default:
		case Orientation::N:
			worldX = localX;
			worldY = -localY;
			break;

		case Orientation::E:
            worldX = localY;
            worldY = localX;
            break;

		case Orientation::S:
            worldX = -localX;
            worldY = localY;
            break;

		case Orientation::W:
            worldX = -localY;
            worldY = -localX;
            break;
	}

	std::pair worldPos { worldX, worldY };
	return worldPos;
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
	}

	return turns;
}

std::vector<NavCmd> Navigator::planPath(Orientation facing, int localX, int localY) {
	// Full path generation to the target coordinates
	// Note: localY should always be >= 0 since we only see in front of us
	(void)facing;
	std::vector<NavCmd> commands;

	if (localX == 0 && localY == 0) {
		return commands;
	}

	if (localX != 0) {
    commands.push_back(localX < 0 ? NavCmd::TurnLeft : NavCmd::TurnRight);

    for (int i = 0; i < std::abs(localX); ++i)
        commands.push_back(NavCmd::Forward);

    // Always correct back — regardless of whether localY follows
    commands.push_back(localX < 0 ? NavCmd::TurnRight : NavCmd::TurnLeft);
}

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