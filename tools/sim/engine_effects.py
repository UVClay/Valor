"""The effects model scripts ask for (sst_vm.py's calls), carried out by the engine as they are
recorded (engine_scripts.Calls)."""
import brain_sim as bs
from engine import s16, s32

G_PATHS = 0x8009E290


def run_effect(vm, ch, op, name, args):
    eng = vm.eng
    fn = EFFECTS.get(name)
    if fn is None:
        eng.note(f"script effect {name} not implemented")
        return
    fn(vm, *args)


def root_motion(vm):
    from engine_anim import root_motion as rm
    rm(vm.eng, vm.e)


# ---------------------------------------------------------------- op 2F behaviour (0x8005AC00)

def brain_clear_moves(m, b):
    m.w32(b + 4, m.u32(b + 4) & ~0x5C03)


def brain_release_node(eng, e):
    """Free the node the soldier held (+0xB6): its holder word +0x30 cleared."""
    m = eng.m
    b = m.u32(e + 0x38C)
    n = m.s16(b + 0xB6)
    if n >= 0:
        wo = m.u32(m.u32(0x8009D648) + 0x388)
        if n < m.u32(wo + 0xC):
            nd = m.u32(wo + 8) + 0x34 * n
            if m.u32(nd + 0x30) == m.u32(e):
                m.w32(nd + 0x30, 0)
        m.w16(b + 0xB6, -1)


def brain_reset_gait(eng, e):
    """brain_reset_gait (0x8005ABA0)."""
    m = eng.m
    b = m.u32(e + 0x38C)
    m.w16(e + 0x2EC, m.s16(b + 0xC4))
    m.w16(e + 0x2E8, m.u16(b + 0xC6))
    m.w16(e + 0x3C8, m.s16(b + 0xCA))
    m.w16(b + 0xC0, 0)
    m.w16(b + 0xC2, 0)
    m.w32(e + 0x224, 0x10000)
    m.w32(b + 0xD8, 0)
    m.w16(b + 0xCC, m.u16(b + 0xD0))


def path_find(m, pid):
    """path_find (0x80057C24): the index of the path with that id in g_paths (8 bytes each),
    -1 if none."""
    base = m.u32(G_PATHS)
    for i in range(m.s32(0x8009E28C)):
        if m.s16(base + 8 * i) == s16(pid):
            return i
    return -1


def path_nearest_point(m, e, path):
    st = {"pos": [m.s32(e + 0x20C + 4 * k) for k in range(3)]}
    return bs.path_nearest_point(m, st, path)


def behaviour(vm, goal, a, b_, c, d):
    eng, m, e = vm.eng, vm.m, vm.e
    b = m.u32(e + 0x38C)
    if not b:
        eng.note("behaviour without a brain")
        return
    goal = s32(goal)
    goals = m.u32(b + 4)
    if goal == -1:
        brain_reset_gait(eng, e)
        m.w32(b + 4, goals & ~1)
    elif goal in (-0x4000, -0x1000, -2):
        brain_reset_gait(eng, e)
        m.w32(b + 4, goals & ~(-goal))
    elif goal == -0x800:
        brain_reset_gait(eng, e)
        brain_release_node(eng, e)
        m.w32(b + 4, goals & ~0x800)
    elif goal in (-4, -0x400):
        m.w32(b + 4, goals & ~(-goal))
    elif goal == -0x2000:
        m.w32(b + 4, goals | 2)
    elif goal == 0x2000:
        m.w32(b + 4, goals & ~2)
    elif goal == 1:
        brain_clear_moves(m, b)
        brain_release_node(eng, e)
        brain_reset_gait(eng, e)
        m.w32(b + 4, m.u32(b + 4) | 1)
        m.w32(b + 0x10, a)
        m.w16(b + 0xC2, b_)
        m.w16(b + 0xD4, c)
        if s16(c) <= 0:
            m.w16(b + 0xD4, 0x100)
    elif goal == 4:
        g = goals | 4
        m.w32(b + 4, g)
        if g == 4:
            brain_reset_gait(eng, e)
        m.w32(b + 0x18, a)
        m.w32(b + 0xDC, a)
        m.w32(b + 0, -0xA)
        m.w16(b + 0xC0, b_)
        m.w32(b + 0xD8, c)
    elif goal == 2:
        brain_clear_moves(m, b)
        brain_release_node(eng, e)
        brain_reset_gait(eng, e)
        m.w32(b + 4, m.u32(b + 4) | 2)
        path = path_find(m, a)
        m.w16(b + 0xB8, path)
        m.w16(b + 0xBA, b_)
        mode = s16(b_)
        m.w16(b + 0xBC, path_nearest_point(m, e, path))
        if mode == 0 or mode == 2:
            m.w16(b + 0xBE, 1)
        elif mode == 1:
            m.w16(b + 0xBE, -1)
        elif mode == -1:
            m.w16(b + 0xBA, goal)
    elif goal == 0x1000:
        brain_clear_moves(m, b)
        brain_release_node(eng, e)
        brain_reset_gait(eng, e)
        m.w32(b + 4, m.u32(b + 4) | 0x1000)
        m.w32(b + 0x40, a)
        m.w32(b + 0x80, b_)
        m.w16(b + 0xA8, c)
        m.w16(b + 0xC2, d)
    elif goal == 0x800:
        brain_clear_moves(m, b)
        brain_release_node(eng, e)
        brain_reset_gait(eng, e)
        m.w32(b + 4, m.u32(b + 4) | 0x800)
        m.w32(b + 0x3C, a)
        m.w32(b + 0x7C, b_)
        m.w16(b + 0xA8, c)
        eng.note("brain_node_target not implemented")
        m.w16(b + 0xC2, d)
    elif goal == 0x4000:
        brain_clear_moves(m, b)
        brain_release_node(eng, e)
        brain_reset_gait(eng, e)
        m.w32(b + 4, m.u32(b + 4) | 0x4000)
        m.w32(b + 0x10, a)
        m.w32(b + 0x50, b_)
    elif goal == 0x8000:
        m.w32(b + 4, 0)
        brain_release_node(eng, e)
        brain_reset_gait(eng, e)


def save_pose(vm, *a):
    """A cross-fade's start (op 00 mode 1, an unforced script start): the pose on screen and the
    root offset saved (+0x88 -> +0x8C, +0x98 -> +0xA0; animation.md, Op 00)."""
    m, inst = vm.m, vm.inst
    p = m.u32(inst + 0x88)
    if p:
        nb = m.u16(inst + 0x72)
        m.copy(m.u32(inst + 0x8C), p, 8 * nb)
        m.w32(inst + 0x88, m.u32(inst + 0x8C))
    for k in range(3):
        m.w16(inst + 0xA0 + 2 * k, m.s16(inst + 0x98 + 2 * k))


def anim(vm, n, mode, a, b):
    if mode == 1:
        save_pose(vm)


def sound(vm, snd, pitch, pspread, vol, vspread, bank, flags):
    """op_sound (0x80058420): a sound on the entity; the voice into +0xFC (audio.md, Scripts)."""
    eng, m, e = vm.eng, vm.m, vm.e
    snd = s32(snd)
    if snd < 0:
        return
    loop = 1 if flags & 0x20 else 0
    if flags & 3:
        v = eng.audio.play(0, snd, e, vol, vspread, pitch, pspread, 0, loop, 0, 0)
        if v >= 0 and flags & 2 and (vol >> 2):
            from engine_feed import FN
            eng.rand.draw(*FN["sound"])
            eng.rand.draw(*FN["sound"])
        return
    bank = s32(bank << 24) >> 24 if bank < 0x100 else s32(bank)
    if bank == -1:
        md = m.u32(m.u32(e + 0x100) + 8) if m.u32(e + 0x100) else 0
        bank = m.u16(md + 0xC8) if md else 0
    m.w32(e + 0xFC, eng.audio.play(bank, snd, e, vol, vspread, pitch, pspread, 0, loop, 0, 0))


EFFECTS = {
    "sound": sound,
    "root_motion": lambda vm: root_motion(vm),
    "anim": anim,
    "pose_blend": save_pose,
    "behaviour": behaviour,
}
