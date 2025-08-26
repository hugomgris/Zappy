extends Node3D

@onready var network = $NetworkManager
@onready var map_root = $MapRoot
@onready var player_root = $PlayerRoot
@onready var ui = $UI
@onready var camera = $Camera

var tiles = {}
var players = {}
var tile_size = 1.0
var gap = 0.1

func _ready():
	# Connect to network and game data signals
	network.connect("line_received", _on_server_line)
	GameData.connect("game_state_updated", _on_game_state_updated)
	GameData.connect("player_updated", _on_player_updated)
	GameData.connect("tile_updated", _on_tile_updated)
	
	# Test with sample data (remove this when connecting to real server)
	_load_test_data()

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
		else:
			print("Error parsing JSON: ", json.get_error_message())
	else:
		print("Could not load test data file")

func _on_game_state_updated():
	"""Called when GameData updates - refresh the visual representation"""
	_generate_map()
	_update_all_players()

func _on_player_updated(player_id):
	"""Called when a specific player is updated"""
	_update_player_visual(player_id)

func _on_tile_updated(x, y):
	"""Called when a specific tile is updated"""
	_update_tile_visual(x, y)

func _generate_map():
	# Clear existing tiles if any
	for child in map_root.get_children():
		child.queue_free()
	tiles.clear()

	var tile_size = 1.0
	var gap = 0.1
	var spacing = tile_size + gap

	for x in range(GameData.map_size.x):
		for y in range(GameData.map_size.y):
			var tile_scene = preload("res://scenes/tile.tscn").instantiate()
			tile_scene.position = Vector3(x * spacing, 0, y * spacing)
			map_root.add_child(tile_scene)
			tiles[Vector2i(x, y)] = tile_scene
			
			# Update tile visual with current data
			_update_tile_visual(x, y)

func _on_server_line(line):
	"""Handle incoming server messages"""
	print("Zappy says:", line)
	
	# Try to parse as JSON first (for game state updates)
	var json = JSON.new()
	var parse_result = json.parse(line)
	if parse_result == OK:
		GameData.update_game_state(json.data)
		return
	
	# Otherwise, parse as command tokens (for individual commands)
	var tokens = line.split(" ")
	match tokens[0]:
		"msz":
			# Map size - could trigger a map regeneration
			var new_size = Vector2i(int(tokens[1]), int(tokens[2]))
			if GameData.map_size != new_size:
				GameData.map_size = new_size
				_generate_map()
		"pnw":
			# New player
			pass # This will be handled by JSON updates
		"ppo":
			# Player position
			pass # This will be handled by JSON updates
		_:
			print("Unknown command: ", tokens[0])

func _update_tile_visual(x: int, y: int):
	"""Update a single tile's visual representation"""
	var tile_pos = Vector2i(x, y)
	if not tiles.has(tile_pos):
		return
		
	var tile_scene = tiles[tile_pos]
	var tile_data = GameData.get_tile_data(x, y)
	
	if not tile_data:
		return
	
	# Update tile color based on resources
	var mesh_instance := tile_scene.get_node("MeshInstance3D") as MeshInstance3D
	if mesh_instance and mesh_instance.material_override:
		var new_material := mesh_instance.material_override.duplicate() as StandardMaterial3D
		
		# Color based on most abundant resource
		var max_resource = 0
		var dominant_resource = ""
		for resource in tile_data.resources:
			if tile_data.resources[resource] > max_resource:
				max_resource = tile_data.resources[resource]
				dominant_resource = resource
		
		# Assign colors based on resource type
		var resource_colors = {
			"nourriture": Color.GREEN,
			"linemate": Color.YELLOW,
			"deraumere": Color.BLUE,
			"sibur": Color.RED,
			"mendiane": Color.PURPLE,
			"phiras": Color.ORANGE,
			"thystame": Color.CYAN
		}
		
		if dominant_resource != "" and max_resource > 0:
			var base_color = resource_colors.get(dominant_resource, Color.GRAY)
			var intensity = min(float(max_resource) / 3.0, 1.0)  # Scale intensity
			new_material.albedo_color = base_color * intensity + Color.WHITE * (1.0 - intensity)
		else:
			# Default color for empty tiles
			new_material.albedo_color = Color(0.3, 0.3, 0.3)
		
		mesh_instance.material_override = new_material

func _update_all_players():
	"""Update all player visuals"""	
	# Clear existing players
	for child in player_root.get_children():
		child.queue_free()
	players.clear()
	
	# Create new player visuals
	for player_id in GameData.players:
		_create_player_visual(player_id)

func _create_player_visual(player_id: int):
	"""Create visual representation for a player"""
	var player_data = GameData.get_player_data(player_id)
	if not player_data:
		return
	
	var player_scene = preload("res://scenes/Player.tscn").instantiate()
	var pos = player_data.position
	var world_pos = Vector3(pos.x * (tile_size + gap), 0.5, pos.y * (tile_size + gap))
	player_scene.position = world_pos
	player_root.add_child(player_scene)
	players[player_id] = player_scene
	
	# Set player color based on team
	_update_player_visual(player_id)

func _update_player_visual(player_id: int):
	"""Update a specific player's visual representation"""
	if not players.has(player_id):
		_create_player_visual(player_id)
		return
	
	var player_data = GameData.get_player_data(player_id)
	if not player_data:
		return
	
	var player_scene = players[player_id]
	var pos = player_data.position
	var target_pos = Vector3(pos.x * (tile_size + gap), 0.5, pos.y * (tile_size + gap))
	var player_label = player_scene.get_node("Label3D") as Label3D
	
	# Smooth movement
	var tween = create_tween()
	tween.tween_property(player_scene, "position", target_pos, 0.3)
	
	# Update player label
	player_label.text = "Player_" + player_data.team + "_" + str(player_id)
	
	# Update player color based on team and status
	var mesh_instance := player_scene.get_node("MeshInstance3D") as MeshInstance3D
	if mesh_instance and mesh_instance.material_override:
		var new_material := mesh_instance.material_override.duplicate() as StandardMaterial3D
		
		# Team colors
		var team_colors = {
			"Alpha": Color.RED,
			"Beta": Color.BLUE,
			"Gamma": Color.GREEN,
			"Delta": Color.YELLOW
		}
		
		var base_color = team_colors.get(player_data.team, Color.WHITE)
		
		# Modify color based on status
		match player_data.status:
			"incantation":
				base_color = base_color.lerp(Color.WHITE, 0.5)  # Lighter for incantation
			"broadcasting":
				base_color = base_color.lerp(Color.YELLOW, 0.3)  # Yellowish for broadcasting
			_:
				pass  # Normal color
		
		new_material.albedo_color = base_color
		mesh_instance.material_override = new_material
