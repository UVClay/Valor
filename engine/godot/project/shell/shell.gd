extends Control
## The front end around the levels (docs/re/shell.md), drawn natively: the title, a new campaign's
## codename, the mission select (the campaign's slots and their unlocked levels with their stars),
## passwords, loading and saving, the briefing before a level (its three pages from the disc), and
## after it the results (the statistics, the rating, the medals earned) with the post-mission
## choices CONTINUE, REPLAY, SAVE and WAR ROOM. The rules are the game's (ValorShell, ported from
## SHELL.BIN); main.gd runs the levels.
##
## Saves go to Godot's user folder (user://valor_saves), one file per save holding what the
## memory card file holds; the recordings' saves/ folder is never read or written.

const SAVE_DIR := "user://valor_saves"
const MEDALS := {
	0x001: "Mission 1 medal", 0x002: "Mission 2 medal", 0x004: "Mission 3 medal", 0x008: "Mission 4 medal",
	0x010: "Mission 5 medal", 0x020: "Mission 7 medal", 0x080: "Mission 9 medal",
	0x200: "Bronze Star", 0x400: "Silver Star", 0x800: "Medal of Honor", 0x100: "DWI Medal",
}

var main: Node3D
var data := ""
var shell := ValorShell.new()
var started := false  # a campaign begun or loaded: CONTINUE
var playing := false
var results := {}
var committed := false
var pending_level := {}  # {slot, level} the next play is of
var box: VBoxContainer
var title: Label
var info: Label
var page_view: TextureRect
var page_box: AspectRatioContainer
var pages := []
var page := 0
var auto := 0  # --shell-auto=N (a test): a new campaign played through N levels on its own (with
# --end-every), each briefing skipped, CONTINUE after each, a save at the end


func setup(m: Node3D, data_dir: String) -> void:
	main = m
	data = data_dir
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var bg := ColorRect.new()
	bg.color = Color(0.05, 0.06, 0.05)
	bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var col := VBoxContainer.new()
	col.custom_minimum_size = Vector2(560, 0)
	col.add_theme_constant_override("separation", 10)
	center.add_child(col)
	title = Label.new()
	title.add_theme_font_size_override("font_size", 34)
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	col.add_child(title)
	info = Label.new()
	info.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	info.autowrap_mode = TextServer.AUTOWRAP_WORD
	col.add_child(info)
	box = VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	col.add_child(box)
	# the briefing's 512 x 240 pages shown as the PS1 showed them: on a 4:3 screen
	page_view = TextureRect.new()
	page_view.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	page_view.stretch_mode = TextureRect.STRETCH_SCALE
	page_view.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
	page_box = AspectRatioContainer.new()
	page_box.ratio = 4.0 / 3.0
	page_box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	page_box.add_child(page_view)
	page_box.visible = false
	add_child(page_box)
	auto = int(main.args.get("shell-auto", "0"))
	show_title()
	if auto > 0 or main.args.has("shell-brief"):  # tests: a new campaign's first briefing
		shell.new_game("TESTER")
		started = true
		call_deferred("briefing", 0, 0)


func clear(heading: String, text := "") -> void:
	visible = true
	page_box.visible = false
	title.text = heading
	info.text = text
	for c in box.get_children():
		c.queue_free()


func button(text: String, action: Callable, enabled := true) -> Button:
	var b := Button.new()
	b.text = text
	b.disabled = not enabled
	b.pressed.connect(action)
	box.add_child(b)
	if box.get_child_count() == 1:
		b.call_deferred("grab_focus")
	return b


# --- the title (m_title)

func show_title(text := "") -> void:
	clear("MEDAL OF HONOR", text)
	if started:
		var n: Dictionary = shell.get_next()
		button("CONTINUE  (%s, %s)" % [shell.get_codename(), level_name(n.slot, n.level)], func(): briefing(n.slot, n.level))
	button("NEW GAME", show_codename)
	button("MISSIONS", show_missions, started)
	button("LOAD", show_load)
	button("SAVE", save_game, started)
	button("PASSWORD", show_password)
	button("OPTIONS", show_options)
	button("QUIT", func(): get_tree().quit())


# --- the settings (shell/options.gd)

func show_options() -> void:
	var o: Control = preload("res://shell/options.gd").new()
	add_child(o)
	o.setup(main)
	o.closed.connect(func():
		o.queue_free()
		show_title())


func level_name(slot: int, level: int) -> String:
	for s in shell.get_slots():
		if int(s.slot) == slot:
			return "MISSION %d LEVEL %d" % [int(s.mission), int(s.levels[level].dir)]
	return ""


# --- a new campaign (m_pname: the codename, then new_game)

func show_codename() -> void:
	clear("ENTER YOUR CODENAME", "Up to 10 letters.")
	var e := LineEdit.new()
	e.max_length = 10
	e.text = ""
	box.add_child(e)
	e.call_deferred("grab_focus")
	var go := func():
		var name := e.text.strip_edges().to_upper()
		if name.is_empty():
			name = "NONAME"
		shell.new_game(name)
		started = true
		briefing(0, 0)
	e.text_submitted.connect(func(_t): go.call())
	button("SEND", go)
	button("BACK", show_title)


# --- the mission select: the slots and their levels as the campaign has them unlocked

func show_missions() -> void:
	clear("MISSIONS", "The levels open in this campaign; stars are the best rating.")
	for s in shell.get_slots():
		for l in s.levels:
			if not bool(l.unlocked):
				continue
			var stars := "" if int(l.best) < 0 else " " + "*".repeat(int(l.best) + 1)
			var slot := int(s.slot)
			var lv := int(l.level)
			button("MISSION %d  LEVEL %d%s" % [int(s.mission), int(l.dir), stars], func(): briefing(slot, lv))
	button("BACK", show_title)


# --- Options > PASSWORD

func show_password(text := "") -> void:
	clear("PASSWORD", text)
	var e := LineEdit.new()
	e.max_length = 12
	box.add_child(e)
	e.call_deferred("grab_focus")
	var send := func():
		var r: Dictionary = shell.password(e.text)
		var i := int(r.index)
		show_password("UNKNOWN PASSWORD" if i == -1 else ("NOT NOW" if i == -2 else String(r.text).to_upper()))
	e.text_submitted.connect(func(_t): send.call())
	button("SEND", send)
	button("BACK", show_title)


# --- saves (the memory card's file per save, in Godot's user folder)

func save_game() -> void:
	var path := shell.save(ProjectSettings.globalize_path(SAVE_DIR))
	show_title("SAVED" if not path.is_empty() else shell.get_error())


func show_load() -> void:
	clear("LOAD", "Saves in " + ProjectSettings.globalize_path(SAVE_DIR))
	var any := false
	for s in shell.list_saves(ProjectSettings.globalize_path(SAVE_DIR)):
		any = true
		var p: String = s.path
		button(String(s.title), func():
			if shell.load(p):
				started = true
				show_title("LOADED " + shell.get_codename())
			else:
				show_title(shell.get_error()))
	if not any:
		info.text += "\n(no saves yet)"
	button("BACK", show_title)


# --- the briefing (m_brief: three pages of TIMs per level), then the level

func briefing(slot: int, level: int) -> void:
	pending_level = {"slot": slot, "level": level}
	var where: Dictionary = shell.begin_level(slot, level)
	pages = shell.load_briefing(data, int(where.mission), int(where.level))
	page = 0
	if pages.is_empty():
		play()
		return
	clear("", "")
	page_box.visible = true
	show_page()
	if auto > 0:
		print("Valor shell: briefing of %d:%d, %d pages" % [int(where.mission), int(where.level), pages.size()])
		call_deferred("play")


func show_page() -> void:
	page_view.texture = ImageTexture.create_from_image(pages[page])


func _unhandled_input(event: InputEvent) -> void:
	if not visible or not page_box.visible:
		return
	if event.is_action_pressed("ui_accept") or (event is InputEventMouseButton and event.pressed):
		page += 1
		if page >= pages.size():
			play()
		else:
			show_page()
		get_viewport().set_input_as_handled()
	elif event.is_action_pressed("ui_cancel"):
		play()
		get_viewport().set_input_as_handled()


func play() -> void:
	visible = false
	page_box.visible = false
	playing = true
	committed = false
	main.play_level(shell.get_game())


# --- after the level: the results (m_pmeval) and the post-mission menu (m_postmis)

func level_over(res: Dictionary, game_after: PackedByteArray) -> void:
	playing = false
	results = res
	var exit := int(res.get("exit", 1))
	if exit != 0 and exit != 3:
		show_title("RETURNING TO BASE")
		return
	# the level's own carried data (its rows) comes back as the engine had it at the end
	shell.set_game(game_after)
	var st: PackedInt32Array = res.stats
	var shots := st[0]
	var hits := st[1]
	var acc := 0 if shots == 0 else hits * 100 / shots
	var health := float(res.health) / 65536.0 / 128.0 * 100.0  # 128.0 is full (shell.md)
	var lines := [
		"OBJECTIVES  %d / %d%s" % [int(res.done), int(res.objectives), "" if bool(res.primary_complete) else "  (primary incomplete)"],
		"ENEMIES NEUTRALIZED  %d" % st[4],
		"SHOTS FIRED  %d     HITS  %d     ACCURACY  %d%%" % [shots, hits, acc],
		"HEAD SHOTS  %d     TORSO  %d     LIMBS  %d" % [st[6], st[7], st[8] + st[9]],
		"TIMES WOUNDED  %d     HEALTH  %d%%" % [st[2], int(health)],
	]
	var stars := ["NO STARS", "ONE STAR", "TWO STARS"]
	lines.append("RATING  " + stars[clampi(shell.rate(res), 0, 2)])
	clear("MISSION EVALUATION", "\n".join(lines))
	button("CONTINUE", post_continue, bool(res.primary_complete))
	button("REPLAY", post_replay)
	button("SAVE", post_save)
	button("WAR ROOM", post_war_room)
	if auto > 0:
		var now: Dictionary = main.game.get_state()
		print("Valor shell: ended at tick %d at (%d, %d, %d)" % [int(now.get("tick", 0)), int(now.get("x", 0)), int(now.get("y", 0)), int(now.get("z", 0))])
		print("Valor shell: " + "; ".join(lines))
		auto -= 1
		if auto > 0:
			call_deferred("post_continue")
		else:
			post_save()
			print("Valor shell: " + info.text.replace("\n", "; "))
			for sv in shell.list_saves(ProjectSettings.globalize_path(SAVE_DIR)):
				print("Valor shell: save " + String(sv.title))
			get_tree().quit()


func post_continue() -> void:
	commit(true)
	var n: Dictionary = shell.get_next()
	briefing(int(n.slot), int(n.level))


func post_replay() -> void:
	commit(false)
	briefing(int(pending_level.slot), int(pending_level.level))


func post_save() -> void:
	commit(true)
	save_game()


func post_war_room() -> void:
	if bool(results.primary_complete):
		commit(true)
	show_title(info.text)


## progress_commit once per level: the rating, the medals newly earned, shown on the title's line.
func commit(advance: bool) -> void:
	if committed:
		return
	committed = true
	var r: Dictionary = shell.commit(results, advance)
	var text := ""
	var medals := int(r.medals)
	for bit in MEDALS:
		if medals & bit:
			text += "AWARDED: " + MEDALS[bit] + "\n"
	info.text = text.strip_edges()
	if not text.is_empty():
		print("Valor shell: " + text.strip_edges().replace("\n", "; "))
