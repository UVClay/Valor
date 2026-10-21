// Semi-transparent faces, GPU blend mode 0 (formats.md: tpage bits 5..6). The texel's STP bit
// picks blending per texel on the PS1; here mode 0 keeps opaque texels opaque and the additive
// modes blend every texel.
shader_type spatial;
render_mode unshaded, cull_disabled, blend_mix, depth_draw_never;
#include "res://world.gdshaderinc"

void fragment() {
    vec4 c = world_texel(UV, COLOR.rgb);
    ALBEDO = c.rgb; ALPHA = c.a > 0.6 ? 1.0 : 0.5;
}
