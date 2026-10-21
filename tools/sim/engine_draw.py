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
DRAWN = os.path.join(ROOT, "build", "engine", "drawn.log")
G_FRAME_COUNT = 0x8009A5D8
LINE = re.compile(r"^drawn (\d+) (\d+):(.*)$")


class Drawn:
    _cache = None

    @classmethod
    def get(cls):
        if cls._cache is None:
            cls._cache = cls(DRAWN)
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
        # the drawn flags and the out-of-sight counter (entities.md, Despawning)
        if drawn_now:
            m.w32(e + 0x298, 1)
            m.w32(e + 0x29C, 1)
            m.w32(e + 0x290, m.s32(e + 0x294))
        else:
            m.w32(e + 0x29C, 0)
            if m.u32(e + 0x298) and fa & 0x01000000:
                if not keep_near(eng, e):
                    m.w32(e + 0x290, m.s32(e + 0x290) - 1)
        if d is not None and d[2] != m.s32(e + 0x290) and fa & 0x8000:
            eng.note("out-of-sight counter differs from the original's", f"{h:X} {m.s32(e + 0x290)} / {d[2]}")
        # entities_draw (0x8006C714): counted out -> message 2 to its record, removed
        if fa & 0x8000 and m.s32(e + 0x290) <= 0 and m.u32(e + 0x298):
            ent = eng.ents.get(e)
            if ent is not None and ent.rec is not None:
                eng.logic.run_message(None, ent.rec.pair, 2, "despawn")
            eng.remove(e, "out of sight")


def keep_near(eng, e):
    """0x8006A3BC: kept while within the camera's far distance (max + (mid + min)/4 of the axis
    differences); the make-room exception is not implemented (it never fired in the session)."""
    m = eng.m
    cam = m.u32(0x8009D638)
    co = m.u32(cam + 0x388)
    far = m.s32(co + 0xC4) >> 16 if m.s32(co + 0xC4) > 0xFFFF else m.s32(co + 0xC4)
    d = sorted(abs(s16((m.s32(e + 0x20C + 4 * k) >> 16) - (m.s32(cam + 0x20C + 4 * k) >> 16))) for k in range(3))
    return d[2] + ((d[0] + d[1]) >> 2) <= far


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
