# The HUD and screen effects: what they show, from which state

Everything the game draws over the 3D view in a level, as the game state behind it, so a new
engine can draw its own HUD with the same information and timings. Screen coordinates are the
game's 512×240 frame (x is drawn at twice the scale of y: circles are ellipses 2:1 in these
numbers). Ticks are 30 Hz game ticks (entities.md).

Read from the code (`fn.py show`/`c` on LEVEL.BIN), checked against RAM dumps of the 1:3
replay (`build/sh2/hud_9000.bin`: `--dump-ram 9000`), screenshots of the same replay
(`--screenshots DIR --screenshot-every 400`, and every 4 frames around the level start), and
`VALOR_WATCH` on the hit timers, health and text count. "Inference" marks what isn't checked.

`tools/recon/sh2_hud.py RAMDUMP` prints all of the state below from a dump.

## What is on screen

The level's HUD is small:

| Element | Where | Shows | Drawn by |
|---|---|---|---|
| Compass rose | top left, centred near (59, 44), about 64 px across | the view's yaw | `hud_draw` (`0x80060AFC`) |
| Health ring | around the compass: radii 14..20 (×2 in x), centre (60, 44) | health, 32 segments | `hud_draw` |
| Hit wedges | inside the ring, four quarters | the direction of the last bullet hits | `hud_draw` |
| Ammo | top right: clip digits at (380, 18), an icon at x 415..447, y 15..35, reserve digits at (453, 18) | the current slot's clip and reserve | texts + `hud_overlay_draw` (`0x8008019C`) |
| Messages | left, x 20 (pickups x 10) | level messages, pickups | the text list (`texts_draw`, `0x8005F86C`) |
| Aim reticle | at the crosshair | in aim mode only | `crosshair_draw` (`0x8007CAF8`) |
| Scope | full screen | sniper scope | `scope_draw` (`0x8007B534`) |
| Fades | full screen | level start, death | fade entities (type 0x32) |

There is **no** crosshair outside aim mode (combat.md), no objective marker on the compass, no
on-screen objective list (the objectives are listed only in the pause menu), no weapon name,
no full-screen damage flash and no lives counter in the level.

Order of drawing, per frame (`render_pass`, `0x800503D4`): `draw_level` (`0x80053C20`) first
calls `hud_draw`, then draws the world; then `texts_draw` (texts), `entities_draw` (the player's
`player_draw` adds the weapon model, the reticle or the scope), then `hud_overlay_draw` (the
ammo icon and the pause menu's panels). `hud_draw` does nothing while paused (`g_paused`,
`0x8009A5E0`); the texts and the ammo icon are still drawn. The HUD stays up after death (the
ring empty) until the fade.

## Health ring

State: the soldier's health (`soldier+0xF4`, 16.16, 0..128.0; player.md). Each frame
`lit = (health >> 16) >> 2`, 0..32: **one segment per 4 points**, rounded down (3.9 health
shows nothing).

The ring is 32 segments of 11.25° (`0x80` angle units), `hud_init` (`0x800606E0`): segment i
spans angles i·0x80..(i+1)·0x80 clockwise from the top, outer radius 20, inner 14:
`x = 60 + 2·r·sin(a)`, `y = 44 − r·cos(a)`. Segments 0..31−lit (from the top, clockwise) are
drawn almost black (1, 1, 1); the rest in a fixed colour per segment:

| Segment i | Colour (R, G, B) |
|---|---|
| 0..16 | (5·i, 64 + i, 0): dark green at the top, to olive-yellow (80, 80, 0) at the bottom |
| 17..31 | (80 − (i−16), 80 − 5·(i−16), 0): to dark red (65, 5, 0) left of the top |

So losing health darkens the ring from the top going clockwise, eating the green end first;
the last segments left are the red ones just left of the top. (Seen: at 119 health three
segments right of the top are dark, frame 4000 of the replay.) Prims: 32 flat quads at
`0x800AB970` (+0xC00 per buffer); only every other one is linked, with a length of 11 words
that makes the GPU draw the next one too (a packing trick, not a gap).

Health changes: `apply_damage` (combat.md), the health pickups (player.md: 13, 32, 64), and
nothing else; there is no regeneration.

## Hit wedges (direction of damage)

State: four timers, `g_hit_timer` (`0x8009A590` right, `0x8009A594` left, `0x8009A598` front,
`0x8009A59C` behind; s32 each).

**Set** by `hit_indicator(victim, attacker)` (`0x80060FC4`), called from `apply_damage` when
the victim is the player (flags A bit 0), for every damage event, before the death check is
acted on:

1. `v` = the attacker's position at the previous tick minus its position now
   (`+0x258 − +0x20C`, >> 16). For a bullet (the usual attacker) that points back along its
   flight, toward the shooter. For an attacker that didn't move (an explosion entity, a melee
   soldier standing still) `v` is 0 and no wedge lights.
2. `v` is turned into view space: `RotMatrixYXZ(−soldier+0x22A, −yaw, −soldier+0x232)`
   (yaw = `inv+0x186`) and applied with the GTE (x right, z forward).
3. If |x| > |z|: x < 0 sets the **left** timer, x > 0 the **right**; otherwise z > 0 sets
   **front**, z < 0 **behind**. The timer is set to **40** (both of a pair can't light from one
   hit; exact ties with x = z = 0 light nothing).

**Shown** while the timer is non-zero: a dark red (64, 0, 0) quarter wedge from the compass's
centre to radius 20, covering 90° (front: the top quarter, −45°..45°; right: 45°..135°;
behind: the bottom; left: the left), drawn over the compass. Each drawn frame the timer drops
by 1 + `g_catchup_ticks` (`0x8009A5D4`), i.e. by one per tick. From 40 to 25 the wedge is
opaque; below 25 it is drawn semi-transparent (the prim's semi-transparency bit; the blend mode
is the draw mode's). Total
40 ticks (1.33 s).

Checked: `VALOR_WATCH` on the timers and health in the replay: each health drop
(e.g. 128 → 121 at vblank 3320) set `0x8009A598` (front) to 0x28 in the same vblank, and it fell
by one every 2 vblanks (with the occasional catch-up step of 2) to 0 by vblank 3399.

## Compass

State: only the view yaw, `inv+0x186` (the high half of `inv+0x184`, movement.md; 4096 = 360°,
taken mod 4096). The compass **doesn't point at objectives** or anything in the level: it is a
textured disc (a compass rose with letters, texture `tpage 0xF`, u 0x80..0xBF, v 0x60..0x9F,
CLUT 0x3E3E, from the level's textures; semi-transparent) rotated with the view.

`compass_place` (`0x80060924`), every frame: `RotMatrixZ(−yaw)` turns three points
(`0x8009A5A0`: (0, 0, 320), (23, −23, 320), (23, 23, 320)), projected with H 400; the quad's
corners are the centre ± the two projected offsets, x doubled. On screen the rose spans ±28.75 in x and ±14.4 in y
round its centre (23 · 400 / 320 = 28.75 for x, half that for y): it fits inside the health
ring's inner radius (14, x doubled), as a recomp frame shows; the native HUD
(`engine/godot/project/present/hud.gd`) draws it so. (Inference: the projection's y scale is half
its x scale here; the GTE's settings in `compass_place` weren't followed.) The result (checked in the dump
at yaw 1847, and by the formula at yaw 0): **the texture's top edge is drawn at screen angle
90° − yaw** (clockwise from up). At yaw 0 the rose's top points right; turning so that yaw
grows turns the rose counter-clockwise, keeping its marks on fixed world directions. Which
world axis the "N" of the texture is on wasn't measured (inference: the rose's top is the
direction 90° clockwise of yaw 0's facing).

A new engine needs: the yaw, and a fixed world direction for north.

## Ammo

State: the current slot `inv+0x54`, its clip `inv+0x66 + 2·slot` and reserve
`inv+0x56 + 2·slot` (player.md).

- **Digits** are two permanent entries of the text list (below), made at level start
  (`ammo_hud_init`, `0x80046874`): the clip at (380, 18), 2 characters, the reserve at
  (453, 18), 3 characters, colour (100, 100, 100), opaque, lifetime −1 (forever). Their strings
  live in the soldier's weapon block (`inv+0x108`, `inv+0x10C`) and are rewritten by
  `ammo_hud_update` (`0x800467EC`) after every change: firing, throwing, reloading, an ammo
  pickup, drawing a weapon. Format (`ammo_digits`, `0x80046700`): the clip right-aligned in 2
  (space-padded, e.g. " 5"), the reserve in 3 (" 29", "120"). Weapon 7 (the papers, 2:x):
  both strings empty, so nothing shows.
- **Icon**: a 32×20 textured quad at x 415..447, y 15..35 (`inv+0xB8`, two buffers of 0x28
  bytes), drawn every frame by `hud_overlay_draw`. Its texture is picked by the **slot**, not
  the weapon (`ammo_hud_icon`, `0x80046574`, on every weapon draw): slot 0 (and 4+) u 0x80, v
  0xA0, CLUT 0x3DFE; slot 1 u 0xA0, v 0xA0, CLUT 0x3DFF; slot 2 u 0xC0, v 0xA0, CLUT 0x3FFF;
  slot 3 u 0xA0, v 0xC0, CLUT 0x3E7F (inference: each level's textures hold its weapons' ammo
  pictures in slot order).

What a new engine needs: per slot, the clip and reserve numbers and an ammo picture; hide both
for the papers. (A reload empties nothing visible: the digits jump when the reload is done,
player.md.)

## Messages: the text list

All the strings drawn in a level go through one list, `g_texts` (`0x80099F4C`, 50 entries of
32 bytes, count at `0x80099ED0`): added by `text_add` (`0x8005F28C`), aged by `texts_update`
(`0x8005F69C`, every tick after the collisions, entities.md), drawn by `texts_draw`
(`0x8005F86C`, every frame), cleared at level start (`texts_clear`, `0x8005F260`). A full list
refuses new texts (−1).

| Offset | Type | Meaning |
|---|---|---|
| +0x00 | char* | The string (not copied: the ammo strings change in place) |
| +0x04, +0x06 | s16 | x, y of the first glyph |
| +0x08..+0x0A | u8 ×3 | Colour now (R, G, B) |
| +0x0B | u8 | 1: semi-transparent (additive) |
| +0x0C | s32 | Lifetime in ticks; −1 forever. Removed when it reaches 0 (the last entry moves into its place) |
| +0x10 | s32 | Hold: −2 (plain texts) no colour change; > 0 counts down, fading in meanwhile; 0: fading out |
| +0x14 | s32 | Fade-in ticks left (0x7FFF: endless) |
| +0x18 | s32 | Viewport mask: bit n drawn in player n's view (1 in one-player) |
| +0x1C..+0x1E | u8 ×3 | Fade step per tick (R, G, B) |
| +0x1F | u8 | Id: texts with an id can be removed together (`texts_remove_id`, `0x8005F60C`); 0xFE asks for a random unused id 120..247 |

`text_add(text, x, y, r, g, b, semi, ticks, id, mask, &id_out)` (names.txt's "scale" for the
fourth argument is wrong: it is red). `text_add_fade` (`0x8005F3E8`) adds the hold, the
fade-in length (negative: worked out from a target) and the steps. Each tick, for a text whose
hold is ≥ 0: hold > 0: hold −1 and, while fade-in > 0, colour += step (capped at 255) and
fade-in −1; hold = 0: colour −= step (floored at 0). On additive texts colour 0 is invisible,
so this is a fade in and out.

**Drawing**: 10×7-pixel glyphs (sprites) every 12 pixels, uppercase font in VRAM page
(960, 0), CLUT (1008, 250); the glyph's u, v from two tables indexed by the character
(`0x80099EB3 + c`, `0x80099EEF + c`); a space skips 12 pixels. Semi-transparent texts use
additive blending.

Who adds texts:

| Text | Position | Colour | Life | How it changes |
|---|---|---|---|---|
| Level messages: built-in 8 `show_text(n)` (`0x80053464`, levels.md), the message table `0x80098188` (objective done, "ATTACHE CASE ACQUIRED", ...) | x 20; y from `g_msg_line_y` (`0x80098628`) | (128, 128, 128), additive | 100 ticks (3.3 s), then gone at once | A message different from the last one (`0x8009862C`) moves down a line: y += 12, and from 168 back to 120; the same message again reuses the line. The line is set to 120 when the level's scripts are set up (`0x80066110`), so the first message is at 132 and the lines cycle 132, 144, 156, 120 |
| Health pickup: "MEDICINAL CANTEEN" (kind 0, +13), "FIRST AID KIT" (1, +32), "FIELD SURGEON PACK" (2, +64) (`0x8009E864..`) | (10, 70) | additive, from 0 up by (10, 8, 4) a tick for 11 ticks: (110, 88, 44), amber | 40 ticks (1.33 s) | id 2: a new pickup message first removes the previous one |
| Ammo pickup: the weapon's ammo name ("RIFLE ROUNDS", player.md) | (10, 70) | as above | 40 ticks | id 2, as above |
| Ammo digits | above | (100, 100, 100), opaque | forever | rewritten in place |
| Pause menu (objectives with a box and a tick: "$", "#"; options) | | | | the pause menu's own, ids 1 |

In 1:3's dump at vblank 9000 the list held only the two ammo strings; the message line was at
144 after message 227.

## Aim reticle

State: aim mode (`inv` flag 0x20), the stance (`inv+0x1C8`, movement.md), the crosshair's
screen position (`inv+0x190`, `+0x192`: 256, 131 at rest; movement.md and combat.md for how it
moves), the aim point (`inv+0x1A4..+0x1AC`, combat.md).

`crosshair_draw` (`0x8007CAF8`, from `player_draw` unless scoped) draws only in aim mode and
not in stance 6 (grenades, papers). Four red (128, 32, 32) lines, near the front of the
ordering table:

| Stance | Centre | Shape |
|---|---|---|
| 0 (pistols, shotgun, Garand), `inv` flags 0x01800000 clear | the aim point projected to the screen (`reticle_at_aim_point`, `0x8007C71C`); the difference to the crosshair is kept in `inv+0x194/+0x196` | a broken cross: vertical ticks y−12..y−5 and y+5..y+12, horizontal x−18..x−8 and x+8..x+18 |
| 0 with flag 0x800000 or 0x1000000 | the crosshair + the last kept difference | the same |
| 1 (SMGs), others | the crosshair | the same |
| 5 (bazooka) | the crosshair | a vertical line y−8..y+6, a bar x−24..x+24 at y−18, and two slants (x−32, y−12)→(x−8, y+16) and (x+32, y−12)→(x+8, y+16) |

Flag 0x800000 is set by `player_update` while the soldier's animation (`+0x11E`) is 2, 9, 10,
13, 16, or 8 with the refire countdown running (inference: firing and reloading animations);
0x1000000 wasn't traced. Since the aim point lies on the crosshair's ray, the projected centre
is the crosshair give or take a tick: a new engine can draw the reticle at the crosshair.

## Sniper scope and zoom

State: `inv` flag 0x100 (scope; sniper rifle in aim mode, movement.md), the projection distance
`inv+0xA4` (400 normal, growing to 2447 in about 47 ticks).

- The **weapon model is drawn only while `inv+0xA4` is 400** and `inv` flags 0xC000 are clear
  (`player_draw`): it disappears as soon as the zoom starts and comes back when aim mode ends
  (which resets the zoom at once).
- While flag 0x100 is set, `scope_draw` (`0x8007B534`) replaces the reticle: a model (the
  weapon block's `inv+0x130` object, its faces and per-vertex colours) drawn flat in front of
  the camera (identity rotation with the vertical scaled 0x780/0x1000, 201 units ahead) as
  Gouraud triangles: the scope's black surround (inference: the model wasn't looked at; no
  separate cross-hair lines are drawn for it).
- The zoom itself is the projection distance (movement.md); the scope's turning is ÷8.

## Camera shake

State: the camera object's `+0xCC` (amplitude, s16) and `+0xCE` (ticks left), set by
`camera_shake(camera, amplitude, ticks)` (`0x80046C50`; a new shake replaces the old). Each tick
in `camera_think`, after placing the camera: the camera's y, its eye point and its look point
all move by `amplitude` (world units, y down), then `amplitude = −(amplitude >> 1)`, ticks −1.
The view jumps vertically and settles in a few ticks (20 → −10, 5, −3, 1, −1, 0 ...); the
offset isn't cumulative since the camera is re-placed every tick (movement.md).

| Cause | Amplitude | Ticks | Code |
|---|---|---|---|
| The player hit (`player_hit_part`) | 20 | 10 | combat.md |
| An explosion within 3000 units (approximate distance d) | (3000 − d) / 16 | 10 | `explosion_think` |
| Firing the mounted gun (its user's camera) | 5 | 3 | `mounted_gun_think` |
| The 0x1774 object's bursts (objects.md) | 125, or 8 | 30, or 10 | `obj1774_think` |

## Fades

A fade is an entity of type 0x32 (`fade_start`, `0x8004F748`; think `0x8004F488`, draw
`0x8004F670`; `fade_find`, `0x8004F700`), linked at the front of the list, holding two
full-screen (512×240, or a given rectangle) textured quads drawn with **subtractive**
blending: colour 0 changes nothing, 255 blacks the screen.

`fade_start(flags, delay, duration, hold, r0, g0, b0, r1, g1, b1, rect)`: flags bit 0 draws.
For `delay` ticks nothing is drawn (with delay 0 it draws from the start). Then for `duration`
ticks a colour is stepped from (r0, g0, b0) toward (r1, g1, b1) by (end − start) / duration a
tick, **but the quads keep the start colour** until the duration is over: only then is the
colour copied to them, shown for `hold` + 1 more ticks, and the entity removed. So on screen a
"fade" is a **cut**: start colour for `duration` ticks, then the end colour briefly.

Checked at the level start (the screenshots every 4 frames around 1:3's start): the screen is
black, then the picture appears from one shot to the next (mean brightness 0.2 → 28.5), with
no intermediate frames.

| When | Call | On screen |
|---|---|---|
| Level start (`0x80061E14`, after loading) | (1, 0, 15, 0, 255 → 0) | black for 15 ticks (0.5 s), then the picture |
| Death with a death-camera animation, when the camera's script has ended | (1, 50, 15, 0, 0 → 255) | normal for 65 ticks, black 1 tick; when the fade entity is gone the level is lost (`0x8009C840` \|= 2; movement.md's "fade (50 ticks)" is this delay; 0x32 is also the entity type) |
| Death without one (`player_death_start`) | (1, 50, 15, 120, 0 → 255) | normal for 65 ticks, then black for 121 |
| Soldier flags A 0x100000 while dead (not identified) | (1, 0, 5, 0, 0 → 255) and (1, 5, 25, 0, 255 → 0), on the view's rectangle `0x8009A630`; `0x8009C840` \|= 0x202 | normal 5 ticks, then black about 26 ticks |

(Inference: the colour ramp was meant to show; a new engine wanting identical behaviour cuts.)
Flag 4 of a fade also ramps a value at `+0x6C` from 0x300000 to 0 (no caller passes it).

## Other things drawn over the view

- **Muzzle flash** (`0x8006DD54` on `inv+0x1D4`, from `player_draw`; combat.md).
- **Weapon model brightness**: `player_draw` moves the weapon model's `+0x6C` toward 0x40
  (2 a frame) while the soldier's flags B 0x400 are set, else back to 0x80 (inference: darker
  in dark places; who sets 0x400 wasn't traced).
- **Pause menu panels**: `menu_boxes` (`0x80096E60`, 9 entries of 0x1EC bytes, bevelled flat
  boxes made by `menu_box_add` `0x8004E798` from `start_handler`), drawn by
  `hud_overlay_draw` → `menu_boxes_draw` (`0x8004E8FC`). shell.md covers the pause menu.
- A second list of 12 screen overlays (`0x8009C844`, 0x58 bytes each, drawn by `0x80064550`,
  cleared by `0x8006450C` from `start_handler`) was empty in every dump; nothing found fills
  it (inference: unused).

(Valor's runtime comment on `hook_draw_hud` calls `0x8008019C` the HUD with the compass; it
draws the ammo icon and the menu panels. The compass, ring and wedges come from `hud_draw`
inside `draw_level`, the digits and messages from `texts_draw`.)

## For a new engine: the HUD's inputs

| Shown | State | Range / timing |
|---|---|---|
| Health ring | `soldier+0xF4` | 0..128.0; 32 steps of 4 |
| Hit direction | per hit on the player: the bullet's travel reversed, in view space | four quarters, 40 ticks each (opaque 15, then translucent 25) |
| Compass | view yaw `inv+0x184` | rose turns with −yaw |
| Clip / reserve | `inv+0x66/+0x56` of slot `inv+0x54` | 2 and 3 digits; hidden for the papers |
| Ammo picture | slot `inv+0x54` | per slot |
| Messages | level message ids, pickups | 100 ticks (4 cycling lines at x 20), pickups 40 ticks with an 11-tick fade-in |
| Reticle | aim flag 0x20, stance, crosshair `inv+0x190/+0x192` | aim mode only; bazooka shape |
| Scope | `inv` flag 0x100, zoom `inv+0xA4` | weapon hidden while zoom ≠ 400 |
| Shake | camera `+0xCC/+0xCE` | vertical, halving and flipping each tick |
| Fades | type 0x32 entities | cuts: start 15 ticks black; death 65 ticks then black |

## Open

- Which world direction the compass rose's "N" marks (measure yaw against a screenshot).
- The scope model's look, and what `inv` flag 0x1000000 and soldier flags B 0x400 are.
- Two-player split screen (LEVEL2P): the viewport masks suggest the same HUD per view; not read.

## Names

```
# --- LEVEL.BIN ---
8004E798 menu_box_add               # pause menu panel: size, colour, position (start_handler)
8004E8FC menu_boxes_draw            # (level, port): the visible panels of g_menu_boxes
8004EB98 menu_box_remove
8004EBC0 menu_box_move
8004EE0C menu_box_size
8004F21C menu_box_colour
8004F488 fade_think                 # delay, then colour ramp (not shown), then end colour for hold+1 ticks
8004F670 fade_draw
8004F700 fade_find                  # first entity of type 0x32
8004F748 fade_start                 # (flags, delay, duration, hold, r0,g0,b0, r1,g1,b1, rect): type 0x32, subtractive quads
8005F260 texts_clear
8005F3E8 text_add_fade              # text_add + hold, fade-in length, per-tick colour steps
8005F5A0 text_remove                # (index): last entry moves into its place
8005F60C texts_remove_id            # (id)
8005F86C texts_draw                 # (ot, prims, port): 10x7 glyphs every 12 px
80060924 compass_place              # RotMatrixZ(-yaw) on 0x8009A5A0's points -> the rose quad
800604D0 compass_init
80060548 hit_wedges_init            # four quarter wedges, (64,0,0)
800606E0 hud_init                   # health ring (32 segments, r 14..20 around (60,44)), wedges, compass
80060AFC hud_draw                   # from draw_level: ring from health, hit wedges and their timers, compass
80060FC4 hit_indicator              # (player, attacker): attacker's back-step in view space -> a 40-tick wedge
80061130 ammo_hud_init_player       # ammo_hud_init(g_soldier)
80064550 overlays_draw              # 12-entry list at 0x8009C844 (empty)
8006450C overlays_clear
80046574 ammo_hud_icon              # icon texture by slot, then ammo_hud_update
80046700 ammo_digits                # (buf, value, pos, width 2|3): space-padded digits
800467EC ammo_hud_update            # inv+0x108 clip, +0x10C reserve strings; empty for the papers
80046874 ammo_hud_init              # icon quad (415..447, 15..35) and the two permanent digit texts
8007B534 scope_draw                 # the scope model flat in front of the camera
8007C71C reticle_at_aim_point       # stance 0: the aim point projected; inv+0x194/+0x196 = it - crosshair
8007CAF8 crosshair_draw             # aim mode only: red broken cross; bazooka shape in stance 5
8008019C hud_overlay_draw           # menu panels, ammo icon (inv+0xB8)
80096E60 g_menu_boxes               # 9 x 0x1EC
80098628 g_msg_line_y               # show_text's line: 120..156, +12 per new message
8009862C g_msg_last                 # last message shown by show_text
80099ED0 g_text_count
80099F4C g_texts                    # 50 x 32 bytes
8009A590 g_hit_timer                # 4 s32: right, left, front, behind; 40 ticks
8009A5A0 g_compass_points           # 3 x SVECTOR: (0,0,320), (23,-23,320), (23,23,320)
8009C844 g_overlays                 # 12 x 0x58 (unused)
800AB970 g_health_ring_prims        # 32 POLY_F4 per buffer (+0xC00)
800AD170 g_compass_prims            # POLY_FT4 x 2 buffers
800AD1C0 g_hit_wedge_prims          # 4 POLY_F3 per buffer (+0x50): right, behind, left, front
# from shell.md (the level loop, pause, death, title, DWI gallery, multiplayer drops)
8009C840 g_level_flags              # 0x02 died (restart), 0x04 finished, 0x20 leave, 0x40/0x80 quit; 0x01/0x08/0x10 never set in retail
800640B4 level_main                 # load once, start/restart, frame loop until an end flag
8006240C level_load_all             # "InitLevel()": level_load, clears flags 4 and 0x100
80061E14 level_start                # entities respawned, player reset, music, fade in; clears flag 2
800624AC level_restart              # after a death: level_start
80061D08 player_reset               # inventory, stats, health 128.0 or the row's (g_game+0x74, CAPTAINDYE)
80061A2C player_reset_inventory     # reserves from g_weapons[].+6 (max with cheat 0x40), clips 0, draw
8006192C stats_reset                # 0x80039CA8..0x80039D10, g_weapons[].+0x30
80061158 debug_flags_hook           # empty (stripped): gets &g_level_flags at level start
8006D600 entity_find_type_group     # (type, +0x35 group): first live one
8009A5E4 g_pause_confirm            # QUIT -> ARE YOU SURE? shown
8009A5EC g_pause_confirm_no         # 1 NO selected (default), 0 YES
8009A5F4 g_pause_continue           # 1 CONTINUE selected, 0 QUIT
8009A600 g_pad_missing              # controller 1 out: pauses
# --- LEVEL2P.BIN ---
80074A2C mp_player_death            # drops a pickup at the body (mp_drop_spawn)
80076118 mp_pad_config              # as player_pad_config; Up+Triangle+shoulders drops a pickup (debug)
8008175C mp_drop_spawn              # PLAYER AMMO with the dead player's ammo, or a random power-up (DENNISMODE)
8009AF38 g_mp_drops                 # 14 x 0x34: model hash, ..., name[32], colour
# --- SHELL.BIN ---
80058CC8 title_enter                # title screen's +0x5C handler; clears g_dev_menu
8005A430 title_read                 # title input -> event 0/2/3/4, 10 after 960 idle frames (demo)
8006D050 dwi_page_load              # data\scr3\mdwiemp|mdwiteam\a\b.rsc
8006D3C4 dwi_input                  # next page; past the last, m_title
8006FCC4 dev_screen_init            # m_dev (unreachable): data\scr2\mdev.rsc
8007B67F g_dwi_pending              # set by the DWI passwords: the Enigma screen goes to m_dwiemp
8007BBB8 g_dev_menu                 # set only by title event 5, which nothing produces
8007BBD0 g_title_idle               # frames until the demo film (960)
8007E870 g_dwi_page
8007E874 g_dwi_set                  # 0 DWIGALLERY, 1 COOLCHICK, 2 AJRULES, 3 DWIMOHTEAM
```
