extends Node
## The presentation layer, drawn natively from the core's state (an autoload, "Present"): the HUD,
## the first-person weapon, the sky, the sounds and music, and the gamepad. It reads the level the
## main scene loaded (main.gd: `game`, `cam`) and the same disc files through ValorAssets; nothing
## here changes the rules. docs/re: hud.md, audio.md, formats.md (Sky), animation.md (the player's
## weapon models).
##
## User arguments (after `--`): --no-hud, --no-viewmodel, --no-sky, --no-effects, --no-pvs, --no-audio, --no-music,
## --ambience-only (the B-mix music), --german (the 0 TAF: German soldiers' voices),
## --crosshair=SIZE (an always-on crosshair on the aim line, 0: none, the original's look),
## --square-stick (stretch a round stick to the DualShock's square range), --modern-sticks (the
## left stick strafes and the right one turns, instead of the original's left-turns, right-strafes),
## --perf (time the parts, printed at exit), --test-message=TEXT (a level message at the start, to see the text list), --test-press=MASK@TICK[:TICKS][,...]
## (press PS1 buttons for 2 ticks, or TICKS, from that tick: 0x40 fires, 0x80 reloads, 0x20 switches), --debug-text (keep
## main.gd's state line).

const ANGLE := TAU / 4096.0

var main: Node3D
var game: ValorGame
var assets: ValorAssets
var args := {}
var state := {}  # the core's state after the latest tick
var prev_state := {}
var tick := 0
var weapon_hash := 0  # the first-person model in hand (g_weapons[w]+0x00)
var events := []  # this tick's (derive_events)

var hud: Control
var sky: Control
var view_model: Node
var audio: Node
var effects: Node3D
var pvs: Node
var attached_level := Vector2i(-1, -1)
var attached_world: Node
var reattached := false  # --test-reattach=TICK: build the layer again once, as a level change does


func _ready() -> void:
	process_priority = 100  # after main.gd's _process (the camera placed for this frame)
	process_physics_priority = 100  # after main.gd's tick
	for a in OS.get_cmdline_user_args():
		var kv := a.trim_prefix("--").split("=", true, 1)
		args[kv[0]] = kv[1] if kv.size() > 1 else ""
	perf = args.has("perf")
	set_process(false)
	set_physics_process(false)
	get_tree().node_added.connect(_on_node_added)


func _on_node_added(_n: Node) -> void:
	# main.gd builds the level in its _ready; attach once it has a camera.
	var scene := get_tree().current_scene
	if main == null and scene != null and scene.get("cam") != null and scene.get("game") != null:
		main = scene
		call_deferred("_attach")


## A new level (main.gd's next_level or start_level: another world, other files): everything
## the layer built for the last one goes, and it is built again for this one.
func _detach() -> void:
	for c in get_children():
		remove_child(c)
		c.queue_free()
	if effects:
		effects.get_parent().remove_child(effects)
		effects.queue_free()
	sky = null
	view_model = null
	hud = null
	audio = null
	effects = null
	pvs = null
	assets = null


func _attach() -> void:
	_detach()
	game = main.game
	var info: Dictionary = game.get_level_info()
	attached_level = Vector2i(int(info.mission), int(info.level))
	attached_world = main.get("world")
	var data: String = args.get("data", ProjectSettings.globalize_path("res://").path_join("../../../extracted/moh/DATA").simplify_path())
	assets = ValorAssets.new()
	if args.has("disc-image") and not assets.mount_disc(String(args["disc-image"])):  # main.gd's
		push_error("Valor present: " + assets.get_error())
	if not assets.load(data, int(info.mission), int(info.level), not args.has("german")):
		push_error("Valor present: " + assets.get_error())
		return
	if not args.has("debug-text") and main.get("hud") != null:
		main.hud.visible = false
	state = game.get_state()
	prev_state = state
	if not args.has("no-sky"):
		sky = preload("res://present/sky.gd").new()
		sky.setup(self)
		if sky.get_parent() == null:  # no sky in this level
			sky.free()
			sky = null
	if not args.has("no-viewmodel"):
		view_model = preload("res://present/view_model.gd").new()
		add_child(view_model)
		view_model.setup(self)
	if not args.has("no-hud"):
		var layer := CanvasLayer.new()
		layer.layer = 2
		add_child(layer)
		hud = preload("res://present/hud.gd").new()
		layer.add_child(hud)
		hud.setup(self)
	if not args.has("no-effects"):
		effects = preload("res://present/effects.gd").new()
		effects.setup(self)
	if not args.has("no-pvs"):
		pvs = preload("res://present/world_pvs.gd").new()
		add_child(pvs)
		pvs.setup(self)
	if not args.has("no-audio"):
		audio = preload("res://present/audio.gd").new()
		add_child(audio)
		audio.setup(self)
	set_process(true)
	set_physics_process(true)


## The weapon in hand: from the engine's state, else the level's first-person model of the
## first weapon it has (a walk without the engine: the Garand, the Thompson or a pistol).
func current_weapon() -> int:
	return int(state.get("weapon", -1))


func weapon_model(w: int) -> int:
	if w < 0:
		return 0
	return assets.level_bin(0x80093640 + 0x44 * w, 4).decode_u32(0)


func _physics_process(_delta: float) -> void:
	var info: Dictionary = game.get_level_info()
	if Vector2i(int(info.mission), int(info.level)) != attached_level or main.get("world") != attached_world \
			or (args.has("test-reattach") and int(state.get("tick", 0)) == int(args["test-reattach"]) and tick > 0 and not reattached):
		reattached = true
		_attach()
		return
	var t0 := Time.get_ticks_usec()
	prev_state = state
	state = game.get_state()
	_perf("get_state", t0)
	tick += 1
	var h: int = int(state.get("weapon_model", 0))
	if h == 0:
		h = weapon_model(current_weapon())
	weapon_hash = h
	events = derive_events()
	for part in [view_model, hud, audio, effects, pvs]:
		if part:
			var t := Time.get_ticks_usec()
			part.on_tick()
			_perf("tick " + part.get_script().resource_path.get_file(), t)
	if perf and tick % 30 == 0 and game.is_engine_running():
		# what main.gd's update_engine_actors spends re-skinning every skinned actor each tick
		var ta := Time.get_ticks_usec()
		var actors: Dictionary = game.get_engine_actors()
		_perf("(sample) get_engine_actors", ta)
		var n := 0
		var tb := Time.get_ticks_usec()
		for hnd in actors:
			if actors[hnd].skinned:
				game.build_engine_actor_mesh(hnd)
				n += 1
		if n:
			_perf_add("(sample) rebuild a skinned actor mesh", float(Time.get_ticks_usec() - tb) / n)
			_perf_add("(sample) skinned actors", float(n))
	if perf:
		# main.gd's tick (the engine, the actors' meshes) is the rest of the physics time
		var total := Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1e6
		_perf_add("main.gd tick and the engine", total - float(Time.get_ticks_usec() - t0))


func _process(_delta: float) -> void:
	var t0 := Time.get_ticks_usec()
	if view_model:
		view_model.on_frame()
	if hud:
		hud.queue_redraw()
	if sky:
		sky.queue_redraw()
	_perf("frame present", t0)
	if perf:
		frames += 1
		_perf_add("frame (Performance.TIME_PROCESS)", Performance.get_monitor(Performance.TIME_PROCESS) * 1e6)
		_perf_add("frame setup CPU (RenderingServer)", RenderingServer.get_frame_setup_time_cpu() * 1000.0)
		_perf_add("draw calls", float(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)))
		_perf_add("objects drawn", float(Performance.get_monitor(Performance.RENDER_TOTAL_OBJECTS_IN_FRAME)))
		_perf_add("primitives drawn", float(Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME)))
		_perf_add("frame time", 1e6 / maxf(Engine.get_frames_per_second(), 1.0))


# ---------------------------------------------------------------- --perf: where the time goes

var perf := false
var perf_sum := {}
var perf_max := {}
var perf_n := {}
var frames := 0


func _perf(name: String, since: int) -> void:
	if perf:
		_perf_add(name, float(Time.get_ticks_usec() - since))


func _perf_add(name: String, us: float) -> void:
	perf_sum[name] = perf_sum.get(name, 0.0) + us
	perf_max[name] = maxf(perf_max.get(name, 0.0), us)
	perf_n[name] = perf_n.get(name, 0) + 1


func _exit_tree() -> void:
	_detach()
	if not perf:
		return
	print("Valor perf: %d frames, %d ticks; average and worst microseconds per call:" % [frames, tick])
	var names := perf_sum.keys()
	names.sort_custom(func(a, b): return perf_sum[a] > perf_sum[b])
	for n in names:
		print("Valor perf:   %-34s %9.0f avg %9.0f max  x%d" % [n, perf_sum[n] / perf_n[n], perf_max[n], perf_n[n]])


## What happened this tick, read from the change of the core's state (the core doesn't report its
## events yet: these stand in for the calls the HUD and the sounds hang on, hud.md and audio.md).
## Only the slot in hand is visible, so ammo picked up for another weapon goes unseen.
func derive_events() -> Array:
	var out := []
	if not state.has("weapon") or not prev_state.has("weapon"):
		return out
	var w := int(state.weapon)
	if int(prev_state.weapon) != w:
		out.append({"kind": "draw", "weapon": w})
		return out
	var dclip := int(state.clip) - int(prev_state.clip)
	var dres := int(state.reserve) - int(prev_state.reserve)
	if dclip < 0:
		out.append({"kind": "fire", "weapon": w})
	elif dclip > 0 and dres < 0:
		out.append({"kind": "reload", "weapon": w})
	elif dres > 0:
		out.append({"kind": "ammo", "weapon": w})
	var dh := float(state.health) - float(prev_state.health)
	if dh > 0.0:
		# health_pickup_touch: kind 0 +13, 1 +32, 2 +64 (capped at 128)
		out.append({"kind": "health", "pack": 0 if dh <= 13.0 else (1 if dh <= 32.0 else 2)})
	elif dh < 0.0:
		out.append({"kind": "hurt", "dead": float(state.health) <= 0.0})
	return out


## The view's yaw and pitch in game units (4096 per turn), as main.gd shows them this frame.
func view_yaw() -> float:
	return -main.cam.rotation.y / ANGLE


func view_pitch() -> float:
	return main.cam.rotation.x / ANGLE


# ---------------------------------------------------------------- the gamepad

## The pad's input for this tick, for main.gd to add to the keyboard's and mouse's: the PS1
## buttons (the game's bit layout) and the four roles (forward, strafe, pitch, turn; 16.16), the
## sticks going through the game's own analog curve (the core's role_axis, movement.md) as a
## DualShock's would. --square-stick first stretches a modern round stick's range to the
## DualShock's square one, which the game's per-axis curve expects for full diagonal speed.
func pad_input() -> Dictionary:
	var out := {"buttons": 0, "roles": PackedInt32Array([0, 0, 0, 0])}
	if assets == null:
		return out
	if args.has("test-turn"):
		# --test-turn=UNITS@TICK:TICKS: the turn role (16.16 game units a tick), for screenshots
		var tt: PackedStringArray = String(args["test-turn"]).split("@")
		var when: PackedStringArray = tt[1].split(":")
		var now := int(state.get("tick", 0))
		if now >= int(when[0]) and now < int(when[0]) + (int(when[1]) if when.size() > 1 else 1):
			var r: PackedInt32Array = out.roles
			r[3] = int(float(tt[0]) * 65536.0)
			out.roles = r
	if args.has("test-press"):
		for press in String(args["test-press"]).split(","):
			var tp: PackedStringArray = press.split("@")
			var when: PackedStringArray = tp[1].split(":")
			var at := int(when[0])
			var hold := int(when[1]) if when.size() > 1 else 2
			var now := int(state.get("tick", 0))
			if now >= at and now < at + hold:
				out.buttons |= tp[0].hex_to_int() if tp[0].begins_with("0x") else int(tp[0])
	var dev := -1
	for d in Input.get_connected_joypads():
		dev = d
		break
	if dev < 0:
		return out
	var map := {
		JOY_BUTTON_Y: 0x10, JOY_BUTTON_B: 0x20, JOY_BUTTON_A: 0x40, JOY_BUTTON_X: 0x80,
		JOY_BUTTON_LEFT_SHOULDER: 0x4, JOY_BUTTON_RIGHT_SHOULDER: 0x8,
		JOY_BUTTON_BACK: 0x100, JOY_BUTTON_LEFT_STICK: 0x200, JOY_BUTTON_RIGHT_STICK: 0x400,
		JOY_BUTTON_START: 0x800, JOY_BUTTON_DPAD_UP: 0x1000, JOY_BUTTON_DPAD_RIGHT: 0x2000,
		JOY_BUTTON_DPAD_DOWN: 0x4000, JOY_BUTTON_DPAD_LEFT: 0x8000,
	}
	var b := 0
	for k in map:
		if Input.is_joy_button_pressed(dev, k):
			b |= map[k]
	if Input.get_joy_axis(dev, JOY_AXIS_TRIGGER_LEFT) > 0.5:
		b |= 0x1  # L2
	if Input.get_joy_axis(dev, JOY_AXIS_TRIGGER_RIGHT) > 0.5:
		b |= 0x2  # R2
	var left := Vector2(Input.get_joy_axis(dev, JOY_AXIS_LEFT_X), Input.get_joy_axis(dev, JOY_AXIS_LEFT_Y))
	var right := Vector2(Input.get_joy_axis(dev, JOY_AXIS_RIGHT_X), Input.get_joy_axis(dev, JOY_AXIS_RIGHT_Y))
	if args.has("square-stick"):
		left = to_square(left)
		right = to_square(right)
	if args.has("modern-sticks"):
		# a fresh card's sticks: ly forward, rx strafe, ry pitch, lx turn
		var lx := left.x
		left.x = right.x
		right.x = lx
	out.buttons = b
	out.roles = assets.stick_roles(stick_byte(right.x), stick_byte(right.y), stick_byte(left.x), stick_byte(left.y))
	return out


static func stick_byte(v: float) -> int:
	return clampi(int(round(128.0 + v * 127.5)), 0, 255)


## A round stick's position stretched along its direction to the square: the same angle, the
## distance to the square's edge where the circle's edge was.
static func to_square(v: Vector2) -> Vector2:
	var m := maxf(absf(v.x), absf(v.y))
	if m < 0.0001:
		return v
	return (v * (v.length() / m)).clampf(-1.0, 1.0)
