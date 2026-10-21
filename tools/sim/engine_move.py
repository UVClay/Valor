"""entity_move (0x8006C83C) for the engine, by kind of entity: the player (move_sim.py), walkers
(brain_sim.py), and the rest (enemies.md, The physical move; entities.md, Moving)."""
import brain_sim as bs
from engine import s16, s32

ENT_FIELDS = ("push", "fwd_acc", "fwd", "fwd_max", "floor", "pos", "vel", "speed", "ang", "angvel", "prev",
              "root", "ground", "eye", "sphere", "probe", "rot", "old", "r3c8", "contact")


def read_st(eng, e):
    m = eng.m
    g = lambda o: m.s32(e + o)
    inst = m.u32(e + 0x100)
    st = dict(addr=e, type=m.u32(e + 8), handle=m.u32(e), flags_a=m.u32(e + 0xC), flags_b=m.u32(e + 0x10),
              mask=m.u32(e + 0x14), attach=m.u32(e + 0x18), contact=m.u32(e + 0x38),
              push=[g(0x1F0 + 4 * k) for k in range(3)], fwd_acc=g(0x1FC), fwd=g(0x200), fwd_max=g(0x204),
              floor=g(0x208), pos=[g(0x20C + 4 * k) for k in range(3)], vel=[g(0x218 + 4 * k) for k in range(3)],
              speed=g(0x224), ang=[g(0x228 + 4 * k) for k in range(3)], angvel=[g(0x234 + 4 * k) for k in range(3)],
              prev=[g(0x258 + 4 * k) for k in range(3)], root=[m.s16(e + 0x27C + 2 * k) for k in range(3)],
              ground=g(0x284), eye=g(0x2D4), sphere=m.u16(e + 0x2E8), probe=m.s16(e + 0x2EC),
              rot=[m.s16(e + 0x340 + 2 * k) for k in range(9)], old=[g(0x3BC + 4 * k) for k in range(3)],
              r3c8=m.s16(e + 0x3C8), brain_ptr=m.u32(e + 0x38C), inst=inst, vars=[g(0x4C + 4 * k) for k in range(0x28)])
    st["anim"] = m.s16(inst + 0x5C) if inst else -1
    st["frame"] = m.s16(inst + 0x5E) if inst else -1
    st["brain"] = BrainMem(m, st["brain_ptr"]) if st["brain_ptr"] else None
    return st


class BrainMem(bs.Brain):
    """brain_sim's Brain on the engine's memory directly."""

    def __init__(self, m, addr):
        self.mem, self.addr = m, addr

    def s16(self, o):
        return self.mem.s16(self.addr + o)

    def u16(self, o):
        return self.mem.u16(self.addr + o)

    def s32(self, o):
        return self.mem.s32(self.addr + o)

    def u32(self, o):
        return self.mem.u32(self.addr + o)

    def w16(self, o, v):
        self.mem.w16(self.addr + o, v)

    def w32(self, o, v):
        self.mem.w32(self.addr + o, v)

    def copy(self):
        return self


def write_st(eng, e, st):
    m = eng.m
    m.w32(e + 0x38, st["contact"])
    for k in range(3):
        m.w32(e + 0x1F0 + 4 * k, st["push"][k])
        m.w32(e + 0x20C + 4 * k, st["pos"][k])
        m.w32(e + 0x218 + 4 * k, st["vel"][k])
        m.w32(e + 0x228 + 4 * k, st["ang"][k])
        m.w32(e + 0x234 + 4 * k, st["angvel"][k])
        m.w32(e + 0x258 + 4 * k, st["prev"][k])
        m.w16(e + 0x27C + 2 * k, st["root"][k])
        m.w32(e + 0x3BC + 4 * k, st["old"][k])
    m.w32(e + 0x200, st["fwd"])
    m.w32(e + 0x208, st["floor"])
    m.w32(e + 0x224, st["speed"])
    m.w32(e + 0x284, st["ground"])
    m.w16(e + 0x2E8, st["sphere"])
    m.w16(e + 0x2EC, st["probe"])
    m.w16(e + 0x3C8, st["r3c8"])
    for k in range(0x28):
        m.w32(e + 0x4C + 4 * k, st["vars"][k])


def entity_move(eng, e):
    m = eng.m
    fa = m.u32(e + 0xC)
    if fa & 1:
        from engine_player import move
        move(eng, e)
        return 0
    t = m.s32(e + 8)
    h = eng.handlers.get(("move", t))
    if h:
        return h(e)
    if fa & 8 and not eng.m.u32(e + 0x10) & 0x45:
        return walker_move(eng, e)
    from engine_projectile import generic_move
    return generic_move(eng, e)


def walker_move(eng, e):
    """A character's move: brain_sim.entity_move with the frame's root turn; contact flags back."""
    st = read_st(eng, e)
    c0 = st["contact"]
    fd = bs.frame_data(eng.m, st["inst"], st["anim"], st["frame"]) if st["inst"] else None
    log = {}
    zone_before = None
    bs.entity_move(eng.world.w, st, fd[2] if fd else None, log)
    write_st(eng, e, st)
    if log.get("floor") is not None or "zone" in st:
        pass
    # the floor zone under the walker (+0x390), as floor_find gives it
    f, zone = eng.world.floor(st["pos"])
    eng.m.w32(e + 0x390, zone)
    res = 0
    ev = log.get("event")
    if ev == "landing":
        res |= 1
    slide = log.get("slide")
    if slide and slide[0] > 0:
        res |= 2
    return res
