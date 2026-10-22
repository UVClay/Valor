extends Control
## The options (shell/settings.gd), from the title's OPTIONS and the pause menu's: VIDEO (window,
## resolution, field of view with what it means for the logic's view), CONTROLS (bindings, mouse,
## invert Y, the square stick) and AUDIO (master, the game's Sound Effects and Background levels).
## The original's own options room has AUDIO and CONTROLLER (shell.md, Options); the rest is the
## port's. Saved on BACK.

signal closed

const Settings := preload("res://shell/settings.gd")

var main: Node3D
var box: VBoxContainer
var title: Label
var info: Label
var waiting := ""  # the action whose new binding the next key or button press sets
var wait_button: Button


func setup(m: Node3D) -> void:
	main = m
	process_mode = Node.PROCESS_MODE_ALWAYS
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var bg := ColorRect.new()
	bg.color = Color(0.05, 0.06, 0.05, 0.96)
	bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var col := VBoxContainer.new()
	col.custom_minimum_size = Vector2(640, 0)
	col.add_theme_constant_override("separation", 10)
	center.add_child(col)
	title = Label.new()
	title.add_theme_font_size_override("font_size", 30)
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	col.add_child(title)
	info = Label.new()
	info.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	info.autowrap_mode = TextServer.AUTOWRAP_WORD
	info.custom_minimum_size = Vector2(640, 0)
	col.add_child(info)
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(640, 420)
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	col.add_child(scroll)
	box = VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.add_theme_constant_override("separation", 6)
	scroll.add_child(box)
	show_menu()
	match String(main.args.get("options", "")):  # tests: --options=video|controls|audio
		"video":
			show_video()
		"controls":
			show_controls()
		"audio":
			show_audio()


func clear(heading: String, text := "") -> void:
	title.text = heading
	info.text = text
	waiting = ""
	for c in box.get_children():
		c.queue_free()


func button(text: String, action: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.pressed.connect(action)
	box.add_child(b)
	if box.get_child_count() == 1:
		b.call_deferred("grab_focus")
	return b


func row(label: String, control: Control) -> void:
	var h := HBoxContainer.new()
	var l := Label.new()
	l.text = label
	l.custom_minimum_size = Vector2(260, 0)
	h.add_child(l)
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	h.add_child(control)
	box.add_child(h)


func slider(lo: float, hi: float, step: float, value: float, changed: Callable) -> HSlider:
	var s := HSlider.new()
	s.min_value = lo
	s.max_value = hi
	s.step = step
	s.value = value
	s.custom_minimum_size = Vector2(300, 24)
	s.value_changed.connect(changed)
	return s


func show_menu() -> void:
	clear("OPTIONS")
	button("VIDEO", show_video)
	button("CONTROLS", show_controls)
	button("AUDIO", show_audio)
	button("BACK", close)


func close() -> void:
	Settings.save()
	closed.emit()


# --- VIDEO

func aspect() -> float:
	var r := get_viewport().get_visible_rect().size
	return r.x / maxf(r.y, 1.0)


func fov_text() -> String:
	var h := Settings.horizontal_fov(Settings.fov, aspect())
	var t := "Field of view %.1f degrees up and down, %.1f across on this screen. The original's is 51.4 up and down (4:3)." % [Settings.fov, h]
	if Settings.fov_within_logic(Settings.fov, aspect()):
		t += "\nWithin the game logic's own view (the recording's: %.0f across at 21:9, %.1f up and down)." % [Settings.LOGIC_HFOV, Settings.logic_vfov()]
	else:
		t += "\nWider than the game logic's view (%.0f across at 21:9, %.1f up and down): soldiers and objects at the screen's edges count as out of sight, so their poses hold and out-of-sight timers run there, as they would off the original's screen." % [Settings.LOGIC_HFOV, Settings.logic_vfov()]
	return t


func show_video() -> void:
	clear("VIDEO", fov_text())
	var full := CheckButton.new()
	full.button_pressed = Settings.fullscreen
	full.toggled.connect(func(on: bool):
		Settings.fullscreen = on
		Settings.apply_window())
	row("Fullscreen", full)
	var res := OptionButton.new()
	for r in Settings.RESOLUTIONS:
		res.add_item("%d x %d" % [r.x, r.y])
		if r == Settings.resolution:
			res.select(res.item_count - 1)
	res.item_selected.connect(func(i: int):
		Settings.resolution = Settings.RESOLUTIONS[i]
		Settings.apply_window()
		info.text = fov_text())
	row("Window size", res)
	var fov_label := Label.new()
	fov_label.text = "%.1f" % Settings.fov
	var s := slider(40.0, 90.0, 0.1, Settings.fov, func(v: float):
		Settings.fov = v
		fov_label.text = "%.1f" % v
		info.text = fov_text()
		main.set_fov(v))
	var h := HBoxContainer.new()
	s.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	h.add_child(s)
	h.add_child(fov_label)
	row("Field of view (vertical)", h)
	button("ORIGINAL FIELD OF VIEW (51.4)", func():
		Settings.fov = Settings.DEFAULT_FOV
		main.set_fov(Settings.fov)
		show_video())
	button("BACK", show_menu)


# --- CONTROLS

func show_controls(text := "") -> void:
	clear("CONTROLS", text if text else "Choose an action, then press its new key or button (Esc keeps the old one).")
	for a in Settings.ACTIONS:
		var action: String = a[0]
		var b := Button.new()
		b.text = Settings.binding_text(action)
		b.pressed.connect(func():
			waiting = action
			wait_button = b
			b.text = "press a key or button...")
		row(String(a[1]), b)
	var speed_label := Label.new()
	speed_label.text = "%.1f" % Settings.mouse_speed
	var sp := slider(0.5, 10.0, 0.1, Settings.mouse_speed, func(v: float):
		Settings.mouse_speed = v
		speed_label.text = "%.1f" % v)
	var h := HBoxContainer.new()
	sp.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	h.add_child(sp)
	h.add_child(speed_label)
	row("Mouse speed (turn units a pixel)", h)
	var inv := CheckButton.new()
	inv.button_pressed = Settings.invert_y
	inv.toggled.connect(func(on: bool): Settings.invert_y = on)
	row("Invert mouse Y", inv)
	var sq := CheckButton.new()
	sq.button_pressed = Settings.square_stick
	sq.toggled.connect(func(on: bool): Settings.square_stick = on)
	row("Square stick (a round stick reaches the DualShock's corners)", sq)
	button("DEFAULT BINDINGS", func():
		Settings.reset_bindings()
		show_controls("Bindings back to the defaults."))
	button("BACK", show_menu)


func _input(event: InputEvent) -> void:
	if waiting.is_empty() or not event.is_pressed() or event.is_echo():
		return
	if not (event is InputEventKey or event is InputEventMouseButton or event is InputEventJoypadButton):
		return
	get_viewport().set_input_as_handled()
	var action := waiting
	waiting = ""
	if event is InputEventKey and event.physical_keycode == KEY_ESCAPE:
		wait_button.text = Settings.binding_text(action)
		return
	var ev: InputEvent
	if event is InputEventKey:
		ev = InputEventKey.new()
		ev.physical_keycode = event.physical_keycode
	elif event is InputEventMouseButton:
		ev = InputEventMouseButton.new()
		ev.button_index = event.button_index
	else:
		ev = InputEventJoypadButton.new()
		ev.button_index = event.button_index
	# one action per key: taken from any other action that had it
	for a in Settings.bindings:
		var kept := []
		for old in Settings.bindings[a]:
			if not same_event(old, ev):
				kept.append(old)
		Settings.bindings[a] = kept
	Settings.bindings[action] = [ev]
	Settings.apply_bindings()
	call_deferred("show_controls", "%s: %s" % [action.trim_prefix("valor_").replace("_", " "), Settings.binding_text(action)])


static func same_event(a: InputEvent, b: InputEvent) -> bool:
	if a is InputEventKey and b is InputEventKey:
		return a.physical_keycode == b.physical_keycode
	if a is InputEventMouseButton and b is InputEventMouseButton:
		return a.button_index == b.button_index
	if a is InputEventJoypadButton and b is InputEventJoypadButton:
		return a.button_index == b.button_index
	return false


# --- AUDIO

func show_audio() -> void:
	clear("AUDIO", "Sound Effects and Background are the game's own settings (0..6, 4 by default).")
	row("Master volume", slider(0.0, 1.0, 0.05, Settings.master_volume, func(v: float):
		Settings.master_volume = v
		Settings.apply_audio()))
	row("Sound effects", slider(0, 6, 1, Settings.effects_volume, func(v: float):
		Settings.effects_volume = int(v)
		Settings.apply_audio()))
	row("Background (music)", slider(0, 6, 1, Settings.music_volume, func(v: float):
		Settings.music_volume = int(v)
		Settings.apply_audio()))
	button("BACK", show_menu)
