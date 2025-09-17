extends Node3D

@onready var camera_rotation_x: Node3D = %CameraRotationX
@onready var camera_zoom_pivot: Node3D = %CameraZoomPivot
@onready var camera: Camera3D = %Camera
@onready var world_manager: Node3D = %WorldManager

@export var move_speed = 0.6
@export var zoom_speed = 12.0
@export var rotation_speed = 2.0
@export var lerp_speed = 0.05
@export var viewport_margin_ratio = 0.1  # 10% margin around the map
@export var isometric_angle = -5.0      # X rotation for isometric view
@export var default_y_rotation = 45.0 

# Movement limits (optional)
var movement_bounds = Rect2()

var move_target: Vector3
var zoom_target: float
var rotation_target: float
var is_initialized = false

func _ready() -> void:
	rotation_target = rotation.y

func initialize_camera_for_map(map_size: Vector2i):
	"""Initialize camera position and zoom based on map dimensions"""
	print("Initializing camera for map size: ", map_size)
	
	# Calculate map center in world coordinates
	var map_center = Vector3(
		(map_size.x - 1) * 0.5,  # Tiles are 1x1 without gaps
		0,
		(map_size.y - 1) * 0.5
	)
	
	# Set camera orientation for isometric view
	camera_rotation_x.rotation.x = deg_to_rad(isometric_angle)
	rotation.y = deg_to_rad(default_y_rotation)
	rotation_target = rotation.y
	
	# Position camera above map center
	position = map_center
	move_target = position
	
	# Calculate optimal zoom distance
	var optimal_zoom = _calculate_optimal_zoom(map_size)
	camera_zoom_pivot.position.z = optimal_zoom
	zoom_target = optimal_zoom
	
	# Set movement bounds based on map size (with some padding)
	var padding = max(map_size.x, map_size.y) * 0.5
	movement_bounds = Rect2(
		-padding, -padding,
		map_size.x + padding * 2, map_size.y + padding * 2
	)
	
	is_initialized = true
	print("Camera initialized - Position: ", position, " Zoom: ", optimal_zoom)
	print("=== Camera Debug ===")
	print("Map size: ", map_size)
	print("Camera FOV: ", camera.fov)
	print("Viewport size: ", get_viewport().get_visible_rect().size)
	print("Calculated zoom: ", optimal_zoom)
	print("Camera angle: ", isometric_angle)
	print("===================")

func _calculate_optimal_zoom(map_size: Vector2i) -> float:
	"""Calculate optimal zoom distance using empirical values"""
	
	var max_dimension = max(map_size.x, map_size.y)
	var base_distance: float
	
	# Empirical values that work well
	if max_dimension <= 3:
		base_distance = 4.0
	elif max_dimension <= 5:
		base_distance = 8.0
	elif max_dimension <= 10:
		base_distance = 12.0
	elif max_dimension <= 20:
		base_distance = 26.0
	else:
		base_distance = max_dimension * 1.8
	
	# Add margin
	var final_distance = base_distance * (1.0 + viewport_margin_ratio)
	
	print("Max dimension: ", max_dimension, " Base: ", base_distance, " Final: ", final_distance)
	
	return final_distance

func _input(event):
	if event is InputEventMouseButton:
		match event.button_index:
			MOUSE_BUTTON_WHEEL_UP:
				zoom_target -= zoom_speed * 0.1
			MOUSE_BUTTON_WHEEL_DOWN:
				zoom_target += zoom_speed * 0.1


func _process(delta: float) -> void:
	if not is_initialized:
		return
		
	_handle_movement(delta)
	_handle_zoom(delta)
	_handle_rotation(delta)

func _handle_movement(delta: float) -> void:
	var input_direction = Input.get_vector("left", "right", "up", "down")
	
	# Calculate movement relative to camera's Y rotation
	var movement_local = Vector3(input_direction.x, 0, input_direction.y) * move_speed
	var rotated_movement = movement_local.rotated(Vector3.UP, rotation.y)
	
	move_target += rotated_movement
	
	# Clamp to bounds
	if movement_bounds != Rect2():
		move_target.x = clamp(move_target.x, movement_bounds.position.x, movement_bounds.position.x + movement_bounds.size.x)
		move_target.z = clamp(move_target.z, movement_bounds.position.y, movement_bounds.position.y + movement_bounds.size.y)
	
	position = lerp(position, move_target, lerp_speed)

func _handle_zoom(delta: float) -> void:
	# Dynamic zoom limits based on map size
	var min_zoom = 2.0
	var max_zoom = max(20.0, zoom_target * 2.0)  # Allow zooming out beyond initial
	
	zoom_target = clamp(zoom_target, min_zoom, max_zoom)
	camera_zoom_pivot.position.z = lerp(camera_zoom_pivot.position.z, zoom_target, lerp_speed)

func _handle_rotation(delta: float) -> void:
	if Input.is_action_pressed("rotate_left"):
		rotation_target -= rotation_speed * delta
	if Input.is_action_pressed("rotate_right"):
		rotation_target += rotation_speed * delta
	
	rotation.y = lerp_angle(rotation.y, rotation_target, lerp_speed)

# Utility functions
func reset_camera_to_overview():
	"""Reset camera to show entire map"""
	if is_initialized and GameData.map_size.x > 0:
		initialize_camera_for_map(GameData.map_size)

func focus_on_position(world_pos: Vector3, zoom_level: float = -1):
	"""Focus camera on a specific world position"""
	move_target = world_pos
	if zoom_level > 0:
		zoom_target = zoom_level
