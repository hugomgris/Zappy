extends Node3D

# Node references
@onready var network_manager = $NetworkManager
@onready var map_root = $MapRoot
@onready var player_root = $PlayerRoot
@onready var egg_root = $EggRoot
@onready var ui = $UI
@onready var camera = $Camera
@onready var camera_controller = $CameraPosition

# Manager components
@onready var world_manager = $WorldManager
@onready var player_manager = $PlayerManager

@export var tile_size: float = 1.0
@export var gap: float = 0.0

func _ready():
	CommandProcessor.set_tile_size_and_gap(tile_size, gap)

	# Initialize managers
	_setup_managers()
	
	# Connect to network signals
	network_manager.connect("line_received", _on_server_line)
	
	# Test data -> Needs to be connected to server sent data
	_load_test_data()
	
	# Debug signals
	CommandProcessor.command_processed.connect(_on_command_processed)
	CommandProcessor.command_failed.connect(_on_command_failed)
	CommandProcessor.player_orientation_change.connect(_on_player_orientation_changed)

func _setup_managers():
	"""Initialize all manager components"""
	await world_manager.initialize(map_root, ui, tile_size, gap)
	
	# Initialize player manager
	player_manager.initialize(player_root, world_manager, egg_root)

	GameData.connect("game_state_updated", _on_game_state_loaded)
	
	print("Zappy GUI initialized successfully")

func _on_game_state_loaded():
	"""Called when game state is loaded - initialize camera position"""
	if GameData.map_size.x > 0 and GameData.map_size.y > 0:
		camera_controller.initialize_camera_for_map(GameData.map_size)

func _load_test_data():
	"""Load test data for demonstration (remove when connecting to real server)"""
	await get_tree().create_timer(1.0).timeout
	
	# Load the sample JSON data
	var file = FileAccess.open("res://data/initial_data/game_3x3.json", FileAccess.READ)
	if file:
		var json_string = file.get_as_text()
		file.close()
		
		var json = JSON.new()
		var parse_result = json.parse(json_string)
		if parse_result == OK:
			GameData.update_game_state(json.data)
			print("Test data loaded successfully")
			
			# NOW start MockServer after data is loaded
			MockServer.initialize()
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
	"""Handle individual server commands. Placeholder until server connection is coded."""
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

func _on_player_orientation_changed(player_id: int, new_orientation: int) -> void:
	pass

# DEBUG 
func _on_command_processed(command_type: String, player_id: int):
	print ("[COMMAND] ", player_id, " -> ", command_type)

func _on_command_failed(command_type: String, error: String):
	print ("Command failed: ", command_type, " - ", error)
	
