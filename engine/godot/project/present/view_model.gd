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
## sways while walking (the seven animations' meanings from animation.md); the Use binding
## (valor_use, E by default) reloads.
##
## Hidden while zoomed (hud.md, Sniper scope: drawn only at projection distance 400); the zoom
## also narrows the main camera's view (the projection distance, movement.md).
##
## The muzzle flash (func_8006DD54, set up by func_8006DCAC on each draw): a 31 x 31 texel sprite
## (tpage 0xF, u 0xC0, v 0x80, CLUT 0x3E7E: VRAM (1008, 128), CLUT (992, 249)), semi-transparent,
## scaled by half, at one of eight rotations, colour 96 (128 - 32 x its counter), on the weapon's bone
## g_weapons[w]+0x34 at the offset +0x36, shown the frame after a shot (fire_gun sets the counter
## to 1 unless the player's flags 0x200040, read as the silenced pistol and the thrown weapons).

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
var flash: Control
var flash_tex: ImageTexture
var flash_angle := 0.0
var flash_ticks := 0
var flash_local := Vector3.ZERO  # in the weapon model's space
var base_fov := 0.0  # main.cam's field of view unzoomed
var zoomed := false
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
	# The flash, over the weapon, added to the picture (a semi-transparent prim): drawn in 2D at the
	# projection of its 3D place, as func_8006E058's camera-facing square projects.
	flash = Control.new()
	flash.set_anchors_preset(Control.PRESET_FULL_RECT)
	flash.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var fm := CanvasItemMaterial.new()
	fm.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
	flash.material = fm
	flash.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	layer.add_child(flash)
	flash_tex = ImageTexture.create_from_image(p.assets.region_image(0xF, 0x3E7E, 0xC0, 0x80, 31, 31))
	flash.draw.connect(_draw_flash)
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
	flash_ticks = maxi(flash_ticks - 1, 0)
	for e in p.events:
		if e.kind == "fire" and not (e.weapon in [2, 3, 7, 9, 10]):
			var row: PackedByteArray = p.assets.level_bin(0x80093640 + 0x44 * e.weapon, 0x44)
			var bone := row.decode_u16(0x34)
			var off := Vector3i(row.decode_s16(0x36), row.decode_s16(0x38), row.decode_s16(0x3A))
			flash_local = p.assets.bone_point(h, a, f, bone, off)
			# ((g_update_stamp & 7) << 9) + (counter << 7): one of eight turns, plus 1/32
			flash_angle = float(((randi() & 7) << 9) + (1 << 7)) * TAU / 4096.0
			flash_ticks = 1
	flash.visible = flash_ticks > 0 and mesh.visible
	flash.queue_redraw()


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
	elif (Input.is_action_pressed("valor_use") if InputMap.has_action("valor_use") else Input.is_physical_key_pressed(KEY_E)) and anim != RELOAD and anim != FIRE:
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


## The flash's square: half 2 x (31 >> 1) x 0.5 = 15 units round its point, facing the camera,
## turned by its angle; colour 96 (0x80 - 32 x the counter).
func _draw_flash() -> void:
	if not flash.visible:
		return
	var at := mesh.basis * flash_local
	if cam.is_position_behind(at):
		return
	var c := cam.unproject_position(at)
	var r := cam.unproject_position(at + Vector3(15.0 / 128.0, 0, 0)).distance_to(c)
	flash.draw_set_transform(c, flash_angle)
	flash.draw_texture_rect(flash_tex, Rect2(-r, -r, 2 * r, 2 * r), false, Color(0.75, 0.75, 0.75))
	flash.draw_set_transform(Vector2.ZERO)


func on_frame() -> void:
	var main_cam: Camera3D = p.main.cam
	viewport.size = p.main.get_viewport().get_visible_rect().size
	# The zoom: the projection distance (400 normal); the weapon is drawn only at 400. Unzoomed the
	# view's field is main.gd's (the options' FOV): read it then, and narrow from it while zoomed.
	var zoom := float(p.state.get("zoom", 400))
	mesh.visible = zoom == 400.0
	if zoom == 400.0:
		if zoomed:
			main_cam.fov = base_fov
			zoomed = false
		base_fov = main_cam.fov
	elif p.state.has("zoom"):
		zoomed = true
		main_cam.fov = rad_to_deg(2.0 * atan(tan(deg_to_rad(base_fov) / 2.0) * 400.0 / maxf(zoom, 1.0)))
	cam.keep_aspect = main_cam.keep_aspect
	cam.fov = base_fov
	flash.queue_redraw()
	if p.state.has("body_yaw"):
		var f := Engine.get_physics_interpolation_fraction()
		var body := Basis.from_euler(Vector3(lerp_angle(body_prev.x, body_cur.x, f), lerp_angle(body_prev.y, body_cur.y, f), 0.0))
		# against the view as the ticks left it (main.gd's), not the mouse's turn not yet sent: in
		# the walking stances the body is the view, and the gun stays put on screen
		var m = p.main
		var view := Basis.from_euler(Vector3(lerpf(m.prev_pitch, m.cur_pitch, f), lerp_angle(m.prev_yaw, m.cur_yaw, f), 0.0))
		mesh.basis = view.inverse() * body
