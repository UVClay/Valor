extends Control
## The sky (docs/re/formats.md, Sky), drawn behind the world as the original's render_pass draws
## it before the level: a gradient of eight 30-line strips over the whole screen when the level has a
## sky colour (BSD +0x58C..+0x58E), stars when it has a star height (+0x58F), and the moon with the
## stars when it has a moon position (+0x594/+0x596). The gradient follows the look pitch; stars and
## moon are directions turned by the view only.

const STARS := 255
const STAR_RADIUS := 0x6FFF

var p
var sky_rgb := PackedByteArray()
var stars := []  # [Vector3 direction (Godot axes), Color]
var twinkle := PackedByteArray()  # per star: 1 dimmed this tick (rand() & 3 != 0)
var moon := false
var moon_dir := Vector3.ZERO
var moon_size := 0.0  # world units at the moon's distance
var moon_tex: ImageTexture
var last_tick := -1


func setup(present) -> void:
	p = present
	var info: Dictionary = p.assets.get_level_info()
	sky_rgb = info.sky_rgb
	var has_sky: bool = sky_rgb[0] != 0 or sky_rgb[1] != 0 or sky_rgb[2] != 0
	var height: int = info.star_height
	if not has_sky and height == 0:
		return
	# The world clears to the fog colour; with a sky the gradient is the background instead.
	for c in p.main.get_children():
		if c is WorldEnvironment and has_sky:
			c.environment.background_mode = Environment.BG_CANVAS
			c.environment.background_canvas_max_layer = -1
	var layer := CanvasLayer.new()
	layer.layer = -1
	p.add_child(layer)
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(self)
	if not has_sky:
		sky_rgb = PackedByteArray()
	if height != 0:
		# stars_init (0x80088744): on a sphere of radius 0x6FFF, y = -((+0x58F << 8) + rand 0..0x2FFF)
		# (up), at a random angle round; colours from the 8-entry table at 0x8009EC6C (LEVEL.BIN),
		# stepped through from a random start (inference: one entry per star).
		var table: PackedByteArray = p.assets.level_bin(0x8009EC6C, 24)
		var rng := RandomNumberGenerator.new()
		var c := rng.randi() & 7
		for i in STARS:
			var y := -((height << 8) + rng.randi_range(0, 0x2FFF))
			var r := sqrt(float(STAR_RADIUS - (-y)) * float(-y + STAR_RADIUS))
			var a := rng.randf() * TAU
			var dir := Vector3(r * cos(a), -y, -(r * sin(a))).normalized()
			stars.append([dir, Color8(table[c * 3], table[c * 3 + 1], table[c * 3 + 2])])
			c = (c + 1) & 7
		twinkle.resize(STARS)
		if info.moon:
			# sky_init / 0x80088170: the moon at (0, y, z) seen from the eye, a 32 x 32 texel quad
			# (tpage 0xF, CLUT 0x3FBF, u 0xC0.., v 0x60..) from that point right and down by
			# (z << 5) / 200 units (x by two thirds of it, the PS1's wide pixels).
			var my: float = info.moon_y
			var mz: float = info.moon_z
			moon = true
			moon_dir = Vector3(0, -my, -mz)
			moon_size = float((int(mz) << 5) / 200)
			moon_tex = ImageTexture.create_from_image(p.assets.region_image(0xF, 0x3FBF, 0xC0, 0x60, 32, 32))


## The gradient's strips (func_80088A5C): the split k from the look pitch, black to half the colour
## over the k + 1 lowest strips, then from half to the full colour over the rest; each strip is
## Gouraud-shaded from its bottom colour to its top one.
func strips() -> Array:
	var full := [int(sky_rgb[0]), int(sky_rgb[1]), int(sky_rgb[2])]
	var half := [full[0] >> 1, full[1] >> 1, full[2] >> 1]
	var v := clampi(int(floor(p.view_pitch())) >> 2, -128, 127)
	var t := (v + 1) >> 5
	var k := (4 if t >= 0 else 3) - t
	var out := []  # [bottom colour, top colour] from the bottom strip up
	if k != 0:
		var step := []
		for i in 3:
			var h: int = half[i]
			step.append([0, h >> 1, (h * 0x555) >> 12, h >> 2, (h * 0x333) >> 12, (h * 0x2AB) >> 12, (h * 0x249) >> 12, h >> 3][k])
		var c := [0, 0, 0]
		for j in k + 1:
			var n := [c[0] + step[0], c[1] + step[1], c[2] + step[2]]
			out.append([c, n])
			c = n
	var step2 := []
	for i in 3:
		var d: int = full[i] - half[i]
		step2.append([d >> 3, (d * 0x249) >> 12, (d * 0x2AB) >> 12, (d * 0x333) >> 12, d >> 2, (d * 0x555) >> 12, d >> 1, 0][k])
	var c2: Array = half.duplicate()
	while out.size() < 8:
		var n2 := [c2[0] + step2[0], c2[1] + step2[1], c2[2] + step2[2]]
		out.append([c2, n2])
		c2 = n2
	return out


func _draw() -> void:
	var size := get_viewport_rect().size
	var line := size.y / 240.0
	if sky_rgb.size() == 3:
		var s := strips()
		for j in 8:
			var bottom := size.y - j * 30 * line
			var top := bottom - 30 * line
			var cb: Array = s[j][0]
			var ct: Array = s[j][1]
			var col_b := Color8(clampi(cb[0], 0, 255), clampi(cb[1], 0, 255), clampi(cb[2], 0, 255))
			var col_t := Color8(clampi(ct[0], 0, 255), clampi(ct[1], 0, 255), clampi(ct[2], 0, 255))
			draw_polygon(PackedVector2Array([Vector2(0, top), Vector2(size.x, top), Vector2(size.x, bottom), Vector2(0, bottom)]),
				PackedColorArray([col_t, col_t, col_b, col_b]))
	if stars.is_empty():
		return
	var cam: Camera3D = p.main.cam
	var eye := cam.global_position
	if p.tick != last_tick:
		# the twinkle (func_80088368): each tick a star is drawn dimmer (components above 32 less
		# 32) three times in four
		last_tick = p.tick
		for i in STARS:
			twinkle[i] = 1 if randi() & 3 != 0 else 0
	var px := Vector2(maxf(1.0, line * 0.625), maxf(1.0, line))
	for i in STARS:
		var dir: Vector3 = stars[i][0]
		var at := eye + dir * 10.0
		if cam.is_position_behind(at):
			continue
		var c: Color = stars[i][1]
		if twinkle[i]:
			c = Color8(dim(c.r8), dim(c.g8), dim(c.b8))
		draw_rect(Rect2(cam.unproject_position(at), px), c)
	if moon:
		var dist := moon_dir.length()
		var d := moon_dir / dist
		var p0 := eye + d * 10.0
		if not cam.is_position_behind(p0):
			var k := 10.0 / dist
			var a := cam.unproject_position(p0)
			var down := -cam.global_transform.basis.y
			# shown square (inference: the two thirds undo the PS1 view matrix's wider x scale)
			var c2 := cam.unproject_position(p0 + down * moon_size * k)
			var h := c2.y - a.y
			draw_texture_rect(moon_tex, Rect2(a, Vector2(h, h)), false)


static func dim(v: int) -> int:
	return v - 32 if v > 32 else v
