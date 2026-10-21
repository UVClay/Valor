# Asset formats: what is on the disc, and how the game reads it

A map of every file type on the disc, for loading the original data into a new engine. Layouts
come from the game's readers (`LEVEL.BIN` unless noted). They were checked in three ways: against
all copies of each file type on the disc (`tools/recon/fmt_*.py`), against RAM dumps of 1:3
(`build/re/mid_*.bin`), and against the GPL-3 viewer AdrianoDiDio/Medal-Of-Honor-PSX-File-Viewer
(`build/ext/mohviewer`, commit 50ab3d0; "the viewer" below). The viewer's `docs/README.md`
documents TSP, BSD, RSC, TAF, TIM, SST, GFX and STR, and its sources read and export them. This
document links to it and doesn't repeat what it gets right: it adds what the game does with each
field and corrects the viewer where the code disagrees. Claims marked *inferred* weren't
confirmed in code.

All values are little-endian. Offsets inside a BSD are **body** offsets: file offset − 0x800
(`g_level` points at the body).

## The disc

| Files | Count | What | Read by | Covered |
|---|---|---|---|---|
| `SLUS_009.74`, `SHELL.BIN`, `LEVEL.BIN`, `LEVEL2P.BIN` | 4 | The exe and its overlays | the exe | RECON.md |
| `DATA\MSNm\LVLl\m_l.BSD` | 31 | The level: header, models, skeletons, entity records, scripts, zones | `level_load` | below, levels.md |
| `DATA\TWOPLAYR\PLAYERS\x\*_P1/_P2.BSD`, `WEAPONS\SETn.BSD` | 63 | Split-screen characters and weapon sets: a BSD with only models and the shared tables | `LEVEL2P.BIN` (`%s%s_p1.BSD`) | below (BSD) |
| `DATA\MSNm\LVLl\TSP0\m_l_cN.TSP` | 220 | World chunks: render tree, faces, vertex colours, animated faces, collision | `tsp_load` | below, levels.md (collision) |
| `DATA\MSNm\LVLl\m_l0.TAF`, `m_l1.TAF` | 62 | The level's textures (TIMs uploaded to VRAM), then sound banks (VABs) | `level_load` | below |
| `DATA\TWOPLAYR\...\*.TAF` | 63 | The same, for the split-screen BSDs | `LEVEL2P.BIN` | below |
| `DATA\MSNm\LVLl\Mm_l.VB`, `Mm_lA.VB` | 50 | Level music: headerless SPU ADPCM, streamed | `level_set(1, ...)` | below |
| `DATA\*.RSC`, `DATA\SCRn\...\*.RSC` | 182 | Front-end archives: TIMs, menu scripts (`.sst`), shell sound bank, clerk models (`.gfx`) | `SHELL.BIN` | below, the viewer |
| `MOVIE\INTRO\*.STR`, `MOVIE\INGAME\{BRIEF,GAL}\*.STR` | 22 | MDEC video with XA audio (Form 2 sectors) | `SHELL.BIN` | the viewer (via jPSXdec) |
| `VICTORY/SUCCESS/FAILURE/RETURN.TIM` | 4 | 320×384 15-bit end-of-level screens | `LEVEL.BIN` (`\victory.tim` ...) | standard TIM |
| `ZZZZ\DUMMY.DAT` | 1 | 28 MB of padding | nothing | |
| `DATA\MSN1\LVL3\TSP0\LOG.BAK` | 1 | A CD-burner log left on the master | nothing | |
| `SYSTEM.CNF` | 1 | Boot file | BIOS | RECON.md |

`LEVEL.BIN` also names `data\shell\loading.tim` and `data\misc\dwilogo.tim`, but neither file
is on the disc.

## BSD (level file)

### File start: the size table (sector 0)

| Offset | |
|---|---|
| +0x0 | N |
| +0x4 | N ascending u32 sizes |

`bsd_read` (`0x8004A374`) reads sector 0, then `bsd_alloc` (`0x8004A300`) tries to allocate
`size[N]`, then `size[N-1]`, and so on down to `size[1]` (1-based entries). It reads the body
into the first allocation that succeeds, entry `k`, and sets the model count
(`0x8009EC54`) to `k + 1`. The models' data is stored in model order at the end of the body, and
`size[k]` covers models `0..k`. When memory is short, the level therefore loses its last models
instead of failing to load. On all 94 BSDs, N is the model count − 1 and `size[N]` covers the
whole body (`fmt_bsd.py`). The viewer says this sector "can be ignored".

### The level header (body +0x0..+0x5A8)

levels.md has the entity-related fields. The full layout:

| Offset | Meaning | Reader / evidence |
|---|---|---|
| +0x000 | TSP path prefix, 0x80 bytes (`...\tsp0\1_3_c`; chunk N is `prefix + "%d.tsp"`) | `0x80053DB0`, `chunk_stream` |
| +0x080 | Number of TSP files. They are found by probing `c1, c2, ...` until that many exist | `world_create` (`0x80053E68`) |
| +0x084 | Number of chunk **slots** (4 in every level): chunks resident at once | `0x80053E68` → `g_chunk_count` |
| +0x088 | First chunk loaded at start: the slots get chunks `+0x88 .. +0x88+slots-1` | `0x80053E68` |
| +0x08C | 0 | |
| +0x090 | 0 on the disc; no reader found | |
| +0x094 | 18 in every level; no reader found | |
| +0x098 | Per-frame primitive pool size: allocated ×2, split per frame buffer | `0x80053E68`, `0x80053B8C` |
| +0x09C | 0 | |
| +0x0A0 | If non-zero: `+0xB0..+0xB7` are zeroed at load and the clear colour is *not* taken from `+0xCE`. 0 in every level | `level_load`, `0x80062358` |
| +0x0A4..+0x0CB | 0 on the disc | |
| +0x0CC | s16 **fog near** (0: no fog). `SetFogNear(+0xCC, 400)` | `level_load`, the world/model draws |
| +0x0CE..+0x0D0 | Fog colour RGB: loaded into the GTE back colour and far colour (×16), and copied into the four draw environments' clear colour | `level_load`, `0x80062358` |
| +0x0D2 | s16, always equal to `+0xCC`; no reader found | |
| +0x0D4 | s16: if non-zero, the GTE far colour is grey (128, 128, 128) instead of the fog colour. 0 on the disc | `level_load` |
| +0x0D6 | s16, 10000 in every level; no reader found | |
| +0x0D8 | Number of **animated lights** (0..40) | `lights_update` (`0x80053AC0`), the draws |
| +0x0DC | 40 light entries of 0x14 bytes (below) | |
| +0x3FC | 20 entries of 0x10 bytes: {s16 -1, 0 ...}, with +0xC/+0xE zeroed at load. Identical in every file; unchanged in RAM after play; no reader found | `level_load` |
| +0x53C | Entity records table | levels.md |
| +0x540 | AI node table | enemies.md (Nodes) |
| +0x544..+0x588 | The shared skeletal-model tables: 5 {offset, count} pairs (below) | `level_load`, `0x80086C9C..0x80086F24` |
| +0x58C..+0x58E | Sky gradient colour (bytes stored into the gradient as R, B, G in that order; the gradient's other end is half of it); 0 in levels without a sky | `0x80088980` |
| +0x58F | Star field height: stars are placed with y = −(`+0x58F` << 8) − rand(0..0x2FFF), 255 stars | `0x80088744` |
| +0x590 | **View distance**: the camera's far plane (`camera+0xC4`, and `<< 16` into the camera struct's `+0x40`); 0 → 4500 | `0x8004770C` |
| +0x594, +0x596 | s16 moon y, z ("Using Scene Y/Z value for moon"); 0: "Scene File Has No Moon" | `0x800888C4` |
| +0x598 | Zone record lists ("Property Set File"): s32 the highest zone id N (-1: none), then N + 1 entries of {u8 255 − count, u8, count × u16 record indices} | levels.md (Triggers), `zones_parse` |
| +0x59C | Level scripts | levels.md |
| +0x5A0 | Paths | enemies.md |
| +0x5A4 | Model count (the loader uses the size table's instead; they agree on the disc) | |
| +0x5A8 | Models, 0x100 bytes each | below |

`fmt_bsd.py -v` prints these fields for a level. Where these fields differ from the viewer:

- The viewer's "TSP Info" second field is the slot count, not the number of TSPs rendered at
  start.
- Its "Animated Lights Block" begins with the count at +0xD8. The entries start at +0xDC.
- Its "Sky Box" Unknown0..2 are the sky colour.
- Moon Y and Z are at +0x594 and +0x596, in that order: the game's own messages name them.
- Its Unknown3 at +0x590 is the view distance.
- Its Unknown4..6 at +0x598..+0x5A0 are the zone lists, the scripts and the paths.

### Animated lights (+0xDC, 0x14 bytes each)

| Offset | |
|---|---|
| +0x0 | Number of steps (0: unused entry) |
| +0x4 | Offset of the steps (body-relative; a pointer after load) |
| +0x8 | Current step (runtime) |
| +0xC | Current colour (runtime; 0 in the file) |
| +0x10 | Ticks left (runtime; zeroed at load) |

Each step is a u32: the low 24 bits are the colour (R in the low byte), and the top byte is how
many ticks it lasts. `lights_update` (called from the world entity's tick, `0x80053C00`) counts
down every entry with steps. At 0 it advances the step, wrapping at the count, and loads the new
colour and duration. 1:3's RAM dumps show the steps advancing.

**How a vertex uses a light.** If the level has any lights (`+0xD8` ≠ 0), the world draw
(`0x80010B60`) and the model draws (`0x8008080C` and the others listed under the model draws
below) replace a vertex colour whose **red byte is below 40** with the RGB of
`light[red].current`. The colour's top byte is kept. Entries past the count stay 0, so such
vertices draw **black**. 1:3 has 17 lights and many vertices with red 17..39, and RAM confirms
those entries hold 0 (checked: `mid_14000.bin`). Levels with no lights (5:1, 7:1, 9:2, 12:6)
keep their colours as they are.

### The shared skeletal tables (+0x544..+0x588)

These were levels.md's open "{pointer, count} sections". They hold the data of the skinned
models (soldiers, the player's arms, animated objects), which the models reference by offset.
Each table is relocated at load by its own function, and each function stops the game (a `break`)
when an offset falls outside the table it points into. The sizes below come from those bounds
checks:

| Header | Table | Entry | Offsets inside relocated against | Relocator |
|---|---|---|---|---|
| +0x544 / +0x548 | Animation table | 8: {u8 frames, u8 bones, u16, u32 offset} | +0x54C (frames) | `0x80086C9C` |
| +0x54C / +0x550 | Animation frames | 0x14 (+0x10: offset of its rotations) | +0x554 | `0x80086D18` |
| +0x554 / +0x558 | Packed rotations | 8 | | |
| +0x55C / +0x560 | Bones | 0x14 (+0xC, +0x10: children, offsets from the table) | +0x55C | `0x80086D8C` |
| +0x564 / +0x568 | Face tables | 8: {offset, count} | +0x56C | `0x80086E34` |
| +0x56C / +0x570 | Skinned faces | 0x1C | | |
| +0x574 / +0x578 | Vertex-table index | 8 | +0x57C | `0x80086EB0` |
| +0x57C / +0x580 | Vertex tables | 8: {offset, count} | +0x584 | `0x80086F24` |
| +0x584 / +0x588 | Vertices | 8: {s16 x, y, z, pad} | | |

The viewer calls this block the "Entry Table" and describes each structure, including the frame
layout (a 10:11:10-bit packed translation at +0x4, which `0x8006A96C` unpacks the same way), the
rotation decoding, the bone tree and the 28-byte skinned face. 1:3's counts are 235, 6715,
52894, 158, 7, 1972, 7, 182 and 1123. All 94 BSDs pass the bounds checks (`fmt_bsd.py`). The
game reads the frames in `0x80084718` (the pose, from the skinned draw `0x80080DD4`); the
rotation decoding wasn't cross-checked against it.

### Models (+0x5A8, 0x100 bytes each)

`g_models` (`0x8009EC58`). The viewer calls these "RenderObjects".

**Inheritance.** At load, a model whose +0xE8 holds another model's hash (103 of the 1183 models
on the disc) copies from that base each field group that is 0, or -1 for the s16/s32 fields
marked so below (`level_load`). The groups are: +0x4 with +0x3C..+0x83, +0x88..+0x98 and
+0xCC..+0xD0; +0xC with +0x94; +0x10 with +0x14; +0x8, +0x18, +0x20..+0x38; +0x84; +0x9C; +0xA0;
+0xA4..+0xAC; +0xB0 with +0xB4; +0xB8 with +0xBC; +0xC0 with +0xBE; +0xC4; +0xC8; +0xD4 with
+0xD6..+0xE7 (if < 0); +0xEC..+0xF8 (each -1); +0xFC (-1).

`model_relocate` (`0x80087080`) turns the offsets into pointers. Each offset is body-relative
unless the table names another base.

| Offset | Meaning | Evidence |
|---|---|---|
| +0x00 | Hash (-1: an empty slot; slots 3, 5, 7, 9 in 1:3) | `model_find` |
| +0x04 | Offset of a bounds block (below); set on no model on the disc | `0x80086798` |
| +0x08 | Animations: {u16 count, u16, count × offsets into the animation table} | relocate (+0x544), `0x8006A96C` |
| +0x0C | Model scripts: {u16 count → +0xBC, u16, u16 the behaviour script → +0x94, ..., at +8 count × 16 bytes} | `0x8003F788`, enemies.md |
| +0x1C | Set to 1 when the first instance is made; with it, later instances share the face lists | `0x8006A96C` |
| +0x20, +0x24, +0x28 | Copied to the instance (`+0x14`, `+0x18`, `+0x1C`); +0x24 is set on 2 models | `0x8006A96C` |
| +0x2C | **Static faces**: {u32 count, ...}, 16 bytes each (below) | `0x8006A96C`, `0x8008080C` |
| +0x30 | A list {u32 count, u32, count × u32}, copied per instance (so it changes at run time); set on 24 models, always with +0x90 | `0x8006A96C` |
| +0x34 | A second face list in the +0x2C format, drawn instead of it when the draw's fourth argument is set; set on no model | `0x8006A96C`, `0x8008080C` |
| +0x38 | Face table index (an offset into +0x564): skinned faces | relocate, `0x80080DD4` |
| +0x3C..+0x43 | From the bounds block: a minimum corner? | `0x80086798` |
| +0x44..+0x83 | From the bounds block: 8 points {s16 x, y, z, pad}, scaled by +0xA4..+0xAC | `0x80086798` |
| +0x84 | Offset into the vertex-table index (+0x574) | relocate, `0x80080DD4` |
| +0x88 | Vertices, 8 bytes each (static models) | `0x8006A96C`, the draws |
| +0x8C | u16 vertex count (also the size of the colour list) | `0x8006A96C` |
| +0x8E | u16 count of the +0x90 entries | `0x8006A96C` |
| +0x90 | 8-byte entries (normals? *inferred*) | `0x8006A96C` |
| +0x94 | u16 the behaviour script started at spawning (from +0xC) | levels.md |
| +0x96, +0x98 | Runtime: the bounds block's parts | `0x80086798` |
| +0x9C | Offset of the root bone (+0x55C) | relocate |
| +0xA0 | s32 16.16 collision radius → entity `+0x2D4` (160, 188, 230 or 76 on the disc) | `0x8006A96C` |
| +0xA4, +0xA8, +0xAC | Scale, 16.16 (1.0 on 967 models, 1.8 on 17) → entity `+0x270..+0x278` | `0x8006A96C` |
| +0xB0, +0xB4 | Count, then 0x3C-byte entries (word 0 cleared at load); set on 189 models, nearly the same ones as the scripts | `0x8003F788` |
| +0xB8, +0xBC | The scripts, made from +0xC (runtime) | enemies.md |
| +0xBE, +0xC0 | **Hit parts**: count, then 0x48-byte templates, copied into entity `+0x304` (fields +0x4, +0x8/+0xC, +0x10, +0x14, +0x1C, +0x1E, +0x30..+0x38, +0x40..+0x44) | `0x8006A96C`, entities.md |
| +0xC4 | Vertex colours, 4 bytes × +0x8C (red < 40: a light, above) | `0x8006A96C`, `0x8008080C` |
| +0xC8 | u16 | inherited only |
| +0xCC..+0xD3 | From the bounds block: a maximum corner? | `0x80086798` |
| +0xD4.. | s16 (< 0: inherit), then +0xD6..+0xE7 | `throw_weapon`, `fire_gun`, `0x8007E034` read it |
| +0xE8 | Base model hash (-1: none) | `level_load` |
| +0xEC..+0xF8 | 4 words, -1: inherit | `0x8004B0F0`, `0x800732F8` |
| +0xFC | **Type**: the entity type to spawn (levels.md), or 0..3 / 0x64 for parts that aren't spawned by type. Type 1: a weapon model, matched by hash against `g_weapons` (13 entries) at load | `level_load`, `spawn_by_type` |

**The bounds block** (+0x04) is laid out as {u16 parts, u16, u16 → +0x8C, u16, 8 bytes →
+0x3C, 8 bytes → +0xCC, 8 × 8-byte points → +0x44, then parts}. No model on the disc has one.

**Static faces** (+0x2C): a u32 count, then the faces from +4. The model draw `0x8008080C` reads
each 16-byte face like this:

| Offset | |
|---|---|
| +0x0 | u8 u0, v0, u16 CLUT → the POLY_GT3's uv0/clut word |
| +0x4 | u8 u1, v1, u16 texture page → uv1/tpage |
| +0x8 | u8 u2, v2, u16 (unused) → uv2 |
| +0xC | u32 vertex indices: v0 = bits 0..9, v1 = bits 10..19, v2 = bits 20..29 |

The same indices pick the vertex colour. The viewer masks the indices to 8 bits. The code masks
them to 10 bits (`& 0x3FF`; the colour lookup uses `(w >> 8) & 0xFFC` and `(w >> 18) & 0xFFC`).
Semi-transparency is per instance (instance +0x68: code 0x36 instead of 0x34), not per face.

**Instances** (`0x8006A96C`, `render_instance_init`, 0xB0 bytes). Every drawn object gets one:
models through the entity's `+0x100`, and also each TSP chunk (`tsp_load`, flags 0x16). Static
models get their own copies of the vertices, colours and +0x30 list unless the flags share them.
Skinned ones get room for a pose. Soldiers (0x1402) and type 0x1405 allocate from a separate heap
(`0x8008ED30`).

### Entity records, zones, scripts, nodes, paths

levels.md covers these: the entity records (+0x53C) with their volumes and message lists, the
zone record lists (+0x598), the level scripts (+0x59C) and the VM. enemies.md covers the AI nodes
(+0x540) and the paths (+0x5A0). Two record details are new here:

- **Chunk streaming records** (kind 0, class 0x4D078F23, 0x38 bytes): **+0x30..+0x36 are four
  s16 chunk numbers**, the chunks that should be resident around this volume. When the player
  enters it, `chunk_stream_trigger` (`0x80054378` → `chunk_stream`) works out which listed
  chunks aren't loaded (`0x80054100`) and which slots hold chunks the list doesn't name
  (`0x80054298`). It then loads the first missing chunk into the first such slot, asynchronously
  (`tsp_load_async`, `0x8008C718`). Until that read finishes, the slot (`0x800AA184`) is skipped
  by the draws, by the collision tests and by the animated faces. 1:3's ten records name runs of
  four consecutive chunks, e.g. (5, 2, 3, 4) and (1, 2, 3, 4) at the two ends of a doorway. The
  record's volume is the usual shape at +0x28 (levels.md, Triggers).

  **One chunk at a time, seconds per chunk.** `chunk_stream` (`0x8005439C`) remembers the record
  (`0x80099070`). The needed list is the record's chunks that no slot holds, in the record's order.
  The evictable list is the slots, in slot order, whose chunk the record doesn't name. The slot's
  chunk number (`0x80098970[slot]`) changes **at once**, and `0x800AA184` holds the slot until the
  read is done. While a read is in progress, `chunk_stream` leaves it alone if the record names
  the chunk being read. Otherwise it prints a warning and replaces that read (1:3 at vblank 11409
  went 5 → 9 → 5 → 9 this way). The **frame idle job** (`frame_idle_job`, `0x80090A30`, a
  four-step round robin that also runs the message queue and the spawn queue) calls
  `chunk_stream` again, so the next missing chunk starts after the previous one is in. 2:1's
  record at vblank 37700 needed chunks 5 and 6: chunk 5 finished at 37884 and chunk 6 started at
  37886.

  Watched on the recorded session (`VALOR_WATCH="cs=80098970:10;cl=800AA184:4;cr=80099070:4"`),
  every read took **240 to 390 vblanks (4 to 6.5 seconds)**. The player can outrun the stream.
  At 2:1 vblank 38124 he walked onto chunk 6's floor while it was still being read. The floor
  search found only a lower floor in the loaded chunks, and he dropped from y 449 to 603
  (`engine/tools/coretest --stream --stream-log` reproduces every tick of it, with the reads
  timed as watched). The slot assignments themselves follow the player's zones exactly: the
  engine's streaming gives the game's slot numbers on every tick of 1:3, 2:1 and 2:2, except
  one tick of idle-job timing. An engine that loads instantly differs from the original only
  where the player outran a read.
- **Kind 5** (class 0x413B09BF): a volume that the ordinary zone code reads, like any other
  record. It has no class-specific code: no instruction builds the hash. There are 51 on the disc
  (`fmt_bsd.py -v`), and they are used in three ways:
  - **Use zones.** With a `+0x4C` list, pressing Use (action 5) inside the volume sends each
    message of the list (`player_update` → `zone_use`, `0x80084670` → `0x800845DC`). The zone
    test (`0x80084400`) tests a record without enter/leave lists for the player only when it has
    a +0x4C list.
  - **Enter/leave messages** through +0x38 and +0x3C, as for other records.
  - **Tag lists** (+0x48: {count, u32 tags}). These are read by `record_has_tag` (built-in 9, and
    the message filter of type 1 in the dispatcher `0x80066EA8`) and by `0x8005359C` (built-in 10,
    filter 5). The tags are group ids shared across records: in 5:1, tag 0x8008D is on a kind-2
    object and two kind-5 volumes, and tags 0x1xxxx link a kind-1 trigger to the soldiers and
    objects it controls. So a script subscribed with a tag filter receives the messages of the
    volumes in its group.

levels.md says kind 5 has "no messages". 2:1's first volume does have no enter/leave lists,
but it has a +0x4C list, and other kind-5 records carry +0x38 or +0x3C lists (6 of the 51 have
+0x4C). The `+0x4C` list (the Use messages) is new; the `+0x50` pointer is still unread.

### Split-screen BSDs (`DATA\TWOPLAYR`)

These have the same body layout with an empty level part: no TSP prefix and no lights. They hold
the models (2..10), the shared skeletal tables and a small +0x53C/+0x540. Each comes with a TAF
of the same name. `LEVEL2P.BIN` reads them (`%s%s_p1.BSD`, `%s%s.TAF`); their use wasn't
followed.

## TSP (world chunk)

Every TSP on the disc is id 1, version 2. `tsp_parse` (`0x8008C514`) reads the header (17
words):

| Offset | | Use |
|---|---|---|
| +0x00 | u16 id (1), u16 version (2) | not read |
| +0x04, +0x08 | Node count, node offset | the render tree |
| +0x0C, +0x10 | Face count, face offset | the count is the number before stripping (≥ the faces stored); not read |
| +0x14, +0x18 | Vertex count, offset (8 bytes: s16 x, y, z, pad) | the draw |
| +0x1C, +0x20 | Count, offset of a second 8-byte array | kept at chunk +0x8E/+0x90; **0 in all 220 files** |
| +0x24, +0x28 | Colour count (= vertex count), offset: 4 bytes per vertex, R, G, B, (unused) | the draw |
| +0x2C, +0x30 | Count, offset | not read; 0 in all files |
| +0x34, +0x38 | Dynamic block count, offset | `tsp_dyn_parse` (`0x800545D8`) |
| +0x3C | Collision block offset | levels.md (Collision), `tspcol.py` |

The viewer's "version 3" layout belongs to Medal of Honor: Underground; this game has none.

### The render tree

The viewer reads every node as 0x24 bytes. In the game, a **leaf is 0x1C bytes and an inner
node 0x24**. `tsp_parse` walks the table with that stride and stops after the highest child
offset it has seen.

| Offset | Inner node (+0xC = 0) | Leaf (+0xC > 0) |
|---|---|---|
| +0x00 | s16 bbox min x, y, z; max x, y, z | same |
| +0x0C | 0 | Face count |
| +0x10 | PVS mask A | PVS mask A |
| +0x14 | PVS mask B | PVS mask B |
| +0x18 | Child 0 (offset from the node table; -1: none) | Offset of the first face (from the face table) |
| +0x1C, +0x20 | Children 1, 2 | |

**The PVS** (the viewer's two "Unknown" fields). Each frame `0x8008CBE4` finds, in every loaded
chunk, the leaf whose box holds the camera, taking the one with the smallest `max y` when boxes
overlap (`0x8008C9F0`). The camera leaf's B becomes `[0x800AA178]`, and its A is ORed into
`[0x800AA17C]`. `tsp_walk` then draws a node only if `A & [0x800AA178]` and `B & [0x800AA17C]`
are **both** non-zero. Visibility therefore doesn't depend on the view direction, and the masks
are compared crosswise (*inferred*: 32 + 32 cell groups). Each file has 1..100 distinct mask
pairs (`fmt_tsp.py`).

### Faces (16 bytes, read by `0x80010B60`)

| Offset | |
|---|---|
| +0x0 | u16 v0, u16 v1 |
| +0x4 | u16 v2 |
| +0x6 | u8 u0, v0 |
| +0x8 | u16 CLUT |
| +0xA | u8 u1, v1 |
| +0xC | u16 texture page: bits 0..11 are the GPU tpage (x/64, y/256, blend mode, colour depth); **bit 14 makes the face semi-transparent** (code 0x36, else 0x34) |
| +0xE | u8 u2, v2 |

A vertex index also picks that vertex's colour. Each leaf's faces are consecutive. The faces are
drawn as POLY_GT3, and polygons close to the camera go to the subdivider (RECON.md, World
rendering). 

**Fog.** When `+0xCC` ≠ 0, each vertex colour is blended toward the fog colour by
clamp(2 × (screen z − fog near), 0, 4096) / 4096 (GTE `dpcs`), for polygons whose depth × 4
exceeds fog near. Animated lights are applied before fog. All 220 files have every face in a
leaf, and the stored faces equal the leaves' total (`fmt_tsp.py`).

### Dynamic blocks (animated faces)

Each block is parsed once per load into the chunk slot's table: 0x288 bytes, room for 32 blocks.

| Offset | |
|---|---|
| +0x00 | Block size (offset to the next) |
| +0x04 | 0 in all 336 blocks; never read |
| +0x08 | Mode (below) |
| +0x0C | **Group id**: what the level scripts name (`world_anim_step`, built-in 18; `world_anim_set`, built-in 21) |
| +0x10 | u16 frame count |
| +0x12 | u16 face count |
| +0x14 | u16 offset of the face list (u16 face indices) |
| +0x16 | u16 offset of the frames: per frame, per face, 10 bytes {uv0, CLUT, uv1, tpage, uv2}, copied over the face's +0x6..+0xF |

The modes, as `world_anim_step` steps them:

| Mode | Step |
|---|---|
| 0 | Forward, stopping on the last frame |
| 1 | `frame = (frame + 23) % frames`: a pseudo-random jump. The viewer calls it "jump to last" |
| 2 | Loop |
| 3 | Ping-pong |

The current frame of each group below 99 is kept in `0x80099AB8` (0xFF: never set). A chunk
that is streamed out and back in therefore comes back on the same frame (`0x800545D8`
re-applies it). The disc uses modes 0 (305 blocks, 2 frames: intact/broken windows and the like)
and 3 (31 blocks, 4 frames, 12 faces, groups 99..105, whose state isn't saved).

### Collision

levels.md (Collision: TSP chunks) and `tspcol.py`. The leaf's fourth halfword (the viewer's
"Property Set index") is the zone id that indexes the BSD's zone record lists (+0x598).

## TAF (level textures and sounds)

The viewer says a TAF is a collection of TIMs and VABs, with one file used per language. The
reader confirms that and fixes the order: `level_load` builds `data\msnM\lvlL\M_LD.taf`, with
the digit D from `0x8009A618`, and loads the whole file.

1. `taf_load_tims` (`0x800869E0`): while the next word is 0x10, it reads a standard TIM
   (`OpenTIM`/`ReadTIM`), uploads the pixels and the CLUT to the VRAM positions in its header,
   and steps over it.
2. `taf_load_vabs` (`0x80086A94`): the rest is a table {u32 count, count × u32 offsets from the
   table}. Each offset points to a **VAB** (`pBAV`) with no programs and no tones, only a sample
   table. `vab_upload` (`0x800418E4`) sends each to SPU RAM as bank 0..7 (header size 0xA20 +
   programs × 0x200). It keeps the 0x200-byte sample-offset table and the 256 sample addresses.
3. The file buffer is freed.

The TIMs are 4- or 8-bit textures placed at fixed positions in VRAM. Faces and models address
them through tpage, CLUT and UV, so a loader must rebuild the 1024×512 VRAM image from the TIMs
(as the viewer's `VRAM.c` does) and sample it per face. `fmt_taf.py` lists every TIM and bank.
All 125 TAFs parse to their last byte.

**0 and 1.** The digit is bit 0x2 of `g_game+0xD8` (the cheat flags, player.md), copied by
`level_entry`. In every level pair the TIMs are identical, and only the **last VAB** differs (or
neither: MSN12, 2:1, 2:2, 3:1). So the second file swaps one sound bank, the soldiers' speech
according to the viewer (English instead of German; *not checked by listening*).

## VB (level music)

`level_set(1, ...)` (level built-in 13, "start the music") streams `\data\msnM\lvlL\mM_L.vb`,
or `mM_LA.vb` when `0x80038658` is set. The shell's audio screen code (`0x8003D9D8`) writes that
setting; what it is labelled wasn't checked. The file is headerless SPU ADPCM (16-byte blocks,
the first one silent). `music_start` (`0x8008A838`) primes a 2 × 0x5000-byte ring in SPU RAM and
refills it from the CD in the SPU IRQ, playing on **one voice** (voice 0, so mono). The sample
rate (the voice pitch) wasn't read. The two files of a pair differ in a few bytes or in length.

## RSC (front-end archives)

The viewer's layout is right: a 64-byte name, u32 count, u32, then count × {64-byte name, u32
index, u32 length, u32 offset, u32 0}. The second u32 is a leftover pointer from the tool that
built the archive: the shell's loader (`0x8005F580`) overwrites it with a pointer to the entry
table. Contents:

- **TIMs**: 8-bit for screens.
- **`.sst`** menu scripts (all in `GLOBAL.RSC`). These are not the model scripts of
  `tools/recon/sst.py`.
- `global\sound\shell.vab`.
- **`.gfx`**: clerk models for the title screen (`GLOBAL2.RSC`, `MTITLE1.RSC`).

The viewer documents SST and GFX. Only the shell uses them: they matter for a new front end, not
for levels.

## TIM, STR

**TIM** is the standard PlayStation format: the viewer, `OpenTIM`/`ReadTIM`. Flag bit 3 means
a CLUT block follows; the low bits are the depth.

**STR** is standard MDEC video with XA audio in Form 2 sectors (15 fps, 37.8 kHz stereo per the
viewer). jPSXdec reads it when given raw sectors, not the 2048-byte extracted files.

## Loading a level in a new engine

| Piece | Source | Status |
|---|---|---|
| Geometry: vertices, faces, UVs | TSP chunks, all of them (the game streams 4 at a time; an engine can keep all) | Known |
| Textures | TAF TIMs → a VRAM image; faces' tpage/CLUT/UV (4/8-bit, semi-transparency bit 14, blend mode in the tpage) | Known |
| Vertex colours | TSP colours, with red < 40 substituted by animated lights (or black) when the level has lights | Known |
| Animated lights | BSD +0xD8/+0xDC, stepped once per 30 Hz tick | Known |
| Fog, clear colour, view distance | BSD +0xCC..+0xD4, +0x590 | Known. The exact depth used for fog (screen z vs OTZ) wasn't measured |
| Sky, stars, moon | BSD +0x58C..+0x597 | Fields known; the drawing (`0x80088170`, the 16 POLY_G4 strips of `0x80088980`) wasn't read |
| PVS | TSP node masks, cross test | Known |
| Animated faces | TSP dynamic blocks, driven by level scripts | Known |
| Static models | BSD models +0x2C/+0x88/+0xC4, scale, inheritance | Known; +0x30/+0x90 (24 models) unknown |
| Skinned models, animations | BSD +0x544..+0x588 and model +0x8/+0x38/+0x84/+0x9C | Layout known (sizes checked); the pose code `0x80084718` wasn't read against the viewer's rotation decoding |
| Collision | TSP collision blocks | Known (levels.md) |
| Entities, triggers, zones, streaming | BSD records, zone lists | Known (levels.md); kind-5 and streaming fields above |
| Scripts | BSD +0x59C, model scripts | Known (levels.md, enemies.md) |
| AI nodes, paths | BSD +0x540, +0x5A0 | enemies.md |
| Hit parts, collision radius | Model +0xBE/+0xC0, +0xA0 | Copied fields known; 0x48-byte meaning partly (entities.md) |
| Sound effects | TAF VABs (bank = index in the TAF) | Banks known; which sound id plays which sample is in the sound code, not read |
| Music | VB | Format known; pitch not read |

Still missing:

- Model fields +0x10..+0x28, +0x30, +0x90, +0xB0/+0xB4, +0xC8, +0xD4..+0xE7 and +0xEC..+0xF8.
  Their readers are named above but not read.
- BSD header +0x3FC (20 entries, identical everywhere) and +0x90..+0xA0/+0xD2/+0xD6, which have
  no reader found.
- Record `+0x50`.
- Entity `+0x270` is called "Rotation" in entities.md, but `0x8006A96C` stores the model's scale
  there, and the world entity sets it to 1.0, 1.0, 1.0. One of the two descriptions is wrong.

## Tools

- `tools/recon/fmt_bsd.py FILE.BSD... [-v]`: the size table, the header, the lights, the
  models, the streaming and kind-5 records, plus the bounds checks.
- `tools/recon/fmt_tsp.py FILE.TSP... [-v]`: the header, the tree (0x1C/0x24-byte nodes), face,
  light-colour and PVS counts, the dynamic blocks.
- `tools/recon/fmt_taf.py FILE.TAF [FILE2.TAF]`: the TIMs with their VRAM positions, the VABs,
  and with two files, which entries differ.

## Names

```
8004A300 bsd_alloc                  # tries the BSD size table from the largest entry down; returns the entry
8004A374 bsd_read                   # (path, &entry): sector 0 = size table, then the body
8004770C camera_entity_init         # camera far plane from g_level+0x590 (0: 4500)
800418E4 vab_upload                 # (vab, bank 0..7): sample-only VAB to SPU RAM
80053AC0 lights_update              # animated lights: g_level+0xD8 count, +0xDC entries of 0x14
80053DB0 tsp_load_initial           # loads the first g_chunk_count chunks into the slots
80053E68 world_create               # the world entity; TSP count/slots/first chunk from the header
80054100 chunk_stream_needed        # streaming record +0x30..+0x36 chunks not loaded
80054238 chunk_not_in_record        # slot's chunk absent from the streaming record's four
80054298 chunk_stream_evictable     # slots whose chunk the record doesn't list
80054378 chunk_stream_trigger       # (record): kind-0 0x4D078F23 entered
80054498 tsp_dyn_apply              # writes a dynamic block's current frame into the faces
80054554 tsp_dyn_clear
800545D8 tsp_dyn_parse              # TSP dynamic blocks -> slot table (0x288 per slot)
80062358 clear_colour_set           # g_level+0xCE..+0xD0 -> draw environments (unless +0xA0)
80066EA8 msg_filter_dispatch        # message subscriptions by filter type (1: has_tag, 5: tags_match)
8006A96C render_instance_init       # (instance, model, flags, entity): copies model data, hit parts, scale
80080DD4 model_draw_skinned         # face tables / bones (inferred from the fields it reads)
8008080C model_draw_static          # model +0x2C faces, 10-bit packed vertex indices
80084670 zone_use                   # Use pressed: records the player is inside -> +0x4C messages
800845DC record_send_use            # sends a record's +0x4C list
80086798 model_bounds_setup         # model +0x4 block (no model on the disc has one)
800869E0 taf_load_tims              # uploads consecutive TIMs, returns the end
80086A94 taf_load_vabs              # {count, offsets} -> vab_upload
80086B20 level_unload               # frees the previous level (entities, chunks, sounds)
80086C9C bsd_reloc_anims            # +0x544 entries -> +0x54C
80086D18 bsd_reloc_frames           # +0x54C entries -> +0x554
80086D8C bsd_reloc_bones            # +0x55C children
80086E34 bsd_reloc_face_tables      # +0x564 -> +0x56C
80086EB0 bsd_reloc_vtx_index        # +0x574 -> +0x57C
80086F24 bsd_reloc_vtx_tables       # +0x57C -> +0x584
80086F98 zones_parse                # BSD +0x598 -> g_zone_records
80088744 stars_init                 # 255 stars, height from g_level+0x58F
800888C4 sky_init                   # moon from g_level+0x594/+0x596
80088980 sky_gradient_init          # 16 POLY_G4 strips, colour g_level+0x58C..+0x58E
8008A838 music_start                # streams a .VB into a 2 x 0x5000 SPU ring
8008C514 tsp_parse
8008C718 tsp_load_async             # streaming: slot -> 0x800AA184 until read
8008C8F4 tsp_unload
8008C9F0 pvs_leaf_find              # camera leaf by bbox; returns mask B, ORs mask A
8008CBE4 pvs_masks                  # over all chunks -> 0x800AA178 / 0x800AA17C
80099AB8 g_dyn_frames               # per dynamic group (< 99): current frame, 0xFF unset
8009A618 g_taf_variant              # TAF digit (cheat bit 0x2)
800AA178 g_pvs_b                    # camera leaf's mask B; nodes need +0x10 & this
800AA17C g_pvs_a                    # camera leaf's mask A; nodes need +0x14 & this
800AA184 g_chunk_loading            # slot being streamed (-1: none), skipped by draw/collision
```
