// A particle sprite (effects.gd): a quad placed and turned by effects.gd, its texel times the particle's colour
// (128 = as stored), the transparent texel discarded.
shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_opaque;

uniform sampler2D tex : filter_nearest, repeat_disable;
instance uniform vec4 tint = vec4(1.0);


void fragment() {
	vec4 t = texture(tex, UV);
	if (t.a < 0.01) {
		discard;
	}
	ALBEDO = clamp(t.rgb * tint.rgb, 0.0, 1.0);
}
