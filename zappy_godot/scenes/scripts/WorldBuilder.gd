extends Node

enum PatternType {
	# Small patterns (≤5x5)
	SMALL_FORTRESS,
	SMALL_ARENA,
	
	# Medium patterns (6-12)
	MEDIUM_COURTYARD,
	MEDIUM_WALLS,
	CORNER_TOWERS,
	
	# Large patterns (>12)
	LARGE_MAZE,
	LARGE_TOWERS,
	LARGE_STRONGHOLD
}

func _ready() -> void:
	print("WorldBuilder initialized with simple pattern system")

func select_pattern(map_size: Vector2i) -> PatternType:
	"""Select a random pattern based on map size"""
	var max_dimension = max(map_size.x, map_size.y)
	
	if max_dimension <= 5:
		# Small patterns
		return [PatternType.SMALL_FORTRESS, PatternType.SMALL_ARENA].pick_random()
	elif max_dimension <= 12:
		# Medium patterns  
		return [PatternType.MEDIUM_COURTYARD, PatternType.MEDIUM_WALLS, PatternType.CORNER_TOWERS].pick_random()
	else:
		# Large patterns
		return [PatternType.LARGE_MAZE, PatternType.LARGE_TOWERS, PatternType.LARGE_STRONGHOLD].pick_random()

func get_tile_type_for_position(pattern: PatternType, x: int, y: int, map_size: Vector2i) -> TileRule.TileType:
	"""Get the tile type for a specific position based on the selected pattern"""
	match pattern:
		PatternType.SMALL_FORTRESS:
			return _build_small_fortress(x, y, map_size)
		PatternType.SMALL_ARENA:
			return _build_small_arena(x, y, map_size)
		PatternType.MEDIUM_COURTYARD:
			return _build_medium_courtyard(x, y, map_size)
		PatternType.MEDIUM_WALLS:
			return _build_medium_walls(x, y, map_size)
		PatternType.CORNER_TOWERS:
			return _build_corner_towers(x, y, map_size)
		PatternType.LARGE_MAZE:
			return _build_large_maze(x, y, map_size)
		PatternType.LARGE_TOWERS:
			return _build_large_towers(x, y, map_size)
		PatternType.LARGE_STRONGHOLD:
			return _build_large_stronghold(x, y, map_size)
		_:
			return TileRule.TileType.BASIC

func get_pattern_name(pattern: PatternType) -> String:
	"""Get a descriptive name for a pattern"""
	match pattern:
		PatternType.SMALL_FORTRESS:
			return "Small Fortress"
		PatternType.SMALL_ARENA:
			return "Small Arena"
		PatternType.MEDIUM_COURTYARD:
			return "Medium Courtyard"
		PatternType.MEDIUM_WALLS:
			return "Medium Walls"
		PatternType.CORNER_TOWERS:
			return "Corner Towers"
		PatternType.LARGE_MAZE:
			return "Large Maze"
		PatternType.LARGE_TOWERS:
			return "Large Towers"
		PatternType.LARGE_STRONGHOLD:
			return "Large Stronghold"
		PatternType.CORNER_TOWERS:
			return "Corner Towers"
		_:
			return "Unknown Pattern"

# Pattern Building Functions

func _build_small_fortress(x: int, y: int, map_size: Vector2i) -> TileRule.TileType:
	"""Small fortress: 1F walls around border, basic interior"""
	if _is_border(x, y, map_size):
		return TileRule.TileType.ARCH_1F
	return TileRule.TileType.BASIC

func _build_small_arena(x: int, y: int, map_size: Vector2i) -> TileRule.TileType:
	"""Small arena: 2F towers at corners, basic elsewhere"""
	if _is_corner(x, y, map_size):
		return TileRule.TileType.ARCH_2F
	return TileRule.TileType.BASIC

func _build_medium_courtyard(x: int, y: int, map_size: Vector2i) -> TileRule.TileType:
	"""Medium courtyard: 3F outer walls, 1F inner border, basic center"""
	if _is_border(x, y, map_size):
		return TileRule.TileType.ARCH_3F
	elif _is_inner_border(x, y, map_size):
		return TileRule.TileType.ARCH_1F
	return TileRule.TileType.BASIC

func _build_medium_walls(x: int, y: int, map_size: Vector2i) -> TileRule.TileType:
	"""Medium walls: 2F corners, 1F borders, basic interior"""
	if _is_corner(x, y, map_size):
		return TileRule.TileType.ARCH_2F
	elif _is_border(x, y, map_size):
		return TileRule.TileType.ARCH_1F
	return TileRule.TileType.BASIC

func _build_corner_towers(x: int, y: int, map_size: Vector2i) -> TileRule.TileType:
	"""Corner Walls: from corners, 3f -> 2f -> 1f"""
	if _is_corner(x, y, map_size):
		return TileRule.TileType.ARCH_3F
	elif _is_sub_corner(x, y, map_size):
		return TileRule.TileType.ARCH_2F
	elif _is_sub_sub_corner(x, y, map_size):
		return TileRule.TileType.ARCH_1F
	return TileRule.TileType.BASIC

func _build_large_maze(x: int, y: int, map_size: Vector2i) -> TileRule.TileType:
	"""Large maze: Varying wall heights creating maze-like patterns"""
	if _is_border(x, y, map_size):
		return TileRule.TileType.ARCH_3F
	elif y == 1 or y == map_size.y - 2:  # North/South inner edges
		return TileRule.TileType.ARCH_2F
	elif x == 1 or x == map_size.x - 2:  # East/West inner edges
		return TileRule.TileType.ARCH_1F
	return TileRule.TileType.BASIC

func _build_large_towers(x: int, y: int, map_size: Vector2i) -> TileRule.TileType:
	"""Large towers: Only 3F corner towers, basic everywhere else"""
	if _is_corner(x, y, map_size):
		return TileRule.TileType.ARCH_3F
	return TileRule.TileType.BASIC

func _build_large_stronghold(x: int, y: int, map_size: Vector2i) -> TileRule.TileType:
	"""Large stronghold: 3F corners, 2F borders, 1F inner border, basic center"""
	if _is_corner(x, y, map_size):
		return TileRule.TileType.ARCH_3F
	elif _is_border(x, y, map_size):
		return TileRule.TileType.ARCH_2F
	elif _is_inner_border(x, y, map_size):
		return TileRule.TileType.ARCH_1F
	return TileRule.TileType.BASIC

# Helper Functions for Position Detection

func _is_border(x: int, y: int, map_size: Vector2i) -> bool:
	return x == 0 or y == 0 or x == map_size.x - 1 or y == map_size.y - 1

func _is_corner(x: int, y: int, map_size: Vector2i) -> bool:
	return (x == 0 or x == map_size.x - 1) and (y == 0 or y == map_size.y - 1)

func _is_sub_corner(x: int, y: int, map_size: Vector2i) -> bool:
	if (x == 0 and (y == 1 or y == map_size.y - 2)):
		return true
	elif (x == 1 and (y == 0 or y == 1 or y == map_size.y - 1 or y == map_size.y - 2)):
		return true
	elif (x == map_size.x - 2 and (y == 0 or y == 1 or y == map_size.y - 1 or y == map_size.y - 2)):
		return true
	elif (x == map_size.x - 1 and (y == 1 or y == map_size.y - 2)):
		return true
	return false

func _is_sub_sub_corner(x: int, y: int, map_size: Vector2i) -> bool:
	if (x == 0 and (y == 2 or y == map_size.y - 3)):
		return true
	elif (x == 1 and (y == 2 or y == map_size.y - 3)):
		return true
	elif (x == 2 and (y == 0 or y == 1 or y == 2 or y == map_size.y - 3 or y == map_size.y - 2 or y == map_size.y - 1)):
		return true
	elif (x == map_size.x - 3 and (y == 0 or y == 1 or y == 2 or y == map_size.y - 3 or y == map_size.y - 2 or y == map_size.y - 1)):
		return true
	elif (x == map_size.x - 2 and (y == 2 or y == map_size.y - 3)):
		return true
	elif (x == map_size.x - 1 and (y == 2 or y == map_size.y - 3)):
		return true
	return false

func _is_center(x: int, y: int, map_size: Vector2i) -> bool:
	@warning_ignore("INTEGER_DIVISION")
	var center_x = map_size.x / 2
	@warning_ignore("INTEGER_DIVISION") 
	var center_y = map_size.y / 2
	return abs(x - center_x) <= 1 and abs(y - center_y) <= 1

func _is_inner_border(x: int, y: int, map_size: Vector2i) -> bool:
	return ((x == 1 or x == map_size.x - 2) or (y == 1 or y == map_size.y - 2)) and not _is_border(x, y, map_size)

func _is_inner_center(x: int, y: int, map_size: Vector2i) -> bool:
	return not _is_border(x, y, map_size) and not _is_inner_border(x, y, map_size)

# DEBUG
func test_pattern_system():
	"""Test function to check pattern selection"""
	print("=== TESTING NEW PATTERN SYSTEM ===")
	
	var test_small = Vector2i(4, 4)
	var test_medium = Vector2i(8, 8)
	var test_large = Vector2i(15, 15)
	
	var small_pattern = select_pattern(test_small)
	var medium_pattern = select_pattern(test_medium)
	var large_pattern = select_pattern(test_large)
	
	print("Pattern for 4x4: ", get_pattern_name(small_pattern))
	print("Pattern for 8x8: ", get_pattern_name(medium_pattern))
	print("Pattern for 15x15: ", get_pattern_name(large_pattern))
	
	print("=== END PATTERN TEST ===")
