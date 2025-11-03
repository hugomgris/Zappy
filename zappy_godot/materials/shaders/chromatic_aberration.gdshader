shader_type canvas_item;
uniform sampler2D screen_tex: hint_screen_texture, filter_linear;

// Chromatic Aberration Controls
uniform float aberration: hint_range(0.0, 1.5, 0.001) = 0.5;
uniform float strength: hint_range(0.0, 0.1, 0.001) = 0.05;

// Paper Effect Controls
uniform float paper_grain_intensity: hint_range(0.0, 1.0, 0.01) = 0.3;
uniform float paper_grain_scale: hint_range(1.0, 50.0, 0.1) = 15.0;
uniform vec3 paper_color: source_color = vec3(0.95, 0.93, 0.85);
uniform float paper_color_intensity: hint_range(0.0, 1.0, 0.01) = 0.2;
uniform float vignette_intensity: hint_range(0.0, 1.0, 0.01) = 0.3;
uniform float vignette_smoothness: hint_range(0.1, 2.0, 0.01) = 0.8;

// Noise function for paper grain
float noise(vec2 uv) {
	return fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453);
}

// Multi-octave noise for more realistic paper texture
float paper_noise(vec2 uv) {
	float n = 0.0;
	n += 0.5 * noise(uv * paper_grain_scale);
	n += 0.25 * noise(uv * paper_grain_scale * 2.0);
	n += 0.125 * noise(uv * paper_grain_scale * 4.0);
	n += 0.0625 * noise(uv * paper_grain_scale * 8.0);
	return n;
}

// Vignette function
float vignette(vec2 uv) {
	vec2 center = uv - 0.5;
	float dist = length(center) * 2.0; // Scale to 0-1 range from center to corner
	float vignette_start = 1.0 - vignette_intensity;
	float vignette_end = vignette_start - (vignette_intensity * vignette_smoothness);
	return smoothstep(vignette_end, vignette_start, 1.0 - dist);
}

void fragment() {
	vec2 suv = SCREEN_UV;
	float aspect_ratio = SCREEN_PIXEL_SIZE.y / SCREEN_PIXEL_SIZE.x;
	vec2 scaled_suv = (suv - vec2(0.0, 0.5)) / vec2(1.0, aspect_ratio) + vec2(0, 0.5);
	vec2 offset = normalize(scaled_suv) * strength;
	vec2 aberration_vec = offset * aberration * strength;

	// Apply chromatic aberration
	vec4 red = texture(screen_tex, suv - aberration_vec);
	vec4 green = texture(screen_tex, suv + aberration_vec);
	vec4 blue = texture(screen_tex, suv);
	vec3 aberrated_color = vec3(red.r, green.g, blue.b);

	// Generate paper texture
	float paper_grain = paper_noise(suv);
	
	// Apply paper grain as brightness modulation
	vec3 paper_effect = aberrated_color * (1.0 + (paper_grain - 0.5) * paper_grain_intensity);
	
	// Mix with paper color tint
	vec3 paper_tinted = mix(paper_effect, paper_effect * paper_color, paper_color_intensity);
	
	// Apply vignette
	float vig = vignette(suv);
	vec3 final_color = paper_tinted * vig;
	
	COLOR = vec4(final_color, 1.0);
}