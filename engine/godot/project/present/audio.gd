extends Node
## Sound and music (docs/re/audio.md). The level's banks are decoded from its TAF, the music from
## its VB; sounds play on 23 effect voices taken first-free with no stealing (a sound finding none
## is dropped), at the pitch and volume the game's calls give, ± their random spread.
##
## Played from the core's state changes (present.gd derive_events): the player's fire, draw,
## reload, health and ammo pickups, the hurt grunt and hit, shell casings landing. The music
## starts with the level (the level script's start, level_set(1, 1), isn't reported yet) and loops.
## Stubbed until the core reports events: every other entity's sounds (enemy shots, impacts,
## explosions, voices, scripts), so positional 3D sound (sound_pan) has nothing to place yet.

const VOICES := 23
# The SPU's level at volume 255, centred, the default effects setting (0x29C0 of 0x3FFF), and
# the music voice's default (setting 4: 0x3000).
const EFFECT_GAIN := 0x29C0 / float(0x3FFF)
const MUSIC_GAIN := 0x3000 / float(0x3FFF)

var p
var voices := []
var music: AudioStreamPlayer
var casings := []  # ticks left until a casing lands


func setup(present) -> void:
	p = present
	for i in VOICES:
		var v := AudioStreamPlayer.new()
		add_child(v)
		voices.append(v)
	if not p.args.has("no-music"):
		var stream: AudioStreamWAV = p.assets.get_music(p.args.has("ambience-only"))
		if stream:
			music = AudioStreamPlayer.new()
			music.stream = stream
			music.volume_db = linear_to_db(MUSIC_GAIN)
			add_child(music)
			music.play()


## sound_play: bank, sound, volume (0x10000 = 128/255, at most 255), pitch (0x10000 = the
## sample's rate) and its spread (16.16, uniform within +-).
func play(bank: int, sound: int, volume: int, pitch := 0x10000, spread := 0) -> void:
	if bank < 0 or sound < 0:
		return
	var stream: AudioStreamWAV = p.assets.get_sound(bank, sound)
	if stream == null:
		return
	for v in voices:
		if not v.playing:
			var pf := pitch / 65536.0
			if spread:
				pf *= 1.0 + randf_range(-1.0, 1.0) * spread / 65536.0
			v.stream = stream
			v.pitch_scale = maxf(pf, 0.01)
			v.volume_db = linear_to_db(maxf(mini(volume >> 9, 255) / 255.0 * EFFECT_GAIN, 0.0001))
			v.play()
			return


## A weapon's sound (+0x14 fire, +0x18 draw, +0x1C holster, +0x20 reload: sound, then bank; bank -1
## is the held weapon model's +0xC8).
func weapon_sound(w: int, field: int) -> Array:
	var row: PackedByteArray = p.assets.level_bin(0x80093640 + 0x44 * w, 0x44)
	var sound := row.decode_s16(field)
	var bank := row.decode_s16(field + 2)
	if bank == -1:
		bank = p.assets.get_model_bank(p.weapon_model(w))
	return [bank, sound]


func on_tick() -> void:
	for e in p.events:
		match e.kind:
			"fire":
				var fs := weapon_sound(e.weapon, 0x14)
				var thrown: bool = e.weapon in [2, 3, 10]
				play(fs[0], fs[1], 0x20000 if (e.weapon in [5, 11] or thrown) else 0x17FFF, 0x10000, 0x170A)
				var flags: int = p.assets.level_bin(0x80093640 + 0x44 * e.weapon + 0x2A, 2).decode_u16(0)
				# casing_spawn: 49% of shots, landing 8..23 frames later, at most 5 pending
				if not thrown and not (flags & 0x10) and randf() < 0.49 and casings.size() < 5:
					casings.append(randi_range(8, 23))
			"draw":
				var ds := weapon_sound(e.weapon, 0x18)
				play(ds[0], ds[1], 0x10000, 0x8000, 0x3333)
			"reload":
				var rs := weapon_sound(e.weapon, 0x20)
				play(rs[0], rs[1], 0x20000, 0x8000)
			"ammo":
				play(0, 0x20, 0x10000)
				var ams := weapon_sound(e.weapon, 0x20)
				play(ams[0], ams[1], 0x10000, 0x8000)
			"health":
				play(0, 0x1F if e.pack == 0 else 0x1E, 0x20000)
			"hurt":
				# player_hit_part: 30% a grunt, and the hit itself
				if randf() < 0.3:
					play(0, 0x1B + (randi() & 1), 0x20000, 0x8000, 0xF5C)
				play(0, 0x0C, 0x20000, 0x10000, 0x4CCC)
	var left := []
	for c in casings:
		if c <= 1:
			play(0, 0x10 + (randi() & 1), 0xCCCC, 0x14000, 0x1999)
		else:
			left.append(c - 1)
	casings = left
