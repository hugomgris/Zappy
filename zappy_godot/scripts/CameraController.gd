# CameraController.gd
extends Camera3D

var map_center: Vector3
var base_camera_distance: float = 15.0
var camera_distance: float = 15.0
var camera_height: float = 12.0
var rotation_angle: float = 0.0
var rotation_speed: float = 45.0  # degrees per second

# Zoom settings
var min_zoom_factor: float = 0.3
var max_zoom_factor: float = 3.0
var current_zoom_factor: float = 1.0
var zoom_speed: float = 0.1

func _ready():
	# Connect to GameData to know when map size changes
	GameData.connect("game_state_updated", _on_game_state_updated)

func _on_game_state_updated():
	"""Update camera position when game state changes"""
	update_camera_for_map_size()

func update_camera_for_map_size():
	"""Position camera based on current map size"""
	var map_size = GameData.map_size
	if map_size.x == 0 or map_size.y == 0:
		return
	
	# Calculate map center point
	var tile_size = 1.0
	var gap = 0.1
	var spacing = tile_size + gap
	
	map_center = Vector3(
		(map_size.x - 1) * spacing * 0.5,
		0,
		(map_size.y - 1) * spacing * 0.5
	)
	
	# Adjust distance based on map size for optimal viewing
	var max_dimension = max(map_size.x, map_size.y)
	base_camera_distance = max_dimension * 1.5 + 5.0
	camera_height = max_dimension * 1.2 + 3.0
	
	# Apply current zoom factor
	camera_distance = base_camera_distance * current_zoom_factor
	
	# Update camera position
	update_camera_position()

func update_camera_position():
	"""Update camera position based on current rotation angle"""
	var angle_rad = deg_to_rad(rotation_angle)
	
	# Calculate position in a circle around the map center
	var camera_pos = Vector3(
		map_center.x + cos(angle_rad) * camera_distance,
		camera_height,
		map_center.z + sin(angle_rad) * camera_distance
	)
	
	# Set camera position
	position = camera_pos
	
	# Look at the center of the map
	look_at(map_center, Vector3.UP)

func _input(event):
	"""Handle camera rotation and zoom input"""
	if event is InputEventKey and event.pressed:
		match event.keycode:
			KEY_Q:
				# Rotate counterclockwise
				rotate_camera(-rotation_speed)
			KEY_E:
				# Rotate clockwise
				rotate_camera(rotation_speed)
	
	elif event is InputEventMouseButton:
		if event.pressed:
			match event.button_index:
				MOUSE_BUTTON_WHEEL_UP:
					# Zoom in
					zoom_camera(-zoom_speed)
				MOUSE_BUTTON_WHEEL_DOWN:
					# Zoom out
					zoom_camera(zoom_speed)

func rotate_camera(degrees: float):
	"""Rotate camera around the map center"""
	rotation_angle += degrees
	
	# Keep angle in 0-360 range
	if rotation_angle >= 360:
		rotation_angle -= 360
	elif rotation_angle < 0:
		rotation_angle += 360
	
	# Smooth rotation using tween
	var tween = create_tween()
	var target_angle = deg_to_rad(rotation_angle)
	var target_pos = Vector3(
		map_center.x + cos(target_angle) * camera_distance,
		camera_height,
		map_center.z + sin(target_angle) * camera_distance
	)
	
	tween.parallel().tween_property(self, "position", target_pos, 0.3)
	tween.parallel().tween_method(_smooth_look_at, 0.0, 1.0, 0.3)

func zoom_camera(zoom_delta: float):
	"""Zoom camera in or out"""
	current_zoom_factor += zoom_delta
	current_zoom_factor = clamp(current_zoom_factor, min_zoom_factor, max_zoom_factor)
	
	# Update camera distance
	camera_distance = base_camera_distance * current_zoom_factor
	
	# Smooth zoom using tween
	var tween = create_tween()
	var target_angle = deg_to_rad(rotation_angle)
	var target_pos = Vector3(
		map_center.x + cos(target_angle) * camera_distance,
		camera_height * current_zoom_factor,  # Also adjust height slightly
		map_center.z + sin(target_angle) * camera_distance
	)
	
	tween.tween_property(self, "position", target_pos, 0.2)

func _smooth_look_at(progress: float):
	"""Smoothly update look_at during rotation"""
	look_at(map_center, Vector3.UP)
