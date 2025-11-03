shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_disabled, diffuse_burley, specular_schlick_ggx;

uniform float aberration: hint_range(0.0, 1.5, 0.001) = 0.5;
uniform float strength: hint_range(0.0, 0.1, 0.001) = 0.05;

// CRT Effects
uniform float scanline_intensity : hint_range(0.0, 1.0) = 0.3;
uniform float scanline_thickness : hint_range(0.1, 5.0) = 1.0;
uniform float noise_intensity : hint_range(0.0, 1.0) = 0.1;
uniform float grain_intensity : hint_range(0.0, 1.0) = 0.05;
uniform float flicker_intensity : hint_range(0.0, 1.0) = 0.02;

varying vec2 screen_uv;

void vertex() {
    POSITION = vec4(VERTEX, 1.0);
    screen_uv = (VERTEX.xy + 1.0) * 0.5;
}

// Noise function for film grain and static
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

// Noise with time for animated static
float noise(vec2 st, float time) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    float a = random(i + time);
    float b = random(i + vec2(1.0, 0.0) + time);
    float c = random(i + vec2(0.0, 1.0) + time);
    float d = random(i + vec2(1.0, 1.0) + time);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

void fragment() {
    vec2 suv = screen_uv;
    
    // Calculate aspect ratio from screen size
    vec2 screen_size = 1.0 / SCREEN_PIXEL_SIZE;
    float aspect_ratio = screen_size.y / screen_size.x;
    
    vec2 scaled_suv = (suv - vec2(0.0, 0.5)) / vec2(1.0, aspect_ratio) + vec2(0, 0.5);
    vec2 offset = normalize(scaled_suv) * strength;
    vec2 aberration_vec = offset * aberration * strength;

    // Sample the screen texture with chromatic aberration
    vec4 red = textureLod(SCREEN_TEXTURE, suv - aberration_vec, 0.0);
    vec4 green = textureLod(SCREEN_TEXTURE, suv + aberration_vec, 0.0);
    vec4 blue = textureLod(SCREEN_TEXTURE, suv, 0.0);

    vec3 final_color = vec3(red.r, green.g, blue.b);
    
    // Add CRT effects
    float time = TIME;
    
    // Scanlines
    float scanline = sin(suv.y * screen_size.y * scanline_thickness) * scanline_intensity;
    final_color *= 1.0 - scanline;
    
    // Film grain
    float grain = (random(suv + time) - 0.5) * grain_intensity;
    final_color += grain;
    
    // Static noise
    float static_noise = noise(suv * 100.0, time * 0.1) * noise_intensity;
    final_color += static_noise;
    
    // Screen flicker
    float flicker = (sin(time * 15.0) + 1.0) * 0.5 * flicker_intensity;
    final_color *= 1.0 - flicker;

    ALBEDO = final_color;
    ALPHA = 1.0;
}
