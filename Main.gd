extends Node3D

@onready var network = $NetworkManager
@onready var map_root = $MapRoot
@onready var player_root = $PlayerRoot

var map_size = Vector2i(0, 0)
var tiles = {}
var players = {}

func _ready():
	network.connect("line_received", _on_server_line)

	await get_tree().create_timer(0.5).timeout
	_on_server_line("msz 10 10")
	await get_tree().create_timer(0.5).timeout
	_on_server_line("pnw 1 2 3 1")
	await get_tree().create_timer(1.0).timeout
	_on_server_line("ppo 1 4 4 1")

func _on_server_line(line):
	print("Zappy says:", line)
	var tokens = line.split(" ")
	match tokens[0]:
		"msz":
			map_size.x = int(tokens[1])
			map_size.y = int(tokens[2])
			_generate_map()
		"pnw":
			_spawn_player(tokens[1], int(tokens[2]), int(tokens[3]), int(tokens[4]))
		"ppo":
			_move_player(tokens[1], int(tokens[2]), int(tokens[3]), int(tokens[4]))

func _generate_map():
	# Clear existing tiles if any
	for child in map_root.get_children():
		child.queue_free()
	tiles.clear()

	var tile_size = 1.0
	var gap = 0.1
	var spacing = tile_size + gap

	for x in range(map_size.x):
		for y in range(map_size.y):
			var tile_scene = preload("res://scenes/Tile.tscn").instantiate()
			tile_scene.position = Vector3(x * spacing, 0, y * spacing)
			map_root.add_child(tile_scene)
			tiles[Vector2i(x, y)] = tile_scene

			# Assign per-tile color
			var mesh_instance := tile_scene.get_node("MeshInstance3D")
			if mesh_instance and mesh_instance.material_override:
				var new_material := mesh_instance.material_override.duplicate() as StandardMaterial3D
				new_material.albedo_color = Color(
					float(x) / map_size.x,
					float(y) / map_size.y,
					1.0
				)
				mesh_instance.material_override = new_material



func _spawn_player(id, x, y, orientation):
	if players.has(id):
		return
	var player_scene = preload("res://scenes/Player.tscn").instantiate()
	player_scene.position = Vector3(x, 2, y)
	player_root.add_child(player_scene)
	players[id] = player_scene

func _move_player(id, x, y, orientation):
	if players.has(id):
		players[id].position = players[id].position.lerp(Vector3(x, 0.5, y), 0.2)
