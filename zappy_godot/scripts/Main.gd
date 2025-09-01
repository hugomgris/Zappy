extends Node3D

# Node references
@onready var network_manager = $NetworkManager
@onready var map_root = $MapRoot
@onready var player_root = $PlayerRoot
@onready var ui = $UI
@onready var camera = $Camera

# Manager components
@onready var world_manager = $WorldManager
@onready var player_manager = $PlayerManager

func _ready():
	# Initialize managers
	_setup_managers()
	
	# Connect to network signals
	network_manager.connect("line_received", _on_server_line)
	
	# Test with sample data (remove this when connecting to real server)
	_load_test_data()

func _setup_managers():
	"""Initialize all manager components"""
	# Initialize world manager
	world_manager.initialize(map_root)
	
	# Initialize player manager
	player_manager.initialize(player_root, world_manager)
	
	print("Zappy GUI initialized successfully")

func _load_test_data():
	"""Load test data for demonstration (remove when connecting to real server)"""
	await get_tree().create_timer(1.0).timeout
	
	# Load the sample JSON data
	var file = FileAccess.open("res://json_examples/server2observer/game_10x10.json", FileAccess.READ)
	if file:
		var json_string = file.get_as_text()
		file.close()
		
		var json = JSON.new()
		var parse_result = json.parse(json_string)
		if parse_result == OK:
			GameData.update_game_state(json.data)
			print("Test data loaded successfully")
		else:
			print("Error parsing JSON: ", json.get_error_message())
	else:
		print("Could not load test data file")

func _on_server_line(line: String):
	"""Handle incoming server messages"""
	print("Zappy server says:", line)
	
	# Try to parse as JSON first (for game state updates)
	var json = JSON.new()
	var parse_result = json.parse(line)
	if parse_result == OK:
		GameData.update_game_state(json.data)
		return
	
	# Otherwise, parse as command tokens (for individual commands)
	var tokens = line.split(" ")
	_handle_server_command(tokens)

func _handle_server_command(tokens: Array):
	"""Handle individual server commands"""
	if tokens.is_empty():
		return
		
	match tokens[0]:
		"msz":
			# Map size - could trigger a map regeneration
			if tokens.size() >= 3:
				var new_size = Vector2i(int(tokens[1]), int(tokens[2]))
				if GameData.map_size != new_size:
					GameData.map_size = new_size
					print("Map size updated: ", new_size)
		"pnw":
			# New player - will be handled by JSON updates
			print("New player detected")
		"ppo":
			# Player position - will be handled by JSON updates
			print("Player position update")
		_:
			print("Unknown command: ", tokens[0])

# Public interface for UI and other systems
func connect_to_server():
	"""Connect to the Zappy server"""
	network_manager.connect_to_server()

func get_game_stats() -> Dictionary:
	"""Get current game statistics for UI"""
	return {
		"map_size": GameData.map_size,
		"player_count": player_manager.get_player_count(),
		"tick": GameData.game_info.get("tick", 0),
		"teams": GameData.teams
	}
