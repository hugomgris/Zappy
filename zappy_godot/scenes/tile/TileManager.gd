extends Area3D

@onready var marker_3d: Marker3D = %Marker3D

var resource_positions : Array[Array] = []
var available_resources: Dictionary = {
		"nourriture": 0,
		"linemate": 0,
		"deraumere": 0,
		"sibur": 0,
		"mendiane": 0,
		"phiras": 0,
		"thystame": 0
	}

var ui_ref
var hovered_tile_x: int
var hovered_tile_y: int

func _ready() -> void:
	setup_resource_grid()

func setup_resource_grid():
	resource_positions = []
	for x in range(3):
		resource_positions.append([])	
		for y in range(3):
			resource_positions[x].append(true)

func setup_tile_hover_signals(tile_data, ui_reference, x: int, y: int):
	ui_ref = ui_reference
	hovered_tile_x = x
	hovered_tile_y = y

	if mouse_entered.is_connected(_on_tile_area_mouse_entered):
		mouse_entered.disconnect(_on_tile_area_mouse_entered)
	
	mouse_entered.connect(_on_tile_area_mouse_entered.bind(tile_data))
	mouse_exited.connect(_on_tile_area_mouse_exited.bind())
	
func _on_tile_area_mouse_entered(tile_data):
	if ui_ref and ui_ref.has_method("update_ui_tile_stats"):
		ui_ref.update_ui_tile_stats(tile_data, hovered_tile_x, hovered_tile_y)

func _on_tile_area_mouse_exited():
	if ui_ref and ui_ref.has_method("hide_tile_info"):
		ui_ref.hide_tile_info()
		
func is_position_available(x: int, y: int) -> bool: return resource_positions[x][y]

func occupy_position(x: int, y: int) -> void: resource_positions[x][y] = false

func get_position_values(raw_x: int, raw_y: int) -> Vector2: return Vector2((raw_x * 0.25) + 0.25, (raw_y * 0.25) + 0.25)
