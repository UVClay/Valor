extends Node3D
## Particle effects drawn from the engine's own particles (ValorGame.get_engine_sprites, the
## accessor layer's sprites), by the game's draw rules (views.h, Sprite):
## - bullet impact sparks, smoke and debris (particles_spawn; drawn by 0x800732F8): a quad of
##   corners (+-w, +-h) scaled by the system's +0x8, turned by the particle's three angles (it
##   tumbles), a triangle when the particle's +0x3 flag is set;
## - explosion fireballs and smoke puffs (0x80070F24 through func_8006E058): a quad facing the
##   camera, turned in the view plane by the record's angle, its colour the size's whole part
##   (red as it shrinks), additive.
## Textures from the level's VRAM as each system's header names them (the sparks VRAM (1012, 224),
## the smoke (1000, 0), the fireballs (992, 32), debris one texel at (1023, 63)).
## Also (get_engine_effects): the hit puffs on soldiers and dogs (0x80072310: a camera-facing quad
## of half 15 x scale across, 12 x scale up, VRAM (1008, 0) 32 x 32 through CLUT 0x3EFF, additive)
## and the drops (0x800729C0: VRAM (992, 0) through CLUT 0x3EFE; their size not traced, half 6);
## the enemies' muzzle flashes (the player's sprite, 15 units, colour 96, at the muzzle a new enemy
## bullet left from: enemy_draw draws the flash the frame after a shot); the impact streaks and
## explosion bursts (streaks, in 2D: a triangle from the start point, its base across the streak
## an eighth of its length on screen, to the end point, Gouraud from the start colour to the end
## colour, B + F/4; the bursts' four-cornered streaks textured 31 x 31, B + F); and the explosion
## light and scorches on the world (world.gdshaderinc).

const POOL := 256
const ANGLE := TAU / 4096.0

var p
var quads := []
var quad_mesh: QuadMesh
var tri_mesh: ArrayMesh
var materials := {}  # texture key -> ShaderMaterial
var sprites := []
var streaks := []
var streak_layer: Control
var streak_tex := {}
var shots_seen := {}
var scorches := []  # [Vector4 (Godot metres, radius squared)], the newest last
const FLASH := {"tpage": 0xF, "clut": 0x3E7E, "u": 0xC0, "v": 0x80, "w": 31, "h": 31, "additive": true}


func setup(present) -> void:
	p = present
	p.main.add_child(self)
	quad_mesh = QuadMesh.new()
	quad_mesh.size = Vector2.ONE
	# the triangle variant: the quad's corners (-w, -h), (w, -h), (-w, h)
	tri_mesh = ArrayMesh.new()
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(-0.5, 0.5, 0), Vector3(0.5, 0.5, 0), Vector3(-0.5, -0.5, 0)])
	arrays[Mesh.ARRAY_TEX_UV] = PackedVector2Array([Vector2(0, 0), Vector2(1, 0), Vector2(0, 1)])
	tri_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var layer := CanvasLayer.new()
	layer.layer = 0
	add_child(layer)
	streak_layer = Control.new()
	streak_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	streak_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var mat := CanvasItemMaterial.new()
	mat.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
	streak_layer.material = mat
	layer.add_child(streak_layer)
	streak_layer.draw.connect(_draw_streaks)
	for i in POOL:
		var mi := MeshInstance3D.new()
		mi.visible = false
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(mi)
		quads.append(mi)


func material(s: Dictionary) -> ShaderMaterial:
	var key := "%x_%x_%d_%d_%d_%d_%s" % [s.tpage, s.clut, s.u, s.v, s.w, s.h, s.additive]
	if not materials.has(key):
		var m := ShaderMaterial.new()
		m.shader = load("res://present/sprite_%s.gdshader" % ("add" if s.additive else "mix"))
		m.set_shader_parameter("tex", ImageTexture.create_from_image(p.assets.region_image(int(s.tpage), int(s.clut), int(s.u), int(s.v), maxi(int(s.w), 1), maxi(int(s.h), 1))))
		materials[key] = m
	return materials[key]


func on_tick() -> void:
	if not p.game.is_engine_running():
		return
	sprites = p.game.get_engine_sprites()
	var fx: Dictionary = p.game.get_engine_effects()
	streaks = fx.get("streaks", [])
	if p.args.has("fx-log"):
		var np := 0
		var nd := 0
		for f in fx.get("puffs", []):
			if f.drop:
				nd += 1
			else:
				np += 1
		if np + nd + streaks.size() + fx.get("enemy_shots", []).size() > 0:
			print("Valor fx: tick %d streaks %d puffs %d drops %d enemy shots %d" % [int(p.state.get("tick", 0)), streaks.size(), np, nd, fx.get("enemy_shots", []).size()])
	for f in fx.get("puffs", []):
		if f.drop:
			# the drops: VRAM (992, 0), CLUT 0x3EFE, 32 x 32, opaque (size not traced)
			sprites.append({"pos": f.pos, "half": Vector2(6, 6) / 128.0, "color": f.color, "rot": Vector3i(f.angle, 0, 0),
				"billboard": true, "triangle": false, "tpage": 0xF, "clut": 0x3EFE, "u": 0x80, "v": 0, "w": 32, "h": 32, "additive": false})
		else:
			sprites.append({"pos": f.pos, "half": Vector2(15.0 * f.scale, 12.0 * f.scale) / 128.0, "color": f.color,
				"rot": Vector3i(f.angle, 0, 0), "billboard": true, "triangle": false,
				"tpage": 0xF, "clut": 0x3EFF, "u": 0xC0, "v": 0, "w": 32, "h": 32, "additive": true})
	var seen := {}
	for s in fx.get("enemy_shots", []):
		seen[s.handle] = true
		if not shots_seen.has(s.handle):
			var flash: Dictionary = FLASH.duplicate()
			flash.merge({"pos": s.from, "half": Vector2(15, 15) / 128.0, "color": Color(0.75, 0.75, 0.75),
				"rot": Vector3i(((randi() & 7) << 9) + 128, 0, 0), "billboard": true, "triangle": false})
			sprites.append(flash)
	shots_seen = seen
	light(fx.get("light", {}))
	streak_layer.queue_redraw()
	if p.args.has("fx-log") and not sprites.is_empty():
		for sp in sprites:
			if sp.billboard:
				print("Valor fx: tick %d, %d sprites, billboard %s" % [int(p.state.get("tick", 0)), sprites.size(), sp])
				break
	for i in POOL:
		var mi: MeshInstance3D = quads[i]
		if i >= sprites.size():
			mi.visible = false
			continue
		var s: Dictionary = sprites[i]
		mi.mesh = tri_mesh if s.triangle else quad_mesh
		mi.material_override = material(s)
		mi.set_instance_shader_parameter("tint", s.color)
		mi.visible = true
	place()


## Every frame: billboards face the camera as it is now.
func _process(_delta: float) -> void:
	place()


func place() -> void:
	var cam: Camera3D = p.main.cam
	for i in mini(sprites.size(), POOL):
		var s: Dictionary = sprites[i]
		var half: Vector2 = s.half
		var size := Basis.from_scale(Vector3(maxf(half.x * 2.0, 0.001), maxf(half.y * 2.0, 0.001), 1.0))
		var b: Basis
		if s.billboard:
			b = cam.global_transform.basis * Basis(Vector3.BACK, -float(s.rot.x) * ANGLE) * size
		else:
			# RotMatrixYXZ of the particle's angles, in Godot's axes (y and z flipped): C M C
			var r: Vector3i = s.rot
			var m := Basis.from_euler(Vector3(r.x * ANGLE, r.y * ANGLE, r.z * ANGLE), EULER_ORDER_YXZ)
			var c := Basis(Vector3(1, 0, 0), Vector3(0, -1, 0), Vector3(0, 0, -1))
			b = c * m * c * size
		quads[i].transform = Transform3D(b, s.pos)


## The explosion light and scorches into the world's shader (world.gdshaderinc), on main.gd's world.
func light(l: Dictionary) -> void:
	var on := not l.is_empty()
	if p.args.has("fx-log") and on:
		print("Valor light: ", l)
	# the scorch flag is set when an explosion is made and cleared by the next draw_level; the
	# engine doesn't draw, so it stays set: one scorch per explosion (its place)
	if on and l.scorch and (scorches.is_empty() or Vector3(scorches[-1].x, scorches[-1].y, scorches[-1].z) != l.pos):
		scorches.append(Vector4(l.pos.x, l.pos.y, l.pos.z, float(l.radius2)))
		if scorches.size() > 32:
			scorches.pop_front()
	var arr := []
	for i in 32:
		arr.append(scorches[i] if i < scorches.size() else Vector4.ZERO)
	for mat in p.main.materials.values():
		mat.set_shader_parameter("dyn_light_r2", float(l.radius2) if on else 0.0)
		if on:
			mat.set_shader_parameter("dyn_light_pos", l.pos)
			var c: Color = l.color
			mat.set_shader_parameter("dyn_light_rgb", Vector3(c.r, c.g, c.b))
		mat.set_shader_parameter("scorches", arr)
	var world = p.main.get("world")
	if world:
		world.set_instance_shader_parameter("dyn_lit", 1.0)


func streak_texture(s: Dictionary) -> ImageTexture:
	var key := "%x_%x_%d_%d_%d_%d" % [s.tpage, s.clut, s.u, s.v, s.w, s.h]
	if not streak_tex.has(key):
		streak_tex[key] = ImageTexture.create_from_image(p.assets.region_image(int(s.tpage), int(s.clut), int(s.u), int(s.v), maxi(int(s.w), 1), maxi(int(s.h), 1)))
	return streak_tex[key]


## The streaks on screen (0x8006F7BC, 0x8006FBF0): the perpendicular is worked out in the PS1's
## 512 x 240 pixels (x narrower than y), an eighth of the streak's length across.
func _draw_streaks() -> void:
	var cam: Camera3D = p.main.cam
	var size := streak_layer.get_viewport_rect().size
	var sy := size.y / 240.0
	var sx := sy * 0.625
	for s in streaks:
		if cam.is_position_behind(s.p0) or cam.is_position_behind(s.p1):
			continue
		var a := cam.unproject_position(s.p0)
		var b := cam.unproject_position(s.p1)
		var d := Vector2((b.x - a.x) / sx, (b.y - a.y) / sy)  # PS1 pixels
		var perp := Vector2(d.y / 16.0 * sx, -d.x / 16.0 * sy)
		# B + F/4 for the impacts' mode 4 (ABR 3), B + F for the bursts' mode 2 (ABR 1)
		var k := 0.25 if int(s.mode) == 4 else 1.0
		var tex := streak_texture(s)
		var c0: Color = s.c0 * k
		var c1: Color = s.c1 * k
		c0.a = 1.0
		c1.a = 1.0
		if s.quad:
			streak_layer.draw_polygon(PackedVector2Array([a + perp, b + perp, b - perp, a - perp]),
				PackedColorArray([c0, c1, c1, c0]), PackedVector2Array([Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)]), tex)
		else:
			streak_layer.draw_polygon(PackedVector2Array([a + perp, a - perp, b]),
				PackedColorArray([c0, c0, c1]), PackedVector2Array([Vector2(0, 0), Vector2(0, 1), Vector2(1, 0.5)]), tex)
