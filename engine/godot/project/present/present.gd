extends Node
## The presentation layer, drawn natively from the core's state (an autoload, "Present"): the HUD,
## the first-person weapon, the sky, the sounds and music, and the gamepad. It reads the level the
## main scene loaded (main.gd: `game`, `cam`) and the same disc files through ValorAssets; nothing
## here changes the rules. docs/re: hud.md, audio.md, formats.md (Sky), animation.md (the player's
## weapon models).
##
## User arguments (after `--`): --no-hud, --no-viewmodel, --no-sky, --no-effects, --no-audio, --no-music,
## --ambience-only (the B-mix music), --german (the 0 TAF: German soldiers' voices),
## --crosshair=SIZE (an always-on crosshair on the aim line, 0: none, the original's look),
## --square-stick (stretch a round stick to the DualShock's square range), --modern-sticks (the
## left stick strafes and the right one turns, instead of the original's left-turns, right-strafes),
## --test-message=TEXT (a level message at the start, to see the text list), --test-press=MASK@TICK[,...]
## (press PS1 buttons for 2 ticks from that tick: 0x40 fires, 0x80 reloads, 0x20 switches), --debug-text (keep
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


func _ready() -> void:
	process_priority = 100  # after main.gd's _process (the camera placed for this frame)
	process_physics_priority = 100  # after main.gd's tick
	for a in OS.get_cmdline_user_args():
		var kv := a.trim_prefix("--").split("=", true, 1)
		args[kv[0]] = kv[1] if kv.size() > 1 else ""
	set_process(false)
	set_physics_process(false)
	get_tree().node_added.connect(_on_node_added)


func _on_node_added(_n: Node) -> void:
	# main.gd builds the level in its _ready; attach once it has a camera.
	var scene := get_tree().current_scene
	if main == null and scene != null and scene.get("cam") != null and scene.get("game") != null:
		main = scene
		call_deferred("_attach")


func _attach() -> void:
	game = main.game
	var info: Dictionary = game.get_level_info()
	var data: String = args.get("data", ProjectSettings.globalize_path("res://").path_join("../../../extracted/moh/DATA").simplify_path())
	assets = ValorAssets.new()
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
	prev_state = state
	state = game.get_state()
	tick += 1
	var h: int = int(state.get("weapon_model", 0))
	if h == 0:
		h = weapon_model(current_weapon())
	weapon_hash = h
	events = derive_events()
	if view_model:
		view_model.on_tick()
	if hud:
		hud.on_tick()
	if audio:
		audio.on_tick()
	if effects:
		effects.on_tick()


func _process(_delta: float) -> void:
	if view_model:
		view_model.on_frame()
	if hud:
		hud.queue_redraw()
	if sky:
		sky.queue_redraw()


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
	if args.has("test-press"):
		for press in String(args["test-press"]).split(","):
			var tp: PackedStringArray = press.split("@")
			var at := int(tp[1])
			var now := int(state.get("tick", 0))
			if now >= at and now < at + 2:
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
