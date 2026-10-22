// A particle sprite (effects.gd): a camera-facing quad, its texel times the particle's colour
// (128 = as stored), added to the screen (the PS1's semi-transparent B+F).
shader_type spatial;
render_mode unshaded, cull_disabled, blend_add, depth_draw_never;

uniform sampler2D tex : filter_nearest, repeat_disable;
instance uniform vec4 tint = vec4(1.0);

void vertex() {
	MODELVIEW_MATRIX = VIEW_MATRIX * mat4(INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);
	MODELVIEW_MATRIX = MODELVIEW_MATRIX * mat4(vec4(length(MODEL_MATRIX[0].xyz), 0.0, 0.0, 0.0), vec4(0.0, length(MODEL_MATRIX[1].xyz), 0.0, 0.0), vec4(0.0, 0.0, length(MODEL_MATRIX[2].xyz), 0.0), vec4(0.0, 0.0, 0.0, 1.0));
}

void fragment() {
	vec4 t = texture(tex, UV);
	if (t.a < 0.01) {
		discard;
	}
	ALBEDO = clamp(t.rgb * tint.rgb, 0.0, 1.0);
}
