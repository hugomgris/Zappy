extends Node

# Quick script to check and fix renderer settings
func _ready():
	print("=== RENDERER DIAGNOSTICS ===")
	
	# Check current renderer
	print("Current Renderer: ", RenderingServer.get_rendering_method())
	
	# Check project settings
	var rendering_method = ProjectSettings.get_setting("rendering/renderer/rendering_method", "")
	print("Project Rendering Method: ", rendering_method)
	
	# Force Forward+ if needed
	if rendering_method != "forward_plus":
		print("WARNING: Renderer not set to Forward+")
		print("Please set Project Settings > Rendering > Renderer > Rendering Method to 'Forward Plus'")
	
	# Check if Forward+ is actually available
	var available_methods = RenderingServer.get_rendering_method_list()
	print("Available rendering methods: ", available_methods)
	
	print("=== END DIAGNOSTICS ===")
