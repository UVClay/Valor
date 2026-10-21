extends Node3D
## Loads a level through the core (ValorGame), draws it, and walks it: the core steps the player at
## 30 Hz in _physics_process; the camera is interpolated between ticks at the display rate, and
## the mouse turns the view at once (its yaw reaches the core on the next tick).
##
## User arguments (after `--`): --data=DIR (extracted/moh/DATA), --level=M:L (1:1),
## --walk=TICKS (walk forward on its own), --place=X,Y,Z,YAW,PITCH (game units and angles), --screenshot=FILE.png --frames=N (save the frame N and quit).

const RUN := 0x22DD00  # 34.863 units per tick: the keyboard's (and a full stick's) speed
# The pad's buttons (a fresh card's layout, controls.cpp): keys press them, and the core reads
# them as the game does: the arrows are the d-pad (turning ramps up to 60 units per tick).
const PAD_TRIANGLE := 0x10  # jump
const PAD_L2 := 0x1  # crouch
const PAD_SQUARE := 0x80  # Use, reload
const PAD_R2 := 0x2  # aim
const PAD_L3 := 0x200  # centre the view
const PAD_LEFT := 0x8000
const PAD_RIGHT := 0x2000
const ANGLE := TAU / 4096.0

@export var mouse_units_per_pixel := 3.0  # game angle units (4096 per turn) per mouse pixel

var game: ValorGame
var cam: Camera3D
var prev_eye := Vector3.ZERO
var cur_eye := Vector3.ZERO
var prev_yaw := 0.0
var cur_yaw := 0.0
var prev_pitch := 0.0
var cur_pitch := 0.0
var mouse_turn := 0.0  # game units not yet given to the core
var mouse_pitch := 0.0
var tapped := 0  # buttons pressed and released since the last tick: held for one tick
var auto_walk := 0
var screenshot := ""
var screenshot_frame := 0
var frames := 0
var hud: Label
var materials := {}  # by surface name, shared by every mesh
var skinned := {}  # actor index -> MeshInstance3D
var level_info := {}


func _ready() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		var kv := a.trim_prefix("--").split("=", true, 1)
		args[kv[0]] = kv[1] if kv.size() > 1 else ""
	var data: String = args.get("data", ProjectSettings.globalize_path("res://").path_join("../../../extracted/moh/DATA").simplify_path())
	var lv: PackedStringArray = String(args.get("level", "1:1")).split(":")
	auto_walk = int(args.get("walk", "0"))
	screenshot = args.get("screenshot", "")
	screenshot_frame = int(args.get("frames", "60"))

	game = ValorGame.new()
	if not game.load_level(data, int(lv[0]), int(lv[1])):
		push_error("Valor: " + game.get_error())
		get_tree().quit(1)
		return
	var info := game.get_level_info()
	print("Valor: level %d:%d, %d chunks, fog from %d, view %d" % [info.mission, info.level, info.chunks, info.fog_near, info.view_distance])

	if args.has("place"):
		var p: PackedFloat64Array = String(args.place).split_floats(",")
		game.place(p[0], p[1], p[2], p[3], p[4] if p.size() > 4 else 0.0)

	level_info = info
	var world := MeshInstance3D.new()
	world.mesh = game.build_world_mesh()
	apply_materials(world)
	add_child(world)
	# The models the records name, posed by the core; skinned ones are rebuilt every tick.
	for i in game.spawn_actors():
		var mi := MeshInstance3D.new()
		mi.transform = game.get_actor_transform(i)
		mi.mesh = game.build_actor_mesh(i)
		apply_materials(mi)
		add_child(mi)
		if game.is_actor_skinned(i):
			skinned[i] = mi
	var pages := game.get_texture_pages()
	for mat in materials.values():
		mat.set_shader_parameter("pages", pages)
	print("Valor: %d actors, %d skinned" % [game.get_actor_count(), skinned.size()])

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = info.fog_color if info.fog_near > 0 else info.sky_color
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	cam = Camera3D.new()
	# The original's view: projection distance 400 for 240 lines scaled by 5/8 (movement.md, The
	# camera) is a vertical field of view of 2 atan(120 / 250) = 51.4 degrees.
	cam.keep_aspect = Camera3D.KEEP_HEIGHT
	cam.fov = float(args.get("fov", "51.4"))
	if args.has("hfov"):  # e.g. 65.4: the original's 512-pixel width at projection 400
		cam.keep_aspect = Camera3D.KEEP_WIDTH
		cam.fov = float(args.hfov)
	cam.near = 0.05
	cam.far = float(info.view_distance) / ValorGame.kUnitsPerMetre
	add_child(cam)
	cur_eye = game.get_eye_position()
	prev_eye = cur_eye
	cur_yaw = game.get_yaw()
	prev_yaw = cur_yaw
	cur_pitch = game.get_pitch()
	prev_pitch = cur_pitch

	hud = Label.new()
	hud.position = Vector2(8, 8)
	add_child(hud)
	if DisplayServer.get_name() != "headless" and screenshot.is_empty():
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func material_for(surface: String) -> ShaderMaterial:
	if not materials.has(surface):
		var mat := ShaderMaterial.new()
		mat.shader = load("res://world_%s.gdshader" % surface)
		mat.set_shader_parameter("fog_near", float(level_info.fog_near))
		var fog: Color = level_info.fog_color
		mat.set_shader_parameter("fog_color", Vector3(fog.r, fog.g, fog.b))
		mat.set_shader_parameter("units_per_metre", ValorGame.kUnitsPerMetre)
		materials[surface] = mat
	return materials[surface]


func apply_materials(mi: MeshInstance3D) -> void:
	for i in mi.mesh.get_surface_count():
		mi.set_surface_override_material(i, material_for(mi.mesh.surface_get_name(i)))


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		mouse_turn += event.relative.x * mouse_units_per_pixel
		mouse_pitch += event.relative.y * mouse_units_per_pixel
	elif event is InputEventKey and event.pressed and not event.echo:
		if event.physical_keycode == KEY_ESCAPE:
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		else:
			tapped |= key_button(event.physical_keycode)
	elif event is InputEventMouseButton and event.pressed:
		if Input.mouse_mode != Input.MOUSE_MODE_CAPTURED:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
		elif event.button_index == MOUSE_BUTTON_MIDDLE:
			tapped |= PAD_L3


func key_button(key: Key) -> int:
	match key:
		KEY_SPACE:
			return PAD_TRIANGLE
		KEY_C, KEY_CTRL:
			return PAD_L2
		KEY_E:
			return PAD_SQUARE
		KEY_LEFT:
			return PAD_LEFT
		KEY_RIGHT:
			return PAD_RIGHT
	return 0


func held_buttons() -> int:
	var b := 0
	for key in [KEY_SPACE, KEY_C, KEY_CTRL, KEY_E, KEY_LEFT, KEY_RIGHT]:
		if Input.is_physical_key_pressed(key):
			b |= key_button(key)
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		b |= PAD_R2
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_MIDDLE):
		b |= PAD_L3
	return b


func _physics_process(_delta: float) -> void:
	# The analog roles, as the runtime's keyboard gives them: the forward role is negative forward.
	var fwd := 0
	var strafe := 0
	var turn := 0
	if Input.is_physical_key_pressed(KEY_W) or auto_walk > 0:
		fwd -= RUN
	if Input.is_physical_key_pressed(KEY_S):
		fwd += RUN
	if Input.is_physical_key_pressed(KEY_D):
		strafe += RUN
	if Input.is_physical_key_pressed(KEY_A):
		strafe -= RUN
	if auto_walk > 0:
		auto_walk -= 1
	# The mouse's turn this tick, in 16.16 game units; pitch: the core does pitch -= role / 2, and
	# moving the mouse up (negative y) looks up.
	turn += int(mouse_turn * 65536.0)
	var pitch_role := int(mouse_pitch * 2.0 * 65536.0)
	mouse_turn = 0.0
	mouse_pitch = 0.0
	prev_eye = cur_eye
	prev_yaw = cur_yaw
	prev_pitch = cur_pitch
	game.tick(held_buttons() | tapped, fwd, strafe, turn, pitch_role)
	tapped = 0
	cur_eye = game.get_eye_position()
	cur_yaw = game.get_yaw()
	cur_pitch = game.get_pitch()
	var lights := game.get_light_colours()
	if not lights.is_empty():
		for mat in materials.values():
			mat.set_shader_parameter("light_colours", lights)
	for i in skinned:
		var mi: MeshInstance3D = skinned[i]
		mi.mesh = game.build_actor_mesh(i)
		apply_materials(mi)


func _process(_delta: float) -> void:
	var f := Engine.get_physics_interpolation_fraction()
	cam.position = prev_eye.lerp(cur_eye, f)
	# The mouse turns the view at once; the core gets it on the next tick.
	var yaw := lerp_angle(prev_yaw, cur_yaw, f) - mouse_turn * ANGLE
	var pitch := clampf(lerpf(prev_pitch, cur_pitch, f) - mouse_pitch * ANGLE, -682 * ANGLE, 682 * ANGLE)
	cam.rotation = Vector3(pitch, yaw, 0.0)
	var s := game.get_state()
	hud.text = "%d:%d  tick %d  pos %.1f %.1f %.1f  %s%s  zone %d  %d fps" % [
		game.get_level_info().mission, game.get_level_info().level, s.tick, s.x, s.y, s.z,
		"ground" if s.ground else "air", " crouched" if s.crouched else "", s.zone, Engine.get_frames_per_second()]
	frames += 1
	if not screenshot.is_empty() and frames == screenshot_frame:
		get_viewport().get_texture().get_image().save_png(screenshot)
		print("Valor: saved ", screenshot, " at tick ", s.tick)
		get_tree().quit()


func _exit_tree() -> void:
	if game:
		print("Valor: quit at ", game.get_state())
