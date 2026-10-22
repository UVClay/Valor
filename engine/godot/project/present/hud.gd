extends Control
## The HUD (docs/re/hud.md), drawn natively from the core's state at any resolution. Positions are
## the game's 512 x 240 frame (x drawn at 5/8 of y's scale on a 4:3 screen, so the game's 2:1
## "circles" are its wide ellipses); on a wider screen the compass keeps to the left edge and the
## ammo to the right one, the messages and the reticle stay in the 4:3 middle.
##
## From the state: the health ring, the compass (the view yaw), the clip and reserve with the
## slot's ammo picture (hidden for the papers), the aim reticle (aim mode; the bazooka's own
## shape), the pickup messages (from health and ammo changes), the level-start and death fades.
## Tab (or --show-objectives) shows the objectives (the pause menu's list: a box, ticked when done). The sniper scope
## (aim mode with the scope, zoomed): the view outside a circle blacked out (inference: the scope
## model, the weapon block's +0x130 object, wasn't read; hud.md).
## Stubbed until the core reports them: the hit wedges (the hit's direction), level messages
## (show_text), the crosshair's movement (drawn at its rest point 256, 131).
## --crosshair=SIZE adds an always-on crosshair on the aim line, hidden in aim mode.

const FONT_TPAGE := 0xF
const FONT_CLUT := 0x3EBF  # CLUT (1008, 250)
const HUD_CLUT := 0x3E3E  # the compass
const AMMO_ICONS := [[0x80, 0xA0, 0x3DFE], [0xA0, 0xA0, 0x3DFF], [0xC0, 0xA0, 0x3FFF], [0xA0, 0xC0, 0x3E7F]]
# The ammo names (player.md, Weapon table), by weapon.
const AMMO_NAMES := {0: "PISTOL ROUNDS", 1: "SMG ROUNDS", 2: "BAZOOKA SHELL", 3: "GRENADES",
	4: "SHOTGUN SHELLS", 5: "RIFLE ROUNDS", 6: "SMG ROUNDS", 8: "PISTOL ROUNDS", 9: "PISTOL ROUNDS",
	10: "STIELHANDGRANATE STICK GRENADES", 11: "SMG ROUNDS", 12: "RIFLE ROUNDS"}

var p
var font_tex: ImageTexture
var glyph_u := PackedByteArray()
var glyph_v := PackedByteArray()
var compass_tex: ImageTexture
var icon_tex := []
var pack_names := []
var crosshair := 4
var texts := []  # {text, x, y, col: Array[3] 0..255, semi, life, hold, fade_in, step: Array[3]}
var add_layer: Control  # additive texts
var dead_since := -1


func setup(present) -> void:
	p = present
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	font_tex = ImageTexture.create_from_image(p.assets.page_image(FONT_TPAGE, FONT_CLUT))
	# texts_draw's glyph tables, indexed by the character (0x80099EB3 + c: u, 0x80099EEF + c: v)
	glyph_u = p.assets.level_bin(0x80099EB3, 128)
	glyph_v = p.assets.level_bin(0x80099EEF, 128)
	compass_tex = ImageTexture.create_from_image(p.assets.page_image(FONT_TPAGE, HUD_CLUT))
	for ic in AMMO_ICONS:
		icon_tex.append(ImageTexture.create_from_image(p.assets.region_image(FONT_TPAGE, ic[2], ic[0], ic[1], 32, 20)))
	for k in 3:
		var ptr: int = p.assets.level_bin(0x8009E864 + 4 * k, 4).decode_u32(0)
		pack_names.append(p.assets.level_string(ptr))
	crosshair = int(p.args.get("crosshair", "4"))
	add_layer = Control.new()
	add_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var mat := CanvasItemMaterial.new()
	mat.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
	add_layer.material = mat
	add_child(add_layer)
	add_layer.draw.connect(_draw_additive)
	if p.args.has("test-message"):
		# as show_text does it (hud.md, Messages): x 20, the first line 132, grey, additive, 100 ticks
		text_add(String(p.args["test-message"]), 20, 132, [128, 128, 128], true, 100)


# ---------------------------------------------------------------- the 512 x 240 frame

func scale_y() -> float:
	return get_viewport_rect().size.y / 240.0


func scale_x() -> float:
	return scale_y() * 0.625


func at_left(x: float, y: float) -> Vector2:
	return Vector2(x * scale_x(), y * scale_y())


func at_right(x: float, y: float) -> Vector2:
	return Vector2(get_viewport_rect().size.x - (512.0 - x) * scale_x(), y * scale_y())


func at_middle(x: float, y: float) -> Vector2:
	return Vector2(get_viewport_rect().size.x / 2.0 + (x - 256.0) * scale_x(), y * scale_y())


# ---------------------------------------------------------------- the text list

## text_add / text_add_fade (hud.md, Messages): a message drawn for `life` ticks; with `hold` > 0 it
## fades in by `step` a tick for `fade_in` ticks; id 2 (pickups) replaces the previous one.
func text_add(text: String, x: int, y: int, col: Array, semi: bool, life: int, hold := -2, fade_in := 0, step := [0, 0, 0], id := 0) -> void:
	if id != 0:
		texts = texts.filter(func(t): return t.id != id)
	if texts.size() >= 48:
		return
	texts.append({"text": text, "x": x, "y": y, "col": col.duplicate(), "semi": semi, "life": life,
		"hold": hold, "fade_in": fade_in, "step": step, "id": id})


func on_tick() -> void:
	for e in p.events:
		match e.kind:
			"health":
				# health_pickup_touch: at (10, 70), from 0 up by (10, 8, 4) for 11 ticks, 40 ticks
				text_add(pack_names[e.pack], 10, 70, [0, 0, 0], true, 40, 40, 11, [10, 8, 4], 2)
			"ammo":
				if AMMO_NAMES.has(e.weapon):
					text_add(AMMO_NAMES[e.weapon], 10, 70, [0, 0, 0], true, 40, 40, 11, [10, 8, 4], 2)
	# texts_update: age, fade in (hold > 0) or out (hold 0)
	var keep := []
	for t in texts:
		if t.hold >= 0:
			if t.hold > 0:
				t.hold -= 1
				if t.fade_in > 0:
					for i in 3:
						t.col[i] = mini(t.col[i] + t.step[i], 255)
					t.fade_in -= 1
			else:
				for i in 3:
					t.col[i] = maxi(t.col[i] - t.step[i], 0)
		if t.life > 0:
			t.life -= 1
		if t.life != 0:
			keep.append(t)
	texts = keep
	var dead: bool = p.state.has("health") and float(p.state.health) <= 0.0
	if dead and dead_since < 0:
		dead_since = int(p.state.get("tick", 0))
	elif not dead:
		dead_since = -1
	add_layer.queue_redraw()


## A string in the game's font: 10 x 7 glyphs every 12 pixels, modulated by the colour (128 = as
## stored), from (x, y) in the 512 x 240 frame.
func draw_game_text(ci: CanvasItem, text: String, origin: Vector2, col: Array) -> void:
	var sx := scale_x()
	var sy := scale_y()
	var mod := Color(col[0] / 128.0, col[1] / 128.0, col[2] / 128.0)
	var x := 0.0
	for ch in text.to_upper():
		var c := ch.unicode_at(0)
		if c != 32 and c < 128 and (glyph_u[c] != 0 or glyph_v[c] != 0):
			ci.draw_texture_rect_region(font_tex, Rect2(origin + Vector2(x * sx, 0), Vector2(10 * sx, 7 * sy)),
				Rect2(glyph_u[c] + 0.02, glyph_v[c] + 0.02, 9.96, 6.96), mod)  # inset: no bleed from the next glyph
		x += 12


func _draw_additive() -> void:
	for t in texts:
		if t.semi:
			draw_game_text(add_layer, t.text, at_middle(t.x, t.y), t.col)


# ---------------------------------------------------------------- drawing

func _draw() -> void:
	var size := get_viewport_rect().size
	var s: Dictionary = p.state
	var health: float = float(s.get("health", 128.0))
	draw_health_ring(health)
	draw_compass()
	var w := int(s.get("weapon", -1))
	if w >= 0 and w != 7:
		# ammo_hud: the clip right-aligned in 2 at (380, 18), the reserve in 3 at (453, 18), grey
		draw_game_text(self, "%2d" % int(s.clip), at_right(380, 18), [100, 100, 100])
		draw_game_text(self, "%3d" % int(s.reserve), at_right(453, 18), [100, 100, 100])
		var slot := int(s.get("slot", 0))
		var ic: ImageTexture = icon_tex[slot if slot >= 0 and slot < 4 else 0]
		var a := at_right(415, 15)
		draw_texture_rect(ic, Rect2(a, at_right(447, 35) - a), false)
	for t in texts:
		if not t.semi:
			draw_game_text(self, t.text, at_middle(t.x, t.y), t.col)
	var aiming: bool = s.get("aiming", false)
	var scoped: bool = s.get("scoped", false) and int(s.get("zoom", 400)) > 400
	if scoped:
		draw_scope()
	elif aiming:
		draw_reticle(w)
	elif crosshair > 0:
		var c := at_middle(256, 131)
		var k := crosshair * scale_y() * 0.5
		var col := Color(1, 1, 1, 0.7)
		draw_line(c - Vector2(k * 2, 0), c - Vector2(k * 0.6, 0), col, maxf(1.0, scale_y() * 0.4))
		draw_line(c + Vector2(k * 0.6, 0), c + Vector2(k * 2, 0), col, maxf(1.0, scale_y() * 0.4))
		draw_line(c - Vector2(0, k * 2), c - Vector2(0, k * 0.6), col, maxf(1.0, scale_y() * 0.4))
		draw_line(c + Vector2(0, k * 0.6), c + Vector2(0, k * 2), col, maxf(1.0, scale_y() * 0.4))
	if Input.is_physical_key_pressed(KEY_TAB) or p.args.has("show-objectives"):
		draw_objectives(s.get("objectives", []))
	# Fades (hud.md): the engine's fade entities when it reports them (state "fade": the black's
	# opacity), else the cuts as hud.md has them: the level start black for 15 ticks, death 65 ticks
	# then black.
	if s.has("fade"):
		if float(s.fade) > 0.0:
			draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, float(s.fade)))
	else:
		var tick := int(s.get("tick", 99))
		if tick < 15 or (dead_since >= 0 and tick - dead_since >= 65):
			draw_rect(Rect2(Vector2.ZERO, size), Color.BLACK)


## The health ring (hud.md, Health ring): 32 segments of 11.25 degrees between radii 14 and 20 (x
## doubled) round (60, 44); one lit per 4 health, the dark ones from the top clockwise.
func draw_health_ring(health: float) -> void:
	var lit := clampi((int(health) >> 2), 0, 32)
	for i in 32:
		var col: Color
		if i <= 31 - lit:
			col = Color8(1, 1, 1)
		elif i <= 16:
			col = Color8(5 * i, 64 + i, 0)
		else:
			col = Color8(80 - (i - 16), 80 - 5 * (i - 16), 0)
		var a0 := i * 0x80 * TAU / 4096.0
		var a1 := (i + 1) * 0x80 * TAU / 4096.0
		var pts := PackedVector2Array()
		for q in [[20, a0], [20, a1], [14, a1], [14, a0]]:
			pts.append(at_left(60 + 2 * q[0] * sin(q[1]), 44 - q[0] * cos(q[1])))
		draw_colored_polygon(pts, col)


## The compass (hud.md, Compass): the rose (u 0x80..0xBF, v 0x60..0x9F) turned with the view, its
## top edge at 90 degrees - yaw clockwise from up; corners from (23, +-23) at 320 projected with
## 400, round (59, 44).
func draw_compass() -> void:
	var theta: float = deg_to_rad(90.0) - p.view_yaw() * TAU / 4096.0
	var up := Vector2(sin(theta), -cos(theta))
	var right := Vector2(cos(theta), sin(theta))
	# 23 at 320 projected with 400 is 28.75 in x; the same in y is 14.4 (x is doubled: the rose
	# fits inside the ring's inner radius, 14)
	var half := 23.0 * 400.0 / 320.0 / 2.0
	var corners := [-right + up, right + up, right - up, -right - up]  # texture TL, TR, BR, BL
	var uvs := [Vector2(0x80, 0x60), Vector2(0xC0, 0x60), Vector2(0xC0, 0xA0), Vector2(0x80, 0xA0)]
	var pts := PackedVector2Array()
	var uv := PackedVector2Array()
	for k in 4:
		var d: Vector2 = corners[k] * half
		pts.append(at_left(59 + 2 * d.x, 44 + d.y))
		uv.append(uvs[k] / 256.0)
	draw_colored_polygon(pts, Color.WHITE, uv, compass_tex)


## The aim reticle (hud.md, Aim reticle): four red lines round the crosshair, the bazooka's shape
## for weapon 2; none for grenades and the papers.
func draw_reticle(w: int) -> void:
	if w == 3 or w == 10 or w == 7:
		return
	var col := Color8(128, 32, 32)
	var width := maxf(1.0, scale_y() * 0.5)
	var c := Vector2(256, 131)
	var lines := []
	if w == 2:
		lines = [[0, -8, 0, 6], [-24, -18, 24, -18], [-32, -12, -8, 16], [32, -12, 8, 16]]
	else:
		lines = [[0, -12, 0, -5], [0, 5, 0, 12], [-18, 0, -8, 0], [8, 0, 18, 0]]
	for l in lines:
		draw_line(at_middle(c.x + l[0], c.y + l[1]), at_middle(c.x + l[2], c.y + l[3]), col, width)


## The scope: black outside a circle round the view's centre (the scoped shot goes to the view's
## centre, player.md), with thin cross-hairs.
func draw_scope() -> void:
	var size := get_viewport_rect().size
	var c := size / 2.0
	var r := size.y * 0.46
	var pts := PackedVector2Array()
	for i in 65:
		var a := TAU * i / 64.0
		pts.append(c + Vector2(cos(a), sin(a)) * r)
	# the four corners round the circle, as one polygon with a hole: two halves
	for half in 2:
		var poly := PackedVector2Array()
		var x0 := 0.0 if half == 0 else size.x
		poly.append(Vector2(c.x, 0))
		poly.append(Vector2(x0, 0))
		poly.append(Vector2(x0, size.y))
		poly.append(Vector2(c.x, size.y))
		for i in 33:
			var a := PI / 2.0 + (PI * i / 32.0 if half == 0 else -PI * i / 32.0)
			poly.append(c + Vector2(cos(a), sin(a)) * r)
		draw_colored_polygon(poly, Color.BLACK)
	var w := maxf(1.0, scale_y() * 0.3)
	draw_line(Vector2(c.x - r, c.y), Vector2(c.x + r, c.y), Color.BLACK, w)
	draw_line(Vector2(c.x, c.y - r), Vector2(c.x, c.y + r), Color.BLACK, w)


## The objectives as the pause menu lists them: a box ("$") or a ticked box ("#"), then the text.
func draw_objectives(objs: Array) -> void:
	var y := 60
	for o in objs:
		draw_game_text(self, ("# " if o.done else "$ ") + String(o.text), at_middle(80, y), [128, 128, 128])
		y += 14
