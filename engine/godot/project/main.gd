extends Node3D
## Loads a level through the core (ValorGame), draws it, and walks it: the core steps the player at
## 30 Hz in _physics_process; the camera is interpolated between ticks at the display rate, and
## the mouse turns the view at once (its yaw reaches the core on the next tick).
##
## User arguments (after `--`): --data=DIR (extracted/moh/DATA), --level=M:L (1:1),
## --walk=TICKS (walk forward on its own), --place=X,Y,Z,YAW,PITCH (game units and angles), --screenshot=FILE.png --frames=N (save the frame N and quit),
## --engine[=AMMO] (the whole engine live: the level built from the disc, its soldiers and dogs on
## their own logic, drawn from the engine's poses; the left mouse button fires, Q switches weapons),
## --replay=LOG (with --engine: a recording's input instead of the keyboard and mouse).

const RUN := 0x22DD00  # 34.863 units per tick: the keyboard's (and a full stick's) speed
# The pad's buttons (a fresh card's layout, controls.cpp): keys press them, and the core reads
# them as the game does: the arrows are the d-pad (turning ramps up to 60 units per tick).
const PAD_TRIANGLE := 0x10  # jump
const PAD_L2 := 0x1  # crouch
const PAD_SQUARE := 0x80  # Use, reload
const PAD_R2 := 0x2  # aim
const PAD_L3 := 0x200  # centre the view
const PAD_CROSS := 0x40  # fire (the engine's layout 1)
const PAD_CIRCLE := 0x20  # switch weapons (the engine's layout 1)
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
# The mouse's turn and look given to the core at the last tick (game units): the tick's yaw and
# pitch already include them, so the blend starts from the previous view plus them (else the view
# jumps back by the mouse's movement at every tick and catches up again: a 30 Hz shake)
var sent_turn := 0.0
var sent_pitch := 0.0
var first_tick := true
var tapped := 0  # buttons pressed and released since the last tick: held for one tick
var auto_walk := 0
var screenshot := ""
var screenshot_frame := 0
var frames := 0
var hud: Label
var materials := {}  # by surface name, shared by every mesh
var skinned := {}  # actor index -> MeshInstance3D
var level_info := {}
var engine_mode := false
var engine_meshes := {}  # entity handle -> MeshInstance3D
var engine_xf := {}  # entity handle -> [the previous tick's transform, this tick's]: blended per frame
var skip_to := 0  # --skip=TICKS: run the engine up to this tick at once (50 ticks a frame)
var shot_tick := 0  # --shot-tick=T: the screenshot once the core reaches tick T
var camtrace: FileAccess = null  # --camtrace=FILE: the camera per drawn frame (smoothness checks)
var spin := 0.0  # --spin=UNITS: a constant mouse turn per drawn frame, in game angle units
var aimbot := false  # --aimbot (with --engine): turn to the nearest living soldier and fire


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
	skip_to = int(args.get("skip", "0"))
	shot_tick = int(args.get("shot-tick", "0"))
	aimbot = args.has("aimbot")
	if args.has("camtrace"):
		camtrace = FileAccess.open(String(args.camtrace), FileAccess.WRITE)
	spin = float(args.get("spin", "0"))

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
	# The models the records name, posed by the core; skinned ones are rebuilt every tick. With
	# --scene=FILE (engine/tools/scene_from_dump.py), the view and the characters of a RAM dump.
	engine_mode = args.has("engine")
	if engine_mode:
		var ammo := int(args.engine) if String(args.engine) != "" else 99
		if not game.start_engine(ammo):
			push_error("Valor: " + game.get_error())
			get_tree().quit(1)
			return
		if args.has("replay"):
			game.set_engine_replay(String(args.replay))
		print("Valor: the engine runs the level live")
	else:
		game.spawn_actors()
	if args.has("scene"):
		var scene: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(args.scene))
		var pl: Dictionary = scene.player
		game.place(pl.x, pl.y, pl.z, pl.yaw, pl.pitch)
		game.remove_skinned_actors()
		for a in scene.actors:
			game.add_actor(int(a.model), Vector3(a.pos[0], a.pos[1], a.pos[2]),
				Vector3i(int(a.angles[0]), int(a.angles[1]), int(a.angles[2])), int(a.anim), int(a.frame))
	for i in game.get_actor_count():
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
		KEY_Q:
			return PAD_CIRCLE
		KEY_LEFT:
			return PAD_LEFT
		KEY_RIGHT:
			return PAD_RIGHT
	return 0


func held_buttons() -> int:
	var b := 0
	for key in [KEY_SPACE, KEY_C, KEY_CTRL, KEY_E, KEY_Q, KEY_LEFT, KEY_RIGHT]:
		if Input.is_physical_key_pressed(key):
			b |= key_button(key)
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		b |= PAD_R2
	if engine_mode and Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT) and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		b |= PAD_CROSS
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
	# The presentation layer's gamepad (present/present.gd): its buttons, and its sticks through the
	# game's analog curve as roles.
	var pad: Dictionary = get_node("/root/Present").pad_input() if has_node("/root/Present") else {}
	var pad_buttons: int = pad.get("buttons", 0)
	if pad.has("roles"):
		fwd += pad.roles[0]
		strafe += pad.roles[1]
		pitch_role += pad.roles[2]
		turn += pad.roles[3]
	sent_turn = mouse_turn
	sent_pitch = mouse_pitch
	mouse_turn = 0.0
	mouse_pitch = 0.0
	prev_eye = cur_eye
	prev_yaw = cur_yaw
	prev_pitch = cur_pitch
	if engine_mode:
		var n := 0
		while n < 50 and game.get_state().tick < skip_to - 1:
			game.engine_tick(0, 0, 0, 0, 0)
			n += 1
		var buttons := held_buttons() | tapped | pad_buttons
		if aimbot:
			var aim := aim_at_nearest()
			if aim[0]:
				game.set_engine_replay("")  # the bot takes over from the recording
				fwd = 0
				strafe = 0
				turn = aim[1]
				if int(game.get_state().tick) % 4 < 2:
					buttons |= PAD_CROSS
		game.engine_tick(buttons, fwd, strafe, turn, pitch_role)
	else:
		game.tick(held_buttons() | tapped | pad_buttons, fwd, strafe, turn, pitch_role)
	tapped = 0
	cur_eye = game.get_eye_position()
	if first_tick:  # nothing to blend from before the first tick (the engine's camera starts at 0)
		prev_eye = cur_eye
		first_tick = false
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
	if engine_mode:
		update_engine_actors()


## --aimbot: [found, turn role] toward the nearest living soldier or dog within 1500 units.
func aim_at_nearest() -> Array:
	var best := 1500.0
	var turn := 0
	var found := false
	var actors := game.get_engine_actors()
	var look := -game.get_yaw() / ANGLE  # the game's yaw, 4096 per turn
	for h in actors:
		var a: Dictionary = actors[h]
		if a.dead or not (int(a.type) == 0x1402 or int(a.type) == 0x1405):
			continue
		var d: Vector3 = (a.transform.origin - cur_eye) * ValorGame.kUnitsPerMetre
		var dist := Vector2(d.x, d.z).length()
		if dist >= best:
			continue
		var ang := atan2(d.x, -d.z) / ANGLE  # game x = Godot x, game z = -Godot z
		var off := fposmod(ang - look + 2048.0, 4096.0) - 2048.0
		best = dist
		turn = int(clampf(off, -48.0, 48.0) * 65536.0)
		found = true
	return [found, turn]


## The engine's entities as meshes: new ones added, gone ones freed, skinned ones re-posed from the
## engine's pose every tick, all placed at the entity.
func update_engine_actors() -> void:
	var actors := game.get_engine_actors()
	for h in engine_meshes.keys():
		if not actors.has(h):
			engine_meshes[h].queue_free()
			engine_meshes.erase(h)
			engine_xf.erase(h)
	for h in actors:
		var a: Dictionary = actors[h]
		var mi: MeshInstance3D = engine_meshes.get(h)
		if mi == null:
			mi = MeshInstance3D.new()
			add_child(mi)
			engine_meshes[h] = mi
			mi.mesh = game.build_engine_actor_mesh(h)
			apply_materials(mi)
		elif a.skinned:
			mi.mesh = game.build_engine_actor_mesh(h)
			apply_materials(mi)
		var xf: Transform3D = a.transform
		engine_xf[h] = [engine_xf[h][1] if engine_xf.has(h) else xf, xf]
		mi.transform = xf
	var pages := game.get_texture_pages()
	for mat in materials.values():
		mat.set_shader_parameter("pages", pages)


func _process(_delta: float) -> void:
	mouse_turn += spin
	var f := Engine.get_physics_interpolation_fraction()
	cam.position = prev_eye.lerp(cur_eye, f)
	# The mouse turns the view at once; the core gets it on the next tick.
	var yaw := lerp_angle(prev_yaw - sent_turn * ANGLE, cur_yaw, f) - mouse_turn * ANGLE
	var pitch := clampf(lerpf(prev_pitch - sent_pitch * ANGLE, cur_pitch, f) - mouse_pitch * ANGLE, -682 * ANGLE, 682 * ANGLE)
	cam.rotation = Vector3(pitch, yaw, 0.0)
	# the engine's actors move at 30 Hz: blended between their last two ticks as the camera is
	for h in engine_xf:
		var pair: Array = engine_xf[h]
		engine_meshes[h].transform = (pair[0] as Transform3D).interpolate_with(pair[1], f)
	if camtrace:
		camtrace.store_line("%d %f %f %f %f %f %f %d" % [frames, f, cam.position.x, cam.position.y, cam.position.z, yaw, pitch, Engine.get_physics_frames()])
	var s := game.get_state()
	hud.text = "%d:%d  tick %d  pos %.1f %.1f %.1f  %s%s  zone %d  %d fps" % [
		game.get_level_info().mission, game.get_level_info().level, s.tick, s.x, s.y, s.z,
		"ground" if s.ground else "air", " crouched" if s.crouched else "", s.zone, Engine.get_frames_per_second()]
	if engine_mode:
		hud.text += "\nhealth %.0f  weapon %d  %d / %d  enemies %d (%d down)" % [
			s.health, s.weapon, s.clip, s.reserve, s.enemies, s.enemies_dead]
	frames += 1
	var due := frames == screenshot_frame if shot_tick == 0 else int(s.tick) >= shot_tick
	if not screenshot.is_empty() and due:
		get_viewport().get_texture().get_image().save_png(screenshot)
		print("Valor: saved ", screenshot, " at tick ", s.tick)
		get_tree().quit()


func _exit_tree() -> void:
	if game:
		print("Valor: quit at ", game.get_state())
