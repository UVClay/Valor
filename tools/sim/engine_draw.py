"""What the drawing pass does to the game state (docs/re/entities.md Despawning, animation.md When a
model is posed and drawn, The pose, Skinning and joints): poses and joints of the models drawn,
the drawn flags, the out-of-sight counters and the removals they make.

Which models the renderer drew (`+0x29C`, `model_draw`'s result, and from it `+0x298`) is taken
from the original (VALOR_TRACE_DRAWN, build/engine/drawn.log): it is the view frustum and the
clipping of the recording's (widened, 21:9) view, which the engine does not reimplement. What is
done with it, posing, joints, counters, removals, is the engine's."""
import os
import re
from collections import defaultdict

import combat_sim as cs
from engine import s16, s32

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
DRAWN = None          # engine.input_log("drawn.log"), on first use
G_FRAME_COUNT = 0x8009A5D8
LINE = re.compile(r"^drawn (\d+) (\d+):(.*)$")


class Drawn:
    _cache = None

    @classmethod
    def get(cls):
        if cls._cache is None:
            from engine import input_log
            cls._cache = cls(DRAWN or input_log("drawn.log"))
        return cls._cache

    def __init__(self, path):
        self.by_level = defaultdict(dict)       # level -> tick -> {handle: (298, 29C, 290)}
        level = None
        mark = re.compile(r"^replay: mark mission (\d) (\d)")
        for line in open(path, errors="replace"):
            if line.startswith("replay: mark mission"):
                mm = mark.match(line)
                level = f"{mm[1]}:{mm[2]}"
                continue
            if not line.startswith("drawn "):
                continue
            mm = LINE.match(line)
            if not mm:
                continue
            d = {}
            for it in mm[3].split():
                h, a, b, c = it.split(":")
                d[int(h, 16)] = (int(a, 16), int(b, 16), int(c))
            self.by_level[level][int(mm[2])] = d


def draw_pass(eng):
    m = eng.m
    m.w32(G_FRAME_COUNT, m.u32(G_FRAME_COUNT) + 1)
    hit_wedges(eng)
    seen = Drawn.get().by_level.get(eng.level, {}).get(eng.tick_no + 1)
    if seen is None:
        eng.note("drawn flags: no record of the next tick")
        seen = {}
    for e in list(eng.entities()):
        if m.u32(e + 0xC) & 0x80000:
            continue
        h = m.u32(e)
        d = seen.get(h)
        drawn_now = bool(d and d[1])
        fa = m.u32(e + 0xC)
        inst = m.u32(e + 0x100)
        # entity_draw_model (0x8006A5B4): out of view and drawn before: not posed
        if inst and m.u32(inst + 0x74):
            if not (fa & 0x02000000 and not drawn_now and m.u32(e + 0x298)):
                pose_update(eng, e, inst, drawn_now)
        # enemy_draw (0x8005608C): a living soldier's attached attachments follow their bones
        if m.s32(e + 8) == 0x1402 and m.s32(e + 0x4C + 4 * 0xD) != -1:
            from engine_attach import attachment_draw
            obj = m.u32(e + 0x388)
            for k in range(3):
                B = obj + 0x1C + 0x144 * k
                if m.u32(B) & 0x27 == 1:
                    attachment_draw(eng, e, B, m.s32(B + 8))
        # the drawn flags and the out-of-sight counter (entities.md, Despawning)
        if m.u32(e + 0x370) == 0x8005E760:
            # draw_in_view_counter: drawn once always; in view (the recording's counter back at
            # its start value: this handler never sets +0x29C) resets it, else cull_countdown
            m.w32(e + 0x298, 1)
            if d is not None and d[2] == m.s32(e + 0x294):
                m.w32(e + 0x290, m.s32(e + 0x294))
            elif not keep_near(eng, e):
                m.w32(e + 0x29C, 0)
                m.w32(e + 0x290, m.s32(e + 0x290) - 1)
        elif drawn_now:
            m.w32(e + 0x298, 1)
            m.w32(e + 0x29C, 1)
            m.w32(e + 0x290, m.s32(e + 0x294))
        else:
            m.w32(e + 0x29C, 0)
            # entity_draw_model: out of view with flag 0x01000000: cull_countdown (0x8006A570),
            # drawn before or not (the make-room case below marks it drawn)
            if fa & 0x01000000:
                if not keep_near(eng, e):
                    m.w32(e + 0x29C, 0)
                    m.w32(e + 0x290, m.s32(e + 0x290) - 1)
        # enemy_draw (0x8005608C): in view this frame, or dead, its muzzle flash drawn: the
        # counter (object +0xB68+2) down by one while positive (flash_draw 0x8006DD54)
        if m.u32(e + 0x370) == 0x8005608C and (m.u32(e + 0x29C) or m.s32(e + 0x4C + 4 * 0xD) == -1):
            flash_countdown(m, m.u32(e + 0x388) + 0xB68)
        # player_draw (0x8007B8F4): with the view object's +0xA4 at 400 and no 0xC000 in its +0,
        # the held model's brightness (+0x6C) toward 0x40 with flags B 0x400, else back to 0x80,
        # and its muzzle flash (inv +0x1D4)
        if m.u32(e + 0x370) == 0x8007B8F4:
            player_draw(m, e)
        if d is not None and d[2] != m.s32(e + 0x290) and fa & 0x8000:
            eng.note("out-of-sight counter differs from the original's", f"{h:X} {m.s32(e + 0x290)} / {d[2]}")
        # entities_draw (0x8006C714): counted out -> message 2 to its record, removed
        if fa & 0x8000 and m.s32(e + 0x290) <= 0 and m.u32(e + 0x298):
            ent = eng.ents.get(e)
            if ent is not None and ent.rec is not None:
                eng.logic.run_message(None, ent.rec.pair, 2, "despawn")
            eng.remove(e, "out of sight")
    # draw_level: the explosion's scorch flag lasts one drawn frame (objects.md, the light)
    m.w32(0x800AA160, 0)


def flash_countdown(m, f):
    """flash_draw (0x8006DD54)'s state: a positive counter (+2) is drawn and goes down by one,
    otherwise it is set to 0."""
    c = m.s16(f + 2)
    m.w16(f + 2, c - 1 if c > 0 else 0)


def player_draw(m, e):
    o = m.u32(e + 0x388)
    if m.s32(o + 0xA4) != 0x190 or m.u32(o) & 0xC000:
        return
    inst = m.u32(e + 0x100)
    b = m.u32(inst + 0x6C)
    if m.u32(e + 0x10) & 0x400:
        if b >= 0x41:
            m.w32(inst + 0x6C, b - 2)
    else:
        m.w32(inst + 0x6C, b + 2 if b < 0x80 else 0x80)
    flash_countdown(m, o + 0x1D4)


def keep_near(eng, e):
    """offscreen_keep (0x8006A3BC, e, the camera): kept while within the camera's far distance
    (max + (mid + min)/4 of the axis differences). Make room: an entity with flags B 0x40000
    (pickups) at 3501 or more while soldiers >= cap - 1 and a spawn waits (0x8009E288), behind the
    camera's target (its forward column . (e - camera) < 0): marked drawn (+0x298 = 1, +0x290 =
    1) and not kept, so it is counted out and removed this pass."""
    m = eng.m
    cam = m.u32(0x8009D638)
    co = m.u32(cam + 0x388)
    far = m.s32(co + 0xC4) >> 16 if m.s32(co + 0xC4) > 0xFFFF else m.s32(co + 0xC4)
    dv = [s16(m.s16(e + 0x20E + 4 * k) - m.s16(cam + 0x20E + 4 * k)) for k in range(3)]
    d = sorted(abs(x) for x in dv)
    t = d[2] + ((d[0] + d[1]) >> 2)
    from engine import G_SOLDIER_COUNT, G_SOLDIER_CAP, G_HANDLES
    if (m.u32(e + 0x10) & 0x40000 and m.s32(G_SOLDIER_COUNT) >= m.s32(G_SOLDIER_CAP) - 1
            and m.u32(0x8009E288) and t >= 0xDAD):
        h = m.u32(co + 0xC)
        tgt = m.u32(G_HANDLES + 4 * (h & 0xFFFF)) if h else 0
        fwd = [m.s16(tgt + o) for o in (0x344, 0x34A, 0x350)]
        if s32(sum(dv[k] * fwd[k] for k in range(3))) < 0:
            m.w32(e + 0x298, 1)
            m.w32(e + 0x290, 1)
            return False
    return t <= far


_models = {}


def model_of(eng, addr):
    md = _models.get(addr)
    if md is None:
        md = _models[addr] = cs.Model(eng.m, addr)
    return md


def pose_update(eng, e, inst, drawn):
    """pose_update (0x80086634) / pose_build (0x80085648), once per frame; then, for a model
    drawn, the joints (skeleton_pose)."""
    m = eng.m
    fc = m.u32(G_FRAME_COUNT)
    if m.u32(e + 0x3A0) == fc:
        return
    m.w32(e + 0x3A0, fc)
    model = model_of(eng, m.u32(inst + 8))
    nb = m.u16(inst + 0x72)
    anim, frame = m.s16(inst + 0x5C), m.s16(inst + 0x5E)
    c = m.s16(inst + 0x3E)
    if anim < 0:
        m.w16(inst + 0x62, -1)
        return
    buf = m.u32(inst + 0x90)
    if c > 0 and m.s16(inst + 0x62) == -1:
        # a fade started with no pose yet: pose_build cuts it (animation.md, The pose, 4)
        c = 0
        m.w16(inst + 0x3E, 0)
        if not m.u32(inst + 0x88):
            write_quats(m, buf, model.frame_pose(anim, frame))
            joints(eng, e, inst, model, drawn)
            return
    if c == 0:
        if (m.s16(inst + 0x62), m.s16(inst + 0x64)) != (anim, frame):
            write_quats(m, buf, model.frame_pose(anim, frame))
            m.w32(inst + 0x88, buf)
            m.w16(inst + 0x62, anim)
            m.w16(inst + 0x64, frame)
    else:
        tgt = m.s16(inst + 0x40)
        if tgt < 0:
            tgt = m.s16(inst + 0x60)
        k = 4 - c
        a0 = tuple(m.s16(inst + 0xA0 + 2 * j) for j in range(3))
        root = cs.blend_root(a0, model.root_offset(anim, tgt), k)
        for j in range(3):
            m.w16(inst + 0x98 + 2 * j, root[j])
        if k == 0:
            m.w32(inst + 0x88, m.u32(inst + 0x8C))
        else:
            saved = cs.slot_quats(m, m.u32(inst + 0x8C), nb)
            b = model.frame_pose(anim, tgt)
            n = m.u8(model.frame(anim, tgt) + 0xF)
            write_quats(m, buf, [cs.q_interp(x, y, k) for x, y in zip(saved[:n], b)])
            m.w32(inst + 0x88, buf)
        m.w16(inst + 0x62, anim)
        m.w16(inst + 0x64, tgt)
    if drawn:
        joints(eng, e, inst, model, drawn)


def write_quats(m, p, quats):
    for i, q in enumerate(quats):
        for k in range(4):
            m.w16(p + 8 * i + 2 * k, q[k])


def joints(eng, e, inst, model, drawn):
    """skeleton_pose: the joints of the pose in use, into +0x94 (only for models drawn)."""
    m = eng.m
    nb = m.u16(inst + 0x72)
    qp = m.u32(inst + 0x88) or m.u32(inst + 0x90)
    quats = cs.slot_quats(m, qp, nb)
    anim, frame = m.s16(inst + 0x62), m.s16(inst + 0x64)
    if anim < 0:
        anim, frame = m.s16(inst + 0x5C), m.s16(inst + 0x5E)
    nq = m.u8(model.frame(anim, frame) + 0xF) if anim >= 0 else nb
    root = tuple(m.s16(inst + 0x98 + 2 * k) for k in range(3))
    js = model.joints(quats, root, nq)
    jp = m.u32(inst + 0x94)
    for i, j in js.items():
        for k in range(3):
            m.w16(jp + 8 * i + 2 * k, j[k])


def hit_wedges(eng):
    """The HUD's hit wedges (0x80060B04, every drawn frame): each timer that is running drops by
    1 + g_catchup_ticks (the frame's ticks beyond the first), not below 0 (hud.md)."""
    m = eng.m
    step = 1 + max(getattr(eng, "frame_ticks", 1) - 1, 0)
    for k in range(4):
        a = 0x8009A590 + 4 * k
        t = m.s32(a)
        if t:
            m.w32(a, max(t - step, 0))
