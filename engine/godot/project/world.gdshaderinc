// The PS1 world look: texture x vertex colour x 2 (128 is 1.0), a transparent texel (0x0000)
// discarded, vertex fog toward the level's fog colour from fog_near on (docs/re/formats.md, TSP).
// The colour goes out as the PS1 computed it: with the Compatibility renderer this matched the
// original's brightness side by side (a linear conversion made the level far darker).
uniform sampler2DArray pages : filter_nearest, repeat_disable;
uniform float fog_near = 0.0;  // world units, 0: no fog
uniform vec3 fog_color = vec3(0.0);
uniform float units_per_metre = 128.0;
uniform vec3 light_colours[40];  // the animated lights this tick (formats.md, Animated lights)
// The explosion light (objects.md, Explosions: The light; set by present/effects.gd from the engine):
// the face drawer (func_800114B0) adds light * (1 - d^2 / r^2) to each vertex colour within the
// radius, clamped at 255. And the scorches: on the frame an explosion is made (0x800AA160) it also
// darkens the vertex colours within the radius for good, colour x (1 - (1 - d^2 / r^2) / 8), at
// least 1. Only on the world (instance dyn_lit 1: present/ sets it on main.gd's world mesh).
uniform vec3 dyn_light_pos = vec3(0.0);  // Godot metres
uniform vec3 dyn_light_rgb = vec3(0.0);  // 0..1 of 255, the vertex colours' scale
uniform float dyn_light_r2 = 0.0;        // radius squared, game units; 0: none
uniform vec4 scorches[32];               // xyz Godot metres, w radius squared (game units); w 0: none
instance uniform float dyn_lit = 0.0;

varying flat float layer;
varying vec3 light_colour;
varying float lit;
varying float depth_units;
varying vec3 dyn_add;
varying float dyn_dark;

void vertex() {
    layer = UV2.x;
    int light = int(UV2.y + 0.5) - 1;
    lit = light >= 0 ? 1.0 : 0.0;
    light_colour = light >= 0 ? light_colours[light] : vec3(0.0);
    depth_units = -(MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).z * units_per_metre;
    dyn_add = vec3(0.0);
    dyn_dark = 1.0;
    if (dyn_lit > 0.5) {
        vec3 wp = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
        if (dyn_light_r2 > 0.0) {
            vec3 d = (wp - dyn_light_pos) * units_per_metre;
            float d2 = dot(d, d);
            if (d2 <= dyn_light_r2) {
                dyn_add = dyn_light_rgb * (1.0 - d2 / dyn_light_r2);
            }
        }
        for (int i = 0; i < 32; i++) {
            if (scorches[i].w > 0.0) {
                vec3 d = (wp - scorches[i].xyz) * units_per_metre;
                float d2 = dot(d, d);
                if (d2 <= scorches[i].w) {
                    dyn_dark *= 1.0 - (1.0 - d2 / scorches[i].w) / 8.0;
                }
            }
        }
    }
}

vec4 world_texel(vec2 uv, vec3 colour) {
    vec4 t = texture(pages, vec3(uv, layer));
    if (t.a < 0.01) {
        discard;
    }
    vec3 base = mix(colour, light_colour, lit);
    if (dyn_dark < 1.0) {
        base = max(base * dyn_dark, vec3(1.0 / 255.0));
    }
    vec3 c = clamp(t.rgb * min(base + dyn_add, vec3(1.0)) * 2.0, 0.0, 1.0);
    if (fog_near > 0.0) {
        c = mix(c, fog_color, clamp(2.0 * (depth_units - fog_near) / 4096.0, 0.0, 1.0));
    }
    return vec4(c, t.a);
}
