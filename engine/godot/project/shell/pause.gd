extends Control
## The pause menu as the game has it (shell.md, The pause menu: start_handler 0x800625BC): PAUSED,
## MISSION OBJECTIVES, TOTAL ENEMIES NEUTRALIZED (g_stat_kills), the objectives ticked when done,
## then CONTINUE and QUIT; QUIT asks ARE YOU SURE? with NO selected; NO resumes the game (not the
## menu), YES leaves the level (level_exit(2), exit code 1, no carry: "Returning to base"). Start
## (the pause action) resumes from anywhere, but only 10 frames after the pause began. OPTIONS
## (the settings, shell/options.gd) is the port's: the game has no options in its pause menu.
## Drawn over the paused level in the original's places on its 512 x 240 screen, scaled to 4:3.

const YELLOW := Color(1.0, 0.9, 0.25)
const GREY := Color(0.7, 0.7, 0.7)
const ORANGE := Color(1.0, 0.6, 0.2)

var main: Node3D
var frames := 0
var screen: Control  # the 512 x 240 page, scaled
var items: VBoxContainer
var options: Control


func setup(m: Node3D) -> void:
	main = m
	process_mode = Node.PROCESS_MODE_ALWAYS
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var shade := ColorRect.new()
	shade.color = Color(0, 0, 0, 0.55)  # the dark translucent box
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(shade)
	var ratio := AspectRatioContainer.new()
	ratio.ratio = 4.0 / 3.0
	ratio.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(ratio)
	screen = Control.new()
	ratio.add_child(screen)
	screen.resized.connect(layout)
	show_paused()


func show_paused() -> void:
	for c in screen.get_children():
		c.queue_free()
	var state: Dictionary = main.game.get_state()
	text("PAUSED", 220, 28, YELLOW)
	text("MISSION OBJECTIVES", 148, 48, Color.WHITE)
	text("TOTAL ENEMIES NEUTRALIZED", 100, 68, GREY)
	text(str(int(state.get("kills", 0))), 416, 68, ORANGE)
	var y := 88
	for o in state.get("objectives", []):
		text("[x]" if bool(o.done) else "[ ]", 48, y, ORANGE)
		text(String(o.text).to_upper(), 72, y, ORANGE)
		y += 16
	text("\"X\" TO ACCEPT \"START\" TO CONTINUE", 48, 188, GREY)
	choices([["CONTINUE", func(): main.resume()], ["OPTIONS", show_options], ["QUIT", show_quit]], 208)
	layout()


func show_quit() -> void:
	for c in screen.get_children():
		c.queue_free()
	text("QUIT", 232, 100, YELLOW)
	text("ARE YOU SURE?", 190, 120, Color.WHITE)
	# NO resumes the game, not the pause menu
	choices([["NO", func(): main.resume()], ["YES", func(): main.resume(true)]], 150)
	layout()


func show_options() -> void:
	screen.visible = false
	options = preload("res://shell/options.gd").new()
	add_child(options)
	options.setup(main)
	options.closed.connect(func():
		options.queue_free()
		options = null
		screen.visible = true
		show_paused())


func text(s: String, x: int, y: int, color: Color) -> Label:
	var l := Label.new()
	l.text = s
	l.set_meta("at", Vector2(x, y))
	l.add_theme_color_override("font_color", color)
	screen.add_child(l)
	return l


## The menu's items on one line from x 100 (the game's CONTINUE at 100, QUIT at 388), as buttons.
func choices(list: Array, y: int) -> void:
	var h := HBoxContainer.new()
	h.set_meta("at", Vector2(100, y))
	h.add_theme_constant_override("separation", 40)
	screen.add_child(h)
	for c in list:
		var b := Button.new()
		b.text = c[0]
		b.flat = true
		b.focus_entered.connect(func(): b.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5)))
		b.focus_exited.connect(func(): b.remove_theme_color_override("font_color"))
		b.pressed.connect(c[1])
		h.add_child(b)
	h.get_child(0).call_deferred("grab_focus")


func layout() -> void:
	var k := screen.size.y / 240.0
	for c in screen.get_children():
		if c.has_meta("at"):
			var at: Vector2 = c.get_meta("at")
			c.position = Vector2(at.x * screen.size.x / 512.0, at.y * k - 8 * k)
			c.add_theme_font_size_override("font_size", int(maxf(10.0 * k, 10.0)))
			if c is HBoxContainer:
				for b in c.get_children():
					b.add_theme_font_size_override("font_size", int(maxf(10.0 * k, 10.0)))


func _process(_delta: float) -> void:
	frames += 1
	# tests: --options opens the options; --screenshot is taken here while the tree is paused
	if frames == 2 and main.args.has("options"):
		show_options()
	if not main.screenshot.is_empty() and frames == 30:
		get_viewport().get_texture().get_image().save_png(main.screenshot)
		print("Valor: saved ", main.screenshot)
		get_tree().quit()


func _unhandled_input(event: InputEvent) -> void:
	if options != null:
		return
	if event.is_action_pressed("valor_pause") and not event.is_echo() and frames >= 10:
		get_viewport().set_input_as_handled()
		main.resume()
