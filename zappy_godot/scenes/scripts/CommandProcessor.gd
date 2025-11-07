class_name CommandProcessor
extends Node

signal command_processed(command_type: String, player_id: int)
signal command_failed(command_type: String, error: String)
signal player_orientation_change(player_id, new_orientation)
signal player_position_change(player_id, current_orientation)

var tile_size: float
var gap: float
var command_queue: Array[Dictionary] = []
var data_ready: bool = false
var queue_timer: Timer
var processing_queue: bool = false

func _ready():
	GameData.connect("game_state_updated", _on_game_data_ready)

	queue_timer = Timer.new()
	queue_timer.wait_time = 0.5 
	queue_timer.timeout.connect(_process_next_queued_command)
	add_child(queue_timer)

func _on_game_data_ready():
	if not data_ready:
		data_ready = true
		print("GameData ready - starting queued command processing")
		if command_queue.size() > 0:
			_start_queue_processing()

func set_tile_size_and_gap(tile_s: float, tile_gap: float):
	tile_size = tile_s
	gap = tile_gap

func process_command(json_data: Dictionary) -> void:
	if not data_ready:
		command_queue.append(json_data)
		print("Command queued - waiting for GameData")
		return
	
	if processing_queue:
		command_queue.append(json_data)
		print("Command queued - processing existing queue")
		return
	
	_execute_command(json_data)

func _start_queue_processing():
	if command_queue.size() > 0:
		processing_queue = true
		print("Processing ", command_queue.size(), " queued commands with timing")
		queue_timer.start()
		_process_next_queued_command()

func _process_next_queued_command():
	if command_queue.size() == 0:
		processing_queue = false
		queue_timer.stop()
		print("Queue processing complete")
		return
	
	var command_data = command_queue.pop_front()
	_execute_command(command_data)

func _execute_command(json_data: Dictionary) -> void:
	var command_type = json_data.get("type", "")
	var player_id = json_data.get("player_id", -1)

	if (player_id == -1):
		command_failed.emit(command_type, "Invalid player ID")
		return

	match command_type:
		"avance":
			handle_avance(player_id)
		"gauche":
			handle_gauche(player_id)
		"droit":
			handle_droit(player_id)
		"prende":
			handle_prende(player_id, json_data.get("object", ""))
		"pose":
			handle_pose(player_id, json_data.get("object", ""))
		_:
			command_failed.emit(command_type, "Unknown command type")

func handle_avance(player_id: int) -> void:
	var player_data = GameData.get_player_data(player_id)
	if not player_data:
		print("Warning: Player ", player_id, " not found in GameData yet")
		command_failed.emit("gauche", "Player data not loaded")
		return

	var current_orientation = player_data.orientation
	player_position_change.emit(player_id, current_orientation, player_data, tile_size + gap)
	command_processed.emit("avance", player_id)

func handle_gauche(player_id: int) -> void:
	var player_data = GameData.get_player_data(player_id)
	if not player_data:
		print("Warning: Player ", player_id, " not found in GameData yet")
		command_failed.emit("gauche", "Player data not loaded")
		return

	var current_orientation = player_data.orientation
	var new_orientation = 1 if current_orientation == 4 else current_orientation + 1
	player_data.orientation = new_orientation
	player_orientation_change.emit(player_id, new_orientation)
	command_processed.emit("gauche", player_id)

func handle_droit(player_id: int) -> void:
	var player_data = GameData.get_player_data(player_id)
	if not player_data:
		print("Warning: Player ", player_id, " not found in GameData yet")
		command_failed.emit("gauche", "Player data not loaded")
		return

	var current_orientation = player_data.orientation
	var new_orientation = 4 if current_orientation == 1 else current_orientation - 1
	player_data.orientation = new_orientation
	player_orientation_change.emit(player_id, new_orientation)
	command_processed.emit("droit", player_id)

func handle_prende(player_id: int, object: String) -> void:
	pass

func handle_pose(player_id: int, object: String) -> void:
	pass
