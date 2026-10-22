extends Node
## The first-person weapon (animation.md, The player: first-person weapon models): the model in
## hand, posed at the frame the core's instance shows (the engine's `weapon_anim`/`weapon_frame`),
## drawn over the world as the PS1 did (in front of everything, in its own pass with its own depth).
## Its vertices are relative to the eye (the root offset hangs the arms below it), so it sits in a
## camera of its own that shares the view's projection. The original draws it with the soldier's
## body angles, which trail the view in the aiming stances (movement.md): it is turned by the body's
## angles less the view's, interpolated between ticks as the view is.
##
## Without the engine (a walk through the level) nothing animates it: the level's first weapon is
## shown, the left mouse button plays the firing animation and E the reload, the rest idles or
## sways while walking (the seven animations' meanings from animation.md).
##
## Hidden while zoomed (hud.md, Sniper scope: drawn only at projection distance 400): stubbed as
## never, the zoom isn't in the core's state yet.

const IDLE := 5
const MOVING := 0
const FIRE := 1
const RAISE := 2
const RELOAD := 4

var p  # present.gd
var viewport: SubViewport
var cam: Camera3D
var mesh: MeshInstance3D
var materials := {}
var cache := {}  # [hash, anim, frame] -> ArrayMesh
var shown := []

# Local playback when the engine isn't running.
var local_hash := 0
var anim := IDLE
var frame := 0
var last_pos := Vector3.ZERO
var body_prev := Vector2.ZERO  # body pitch, Godot yaw (radians), last tick and this one
var body_cur := Vector2.ZERO


func setup(present) -> void:
	p = present
	viewport = SubViewport.new()
	viewport.own_world_3d = true
	viewport.transparent_bg = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(viewport)
	cam = Camera3D.new()
	cam.near = 0.02
	cam.far = 20.0
	viewport.add_child(cam)
	mesh = MeshInstance3D.new()
	viewport.add_child(mesh)
	var layer := CanvasLayer.new()
	layer.layer = 1
	add_child(layer)
	var rect := TextureRect.new()
	rect.texture = viewport.get_texture()
	rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(rect)
	if not p.state.has("weapon_model"):
		for w in [12, 1, 8, 0, 5, 4, 6, 11, 9, 2, 3, 10]:
			var h: int = p.weapon_model(w)
			if p.assets.get_anim_frames(h, IDLE) > 0:
				local_hash = h
				break
	on_frame()


func material_for(surface: String) -> ShaderMaterial:
	if not materials.has(surface):
		var mat := ShaderMaterial.new()
		mat.shader = load("res://world_%s.gdshader" % surface)
		mat.set_shader_parameter("fog_near", 0.0)
		mat.set_shader_parameter("units_per_metre", 128.0)
		materials[surface] = mat
	return materials[surface]


func on_tick() -> void:
	var h: int
	var a: int
	var f: int
	if p.state.has("body_yaw"):
		const ANGLE := TAU / 4096.0
		body_prev = body_cur
		body_cur = Vector2(float(p.state.body_pitch) * ANGLE, -float(p.state.body_yaw) * ANGLE)
	if p.state.has("weapon_anim"):
		h = p.weapon_hash
		a = int(p.state.weapon_anim)
		f = int(p.state.weapon_frame)
	else:
		h = local_hash
		local_step()
		a = anim
		f = frame
	show_pose(h, a, f)


func local_step() -> void:
	var pos := Vector3(p.state.get("x", 0.0), 0.0, p.state.get("z", 0.0))
	var moving := pos.distance_to(last_pos) > 1.0
	last_pos = pos
	var n: int = p.assets.get_anim_frames(local_hash, anim)
	frame += 1
	var fire := Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT) and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED
	if fire and anim != FIRE and anim != RELOAD:
		anim = FIRE
		frame = 0
	elif Input.is_physical_key_pressed(KEY_E) and anim != RELOAD and anim != FIRE:
		anim = RELOAD
		frame = 0
	elif frame >= n:
		frame = 0
		if anim != IDLE and anim != MOVING:
			anim = IDLE
	if anim == IDLE and moving:
		anim = MOVING
		frame = 0
	elif anim == MOVING and not moving:
		anim = IDLE
		frame = 0


func show_pose(h: int, a: int, f: int) -> void:
	var key := [h, a, f]
	if key == shown:
		return
	shown = key
	if h == 0 or a < 0:
		mesh.mesh = null
		return
	var m: ArrayMesh = cache.get(key)
	if m == null:
		m = p.assets.build_model_mesh(h, a, f)
		cache[key] = m
		var pages: Texture2DArray = p.assets.get_texture_pages()
		for mat in materials.values():
			mat.set_shader_parameter("pages", pages)
	mesh.mesh = m
	for i in m.get_surface_count():
		var mat := material_for(m.surface_get_name(i))
		mat.set_shader_parameter("pages", p.assets.get_texture_pages())
		mesh.set_surface_override_material(i, mat)


func on_frame() -> void:
	var main_cam: Camera3D = p.main.cam
	viewport.size = p.main.get_viewport().get_visible_rect().size
	cam.keep_aspect = main_cam.keep_aspect
	cam.fov = main_cam.fov
	if p.state.has("body_yaw"):
		var f := Engine.get_physics_interpolation_fraction()
		var body := Basis.from_euler(Vector3(lerp_angle(body_prev.x, body_cur.x, f), lerp_angle(body_prev.y, body_cur.y, f), 0.0))
		# against the view as the ticks left it (main.gd's), not the mouse's turn not yet sent: in
		# the walking stances the body is the view, and the gun stays put on screen
		var m = p.main
		var view := Basis.from_euler(Vector3(lerpf(m.prev_pitch, m.cur_pitch, f), lerp_angle(m.prev_yaw, m.cur_yaw, f), 0.0))
		mesh.basis = view.inverse() * body
