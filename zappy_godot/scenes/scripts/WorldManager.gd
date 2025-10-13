extends Node3D

@onready var map_root: Node3D
@export var tile_size = 1.0
@export var gap = 0.1
var tiles = {}
var ui_reference
var world_ready_emitted = false

signal world_ready

func _ready():
	# Connect to GameData signals
	GameData.connect("game_state_updated", _on_game_state_updated)
	GameData.connect("tile_updated", _on_tile_updated)

func initialize(map_root_node: Node3D, ui_node: Control):
	"""Initialize the world manager with the map root node"""
	map_root = map_root_node
	ui_reference = ui_node
	# Don't emit world_ready here - wait until map is actually generated

func _on_game_state_updated():
	"""Regenerate the entire world when game state updates"""
	_generate_map()

func _on_tile_updated(x: int, y: int):
	"""Update a specific tile when its data changes"""
	pass

func _generate_map():
	"""Generate the visual representation of the map"""
	if not map_root:
		return

	world_ready_emitted = false
		
	for child in map_root.get_children():
		child.queue_free()
	tiles.clear()

	var spacing = tile_size + gap
	for x in range(GameData.map_size.x):
		for y in range(GameData.map_size.y):
			var tile_scene = preload("res://scenes/tile/tile.tscn").instantiate()
			tile_scene.position = Vector3(x * spacing, 0, y * spacing)
			map_root.add_child(tile_scene)
			tiles[Vector2i(x, y)] = tile_scene
			
			_set_up_tile(x, y)
	
	if not world_ready_emitted:
		world_ready_emitted = true
		emit_signal("world_ready")

func _set_up_tile(x: int,y: int):
	var tile_pos = Vector2i(x, y)
	if not tiles.has(tile_pos):
		return

	var tile_scene = tiles[tile_pos]
	
	var tile_data = GameData.get_tile_data(x, y)
	if not tile_data:
		return

	tile_scene.setup_tile_hover_signals(tile_data, ui_reference, x, y)
	
	# Color managament -> checkerboard pattern
	var tile_mesh = tile_scene.get_node("MeshInstance3D") as MeshInstance3D
	if (tile_mesh and (x % 2 == 0 and y % 2 != 0) or tile_mesh and (x % 2 != 0 and y % 2 == 0)):
		var new_material := tile_mesh.material_override.duplicate() as StandardMaterial3D
		
		var current_color = new_material.albedo_color
		var darker_color = current_color.darkened(0.5)

		new_material.albedo_color = darker_color
		tile_mesh.material_override = new_material
	
	# Resource placement
	for resource in tile_data.resources:
		if tile_data.resources[resource] > 0:
			tile_scene.available_resources[resource] = tile_data.resources[resource]

			var resource_scene
			
			match resource:
				"linemate":
					resource_scene = preload("res://scenes/resources/linemateResource.tscn").instantiate();
					resource_scene.get_node("linemate").setup_resource_hover_signals(ui_reference, "linemate")
				"deraumere":
					resource_scene = preload("res://scenes/resources/deraumereResource.tscn").instantiate();
					resource_scene.get_node("deraumere").setup_resource_hover_signals(ui_reference, "deraumere")
				"sibur":
					resource_scene = preload("res://scenes/resources/siburResource.tscn").instantiate();
					resource_scene.get_node("sibur").setup_resource_hover_signals(ui_reference, "sibur")
				"mendiane":
					resource_scene = preload("res://scenes/resources/mendianeResource.tscn").instantiate();
					resource_scene.get_node("mendiane").setup_resource_hover_signals(ui_reference, "mendiane")
				"phiras":
					resource_scene = preload("res://scenes/resources/phirasResource.tscn").instantiate();
					resource_scene.get_node("phiras").setup_resource_hover_signals(ui_reference, "phiras")
				"thystame":
					resource_scene = preload("res://scenes/resources/thystameResource.tscn").instantiate();
					resource_scene.get_node("thystame").setup_resource_hover_signals(ui_reference, "thystame")
				"nourriture":
					resource_scene = preload("res://scenes/resources/nourritureResource.tscn").instantiate();
					resource_scene.get_node("nourriture").setup_nourriture_hover_signals(ui_reference)
				_:
					resource_scene = preload("res://scenes/resources/linemateResource.tscn").instantiate();
					resource_scene.get_node("linemate").setup_resource_hover_signals(ui_reference, "linemate")
						
			if (resource_scene):
				var quantity = tile_scene.available_resources[resource]
				var scale_factor = 0.5 + (quantity - 1) * 0.2  # Increase multiplier for more visible effect
				resource_scene.scale = Vector3(scale_factor, scale_factor, scale_factor)
				
				place_resource_in_tile(tile_scene, resource_scene, scale_factor)

func place_resource_in_tile(tile_scene: Node3D, resource_scene: Node3D, scale_factor: float) -> void:
	var position_index = tile_scene.get_resource_available_position_index()
	
	if position_index == -1:
		print("Warning: No available positions in tile for resource")
		return
	
	tile_scene.occupy_resource_position(position_index, resource_scene, scale_factor)

func get_world_position(grid_x: int, grid_y: int) -> Vector3:
	"""Convert grid coordinates to world position"""
	var spacing = tile_size + gap
	return Vector3(grid_x * spacing, 0, grid_y * spacing)

func get_tile_size_with_gap() -> float:
	"""Get the total size of a tile including gap"""
	return tile_size + gap
