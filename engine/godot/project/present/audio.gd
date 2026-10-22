extends Node
## Sound and music (docs/re/audio.md). The level's banks are decoded from its TAF, the music from
## its VB. Every sound comes from the engine's own voice table (g_voices, through the accessor
## layer: ValorGame.get_engine_voices): each of the 23 effect voices the sound driver keeps is a
## player here, started when the engine starts it, stopped when it releases it, and placed every
## frame as sound_pan places it: the source turned into the listener's view (the scaled view
## matrix: right 2.0, up 1.25, forward 2.0), the distance class's linear falloff, the near/far side
## gains (nearly centred in front), the voice's volume. Non-positional voices keep the flat level.
## The music starts when the level script starts it (level_set(1, 1)) and loops.
##
## Without the engine (a walk) there is no voice table: only the music plays, from the start.

const VOICES := 24  # voice 0 is the music's
const S := 168  # the effects setting (g_params+0xA = 4 x 42, the default)
const FALLOFF := [6912, 7936, 8960, 9984]  # R per distance class
const ANGLE := TAU / 4096.0

var p
var players := []  # per voice index: AudioStreamPlayer (index 0 unused)
var panners := []
var started := []  # per voice: [bank, sound, handle] of the sound playing
var last_age := PackedInt32Array()
var music: AudioStreamPlayer


func setup(present) -> void:
	p = present
	for i in VOICES:
		var bus := AudioServer.bus_count
		AudioServer.add_bus(bus)
		AudioServer.set_bus_name(bus, "Voice%d" % i)
		var pan := AudioEffectPanner.new()
		AudioServer.add_bus_effect(bus, pan)
		var v := AudioStreamPlayer.new()
		v.bus = "Voice%d" % i
		add_child(v)
		players.append(v)
		panners.append(pan)
		started.append([])
	last_age.resize(VOICES)
	if not p.args.has("no-music"):
		var stream: AudioStreamWAV = p.assets.get_music(p.args.has("ambience-only"))
		if stream:
			music = AudioStreamPlayer.new()
			music.stream = stream
			# the music voice at the default setting (4: 0x3000 of 0x3FFF)
			music.volume_db = linear_to_db(0x3000 / float(0x3FFF))
			add_child(music)
			if not p.state.has("music"):
				music.play()


func on_tick() -> void:
	if music and not music.playing and p.state.get("music", false):
		music.play()
	if not p.game.is_engine_running():
		return
	for v in p.game.get_engine_voices():
		var i: int = v.voice
		var on: bool = int(v.status) & 1
		var player: AudioStreamPlayer = players[i]
		var key := [int(v.bank), int(v.sound), int(v.handle)]
		if on and (started[i] != key or int(v.age) < last_age[i] or not player.playing and int(v.age) <= 1):
			var stream: AudioStreamWAV = p.assets.get_sound(int(v.bank), int(v.sound))
			if stream:
				player.stream = stream
				player.pitch_scale = maxf(float(v.pitch) / 65536.0, 0.01)
				player.play()
			if p.args.has("audio-log"):
				print("Valor sound: tick %d voice %d bank %d sound %d at %s flags %d vol %d pitch %x" % [
					int(p.state.get("tick", 0)), i, v.bank, v.sound, v.pos, v.flags, int(v.volume) >> 16, v.pitch])
			started[i] = key
		elif not on and int(v.status) == 0 and player.playing and started[i] != []:
			player.stop()
			started[i] = []
		last_age[i] = int(v.age)
		if player.playing:
			place(i, v)


## sound_pan (audio.md, 3D): the two sides' levels (0..0x3FFF) of a voice, applied as a volume and a
## pan.
func place(i: int, v: Dictionary) -> void:
	var vol := (int(v.volume) >> 16) & 0xFF
	var left: float
	var right: float
	if int(v.flags) & 1:
		var side := float(((int(v.volume) >> 10) * S) >> 8)
		left = side
		right = side
	else:
		var lr := pan(v.pos, int(v.cls))
		left = float((int(lr.x) * vol) >> 12) * 64.0
		right = float((int(lr.y) * vol) >> 12) * 64.0
		if int(v.fade_out) > 0:
			pass  # the fade's volume steps happen on the SPU side; the release ends it
	var level := maxf(left, right) / float(0x3FFF)
	players[i].volume_db = linear_to_db(maxf(level, 0.00001))
	panners[i].pan = 0.0 if left + right <= 0.0 else clampf((right - left) / maxf(left, right), -1.0, 1.0)


## The gain and the two sides for a source at `pos` (game units) from the listener: the player's
## soldier, turned by the view's yaw and pitch.
func pan(pos: Vector3, cls: int) -> Vector2i:
	var st: Dictionary = p.state
	var off := Vector3(pos.x - float(st.get("x", 0.0)), pos.y - float(st.get("y", 0.0)), pos.z - float(st.get("z", 0.0)))
	var yaw: float = p.view_yaw() * ANGLE
	var pitch: float = p.view_pitch() * ANGLE
	# view space: x right, y up, z forward (y down in the game), scaled as the view matrix is
	var fx := Vector3(cos(yaw), 0, -sin(yaw))
	var fz := Vector3(sin(yaw) * cos(pitch), -sin(pitch), cos(yaw) * cos(pitch))  # y down
	var fy := fz.cross(fx)
	var x := int(off.dot(fx) * 2.0)
	var y := int(off.dot(fy) * 1.25)
	var z := int(off.dot(fz) * 2.0)
	var a := [absi(x), absi(y), absi(z)]
	a.sort()
	var d: int = a[2] + (a[0] + a[1]) / 4
	var r: int = FALLOFF[clampi(cls, 0, 3)]
	var g := maxi(0, 4096 - (maxi(d, 256) - 256) * 4096 / r)
	g = (g * S) >> 7
	if z > 400:
		x = x / (absi(z) - 400)
	var ax := absi(x)
	var near := g
	var far := 0
	if ax <= 1024:
		near = (g * (0x800 + 2 * ax)) >> 12
		far = (g * (0x800 - 2 * ax)) >> 12
	return Vector2i(far, near) if x > 0 else Vector2i(near, far)
