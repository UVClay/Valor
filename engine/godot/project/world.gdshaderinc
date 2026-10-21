// The PS1 world look: texture x vertex colour x 2 (128 is 1.0), a transparent texel (0x0000)
// discarded, vertex fog toward the level's fog colour from fog_near on (docs/re/formats.md, TSP).
// The colour goes out as the PS1 computed it: with the Compatibility renderer this matched the
// original's brightness side by side (a linear conversion made the level far darker).
uniform sampler2DArray pages : filter_nearest, repeat_disable;
uniform float fog_near = 0.0;  // world units, 0: no fog
uniform vec3 fog_color = vec3(0.0);
uniform float units_per_metre = 128.0;
uniform vec3 light_colours[40];  // the animated lights this tick (formats.md, Animated lights)

varying flat float layer;
varying vec3 light_colour;
varying float lit;
varying float depth_units;

void vertex() {
    layer = UV2.x;
    int light = int(UV2.y + 0.5) - 1;
    lit = light >= 0 ? 1.0 : 0.0;
    light_colour = light >= 0 ? light_colours[light] : vec3(0.0);
    depth_units = -(MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).z * units_per_metre;
}

vec4 world_texel(vec2 uv, vec3 colour) {
    vec4 t = texture(pages, vec3(uv, layer));
    if (t.a < 0.01) {
        discard;
    }
    vec3 c = clamp(t.rgb * mix(colour, light_colour, lit) * 2.0, 0.0, 1.0);
    if (fog_near > 0.0) {
        c = mix(c, fog_color, clamp(2.0 * (depth_units - fog_near) / 4096.0, 0.0, 1.0));
    }
    return vec4(c, t.a);
}
