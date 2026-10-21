shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_opaque;
#include "res://world.gdshaderinc"

void fragment() {
    vec4 c = world_texel(UV, COLOR.rgb);
    ALBEDO = c.rgb;
}
