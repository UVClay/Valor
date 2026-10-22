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

const POOL := 256
const ANGLE := TAU / 4096.0

var p
var quads := []
var quad_mesh: QuadMesh
var tri_mesh: ArrayMesh
var materials := {}  # texture key -> ShaderMaterial
var sprites := []


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
