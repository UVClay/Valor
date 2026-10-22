extends RefCounted
## The player's settings (preloaded as `Settings` where used), kept in Godot's user folder (user://valor_settings.cfg), apart from the
## campaign's saves: the window, the field of view, the key and button bindings (Godot input
## actions the host reads to press the pad's buttons), the mouse, the square-stick option, and
## the volumes. Loaded and applied at start (main.gd); the options screen (shell.gd) edits them.
##
## The field of view: the original's is 51.4 degrees vertical (projection distance 400 for 240
## lines, scaled by 5/8). Which entities count as drawn (the logic's view test: a soldier's pose
## is rebuilt and its out-of-sight counter runs only when drawn) is decided with the recording's
## view, 21:9 at 110 degrees across (62.9 vertical), whatever Godot shows. A wider view (or a
## screen wider than 21:9) shows things the logic counts as out of view: their poses hold.

const PATH := "user://valor_settings.cfg"
const DEFAULT_FOV := 51.4
const LOGIC_HFOV := 110.0  # the engine's view test (README, The view test)
const LOGIC_ASPECT := 21.0 / 9.0
const RESOLUTIONS := [Vector2i(1280, 720), Vector2i(1600, 900), Vector2i(1920, 1080), Vector2i(2560, 1440), Vector2i(1024, 768), Vector2i(1440, 1080)]

## The actions (name, what the options screen calls it, the default events). The pad's buttons
## are the game's layout 1 (a fresh card's): fire is Cross, switch weapons Circle.
const ACTIONS := [
	["valor_forward", "Forward", [KEY_W]],
	["valor_back", "Back", [KEY_S]],
	["valor_strafe_left", "Strafe left", [KEY_A]],
	["valor_strafe_right", "Strafe right", [KEY_D]],
	["valor_turn_left", "Turn left (d-pad)", [KEY_LEFT]],
	["valor_turn_right", "Turn right (d-pad)", [KEY_RIGHT]],
	["valor_fire", "Fire (Cross)", [MOUSE_BUTTON_LEFT]],
	["valor_aim", "Aim (R2)", [MOUSE_BUTTON_RIGHT]],
	["valor_jump", "Jump (Triangle)", [KEY_SPACE]],
	["valor_crouch", "Crouch (L2)", [KEY_C, KEY_CTRL]],
	["valor_use", "Use, reload (Square)", [KEY_E]],
	["valor_switch", "Switch weapons (Circle)", [KEY_Q]],
	["valor_center", "Centre the view (L3)", [MOUSE_BUTTON_MIDDLE]],
	["valor_pause", "Pause (Start)", [KEY_ESCAPE, JOY_BUTTON_START]],
]

static var fullscreen := false
static var resolution := Vector2i(1280, 720)
static var fov := DEFAULT_FOV
static var mouse_speed := 3.0  # game angle units (4096 a turn) per mouse pixel
static var invert_y := false
static var square_stick := false
static var master_volume := 1.0
static var effects_volume := 4  # the game's Sound Effects setting, 0..6 (g_game+0x88, default 4)
static var music_volume := 4  # the game's Background setting, 0..6 (g_game+0x80, default 4)
static var bindings := {}  # action -> [InputEvent]
static var disc_image := ""  # the user's disc image (.cue or .bin), read in place


static func load_and_apply() -> void:
	var cf := ConfigFile.new()
	var ok := FileAccess.file_exists(PATH) and cf.load(PATH) == OK
	fullscreen = bool(cf.get_value("video", "fullscreen", false)) if ok else false
	resolution = cf.get_value("video", "resolution", Vector2i(1280, 720)) if ok else Vector2i(1280, 720)
	fov = float(cf.get_value("video", "fov", DEFAULT_FOV)) if ok else DEFAULT_FOV
	mouse_speed = float(cf.get_value("input", "mouse_speed", 3.0)) if ok else 3.0
	invert_y = bool(cf.get_value("input", "invert_y", false)) if ok else false
	square_stick = bool(cf.get_value("input", "square_stick", false)) if ok else false
	master_volume = float(cf.get_value("audio", "master", 1.0)) if ok else 1.0
	effects_volume = int(cf.get_value("audio", "effects", 4)) if ok else 4
	disc_image = String(cf.get_value("disc", "image", "")) if ok else ""
	music_volume = int(cf.get_value("audio", "music", 4)) if ok else 4
	bindings.clear()
	for a in ACTIONS:
		var saved = cf.get_value("bindings", a[0], null) if ok else null
		bindings[a[0]] = decode(saved) if saved is Array else default_events(a[2])
	apply()


static func save() -> void:
	var cf := ConfigFile.new()
	cf.set_value("video", "fullscreen", fullscreen)
	cf.set_value("video", "resolution", resolution)
	cf.set_value("video", "fov", fov)
	cf.set_value("input", "mouse_speed", mouse_speed)
	cf.set_value("input", "invert_y", invert_y)
	cf.set_value("input", "square_stick", square_stick)
	cf.set_value("audio", "master", master_volume)
	cf.set_value("audio", "effects", effects_volume)
	cf.set_value("audio", "music", music_volume)
	cf.set_value("disc", "image", disc_image)
	for a in bindings:
		cf.set_value("bindings", a, encode(bindings[a]))
	cf.save(PATH)


## Bindings in the file: ["key", physical keycode], ["mouse", button], ["joy", button].
static func encode(events: Array) -> Array:
	var out := []
	for ev in events:
		if ev is InputEventKey:
			out.append(["key", int(ev.physical_keycode)])
		elif ev is InputEventMouseButton:
			out.append(["mouse", int(ev.button_index)])
		elif ev is InputEventJoypadButton:
			out.append(["joy", int(ev.button_index)])
	return out


static func decode(saved: Array) -> Array:
	var out := []
	for e in saved:
		if not (e is Array and e.size() == 2):
			continue
		var ev: InputEvent
		match String(e[0]):
			"key":
				ev = InputEventKey.new()
				ev.physical_keycode = int(e[1])
			"mouse":
				ev = InputEventMouseButton.new()
				ev.button_index = int(e[1])
			"joy":
				ev = InputEventJoypadButton.new()
				ev.button_index = int(e[1])
		if ev:
			out.append(ev)
	return out


static func default_events(codes: Array) -> Array:
	var out := []
	for c in codes:
		out.append(event_for(c))
	return out


## The defaults' codes: mouse buttons and joypad buttons are small numbers, keys large ones.
static func event_for(code: int) -> InputEvent:
	if code == JOY_BUTTON_START:
		var j := InputEventJoypadButton.new()
		j.button_index = JOY_BUTTON_START
		return j
	if code in [MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE]:
		var mb := InputEventMouseButton.new()
		mb.button_index = code
		return mb
	var k := InputEventKey.new()
	k.physical_keycode = code
	return k


static func reset_bindings() -> void:
	for a in ACTIONS:
		bindings[a[0]] = default_events(a[2])
	apply_bindings()


static func apply() -> void:
	apply_window()
	apply_bindings()
	apply_audio()


static func apply_window() -> void:
	if DisplayServer.get_name() == "headless":
		return
	if fullscreen:
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN)
	else:
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
		DisplayServer.window_set_size(resolution)


static func apply_bindings() -> void:
	for a in ACTIONS:
		if not InputMap.has_action(a[0]):
			InputMap.add_action(a[0])
		InputMap.action_erase_events(a[0])
		for ev in bindings.get(a[0], []):
			InputMap.action_add_event(a[0], ev)


## The master volume on Godot's master bus; the game's Sound Effects setting on the effect voices'
## buses (present/audio.gd's "Voice<n>"), relative to its default 4 (the setting times 42 is the
## effects' level, shell.md: so the level scales linearly, an inference for the SPU side). The
## Background (music) setting is kept for the presentation layer's music player.
static func apply_audio() -> void:
	AudioServer.set_bus_volume_db(0, linear_to_db(maxf(master_volume, 0.0001)))
	var fx := linear_to_db(maxf(effects_volume / 4.0, 0.0001))
	for i in AudioServer.bus_count:
		if AudioServer.get_bus_name(i).begins_with("Voice"):
			AudioServer.set_bus_volume_db(i, fx)


## The view a vertical field of view gives across a screen of this aspect, in degrees.
static func horizontal_fov(vfov: float, aspect: float) -> float:
	return rad_to_deg(2.0 * atan(tan(deg_to_rad(vfov) / 2.0) * aspect))


## The vertical extent of the logic's view (the recording's 21:9 at 110 degrees): 62.9.
static func logic_vfov() -> float:
	return rad_to_deg(2.0 * atan(tan(deg_to_rad(LOGIC_HFOV) / 2.0) / LOGIC_ASPECT))


## Whether the view shown stays inside the logic's (else entities at its edges can be shown but
## counted as out of view).
static func fov_within_logic(vfov: float, aspect: float) -> bool:
	return vfov <= logic_vfov() + 0.05 and horizontal_fov(vfov, aspect) <= LOGIC_HFOV + 0.05


static func binding_text(action: String) -> String:
	var names := []
	for ev in bindings.get(action, []):
		if ev is InputEventKey:
			names.append(OS.get_keycode_string(ev.physical_keycode))
		elif ev is InputEventMouseButton:
			names.append({MOUSE_BUTTON_LEFT: "Left mouse", MOUSE_BUTTON_RIGHT: "Right mouse", MOUSE_BUTTON_MIDDLE: "Middle mouse"}.get(ev.button_index, "Mouse %d" % ev.button_index))
		elif ev is InputEventJoypadButton:
			names.append("Pad %d" % ev.button_index)
	return ", ".join(names) if names.size() else "(none)"
