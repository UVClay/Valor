extends Node3D
## Particle effects drawn from the engine's own particles (ValorGame.get_engine_sprites, the
## accessor layer's sprites): bullet impact sparks and smoke (particles_spawn), and the fireballs
## and smoke puffs of explosions, each a camera-facing quad with its system's texture from the level's
## VRAM (the sparks VRAM (1012, 224), the smoke (1000, 0), the fireballs (992, 32)), additive where
## the system is semi-transparent. Their sizes follow the particle's size field and the system's
## scale (approximate: the draw functions 0x800732F8 and 0x80070F24 weren't followed in detail).

const POOL := 256

var p
var quads := []
var materials := {}  # texture key -> StandardMaterial3D


func setup(present) -> void:
	p = present
	p.main.add_child(self)
	var mesh := QuadMesh.new()
	mesh.size = Vector2.ONE
	for i in POOL:
		var mi := MeshInstance3D.new()
		mi.mesh = mesh
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
	var sprites: Array = p.game.get_engine_sprites()
	if p.args.has("fx-log") and not sprites.is_empty():
		print("Valor fx: tick %d, %d sprites, first %s" % [int(p.state.get("tick", 0)), sprites.size(), sprites[0]])
	for i in POOL:
		var mi: MeshInstance3D = quads[i]
		if i >= sprites.size():
			mi.visible = false
			continue
		var s: Dictionary = sprites[i]
		mi.position = s.pos
		mi.scale = Vector3.ONE * maxf(float(s.size), 0.01)
		var m := material(s)
		mi.material_override = m
		mi.set_instance_shader_parameter("tint", s.color)
		mi.visible = true
