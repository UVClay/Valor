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
        node_target(eng, e, b, a)
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


# ---------------------------------------------------------------- the other effects

def hit_parts_set(m, e, mask, kind, radius, damage):
    """0x8004C64C: the parts whose +4 bits meet the mask get the kind (+8; < 0: the default +0xC),
    the damage (+0x10; < 0: +0x14) and the radius (+0x34, squared +0x38; < 0: +0x30)."""
    pp = m.u32(e + 0x304)
    for i in range(m.u16(e + 0x36)):
        q = pp + 0x48 * i
        if not m.u32(q + 4) & mask:
            continue
        if kind > 0:
            m.w32(q + 8, kind)
        elif kind < 0:
            m.w32(q + 8, m.u32(q + 0xC))
        if damage > 0:
            m.w32(q + 0x10, damage)
        elif damage < 0:
            m.w32(q + 0x10, m.u32(q + 0x14))
        if radius > 0:
            m.w32(q + 0x34, radius)
            m.w32(q + 0x38, s32(radius * radius))
        elif radius < 0:
            r = m.s32(q + 0x30)
            m.w32(q + 0x34, r)
            m.w32(q + 0x38, s32(r * r))


def hit_part(vm, mask, kind, radius, damage):
    """Op 0C (0x80058AB4): configure hit parts (a melee attack); for the player an attack kind
    counts a melee statistic, any other clears flags A 0x10000000."""
    m, e = vm.m, vm.e
    hit_parts_set(m, e, mask & 0xFFFFFFFF, s32(kind), s32(radius), s32(damage << 16))
    if m.s32(e + 8) == 1:
        if kind in (9, 5, 6):
            vm.eng.note("stat_melee not implemented")
        else:
            m.w32(e + 0xC, m.u32(e + 0xC) & 0xEFFFFFFF)


def hit_part_reset(vm):
    """script_start on channel 0: every part back to its defaults (0x8004C64C(e, -1, -1, -1, -1))."""
    hit_parts_set(vm.m, vm.e, 0xFFFFFFFF, -1, -1, -1)


def remove_self(vm):
    """Op 17 (0x8005926C): entity_remove; the flag 0x80099EC0."""
    vm.eng.remove(vm.e, "remove_self")
    vm.m.w32(0x80099EC0, 1)


def model_variant(vm, v):
    """Op 1C (0x800594AC): the instance's variant (+0x68, which mesh is drawn)."""
    inst = vm.m.u32(vm.e + 0x100)
    if inst:
        vm.m.w32(inst + 0x68, v)


def prop(vm, sel, value, typ, grp, tgt):
    """Op 1F, sop_property (0x80059718): a field of the entity (or of the first of type/group)."""
    m = vm.m
    t = tgt & 0xFFFFFFFF
    if t == 0:
        t = vm.e
        if s32(typ) != -1 and s32(grp) != -1:
            from engine_sense import find_type_group
            t = find_type_group(m, s32(typ << 24) >> 24, s32(grp << 24) >> 24)
    if not t:
        vm.eng.note("property of a missing entity")
        return
    value = s32(value)
    if sel == 1:
        m.w32(t + 0xF4, value)
    elif sel == 2:
        for k in range(3):
            m.w32(t + 0x270 + 4 * k, value)
    elif sel == 3:
        m.w32(t + 0x2D4, value)
    elif sel == 4:
        m.w32(t + 0x294, value >> 16)
        m.w32(t + 0x290, value >> 16)
    elif sel == 5:
        m.w32(t + 0x44, value >> 16)
    elif sel == 6:
        m.w32(t + 0x48, value >> 16)
    elif sel == 19:
        m.w16(t + 0x2E8, value >> 16)
    elif sel == 20:
        m.w16(t + 0x2EC, value >> 16)


def send_event(vm, msg):
    """Op 43 (0x8005C210 -> send_event -> msg_send): a level message with the entity as sender."""
    eng = vm.eng
    eng.logic.send(msg & 0xFFFF, eng.ents.get(vm.e), 0, None, src="send_event")


EFFECTS = {
    "hit_part": hit_part,
    "hit_part_reset": hit_part_reset,
    "remove_self": remove_self,
    "model_variant": model_variant,
    "property": prop,
    "send_event": send_event,
    "sound": sound,
    "root_motion": lambda vm: root_motion(vm),
    "anim": anim,
    "pose_blend": save_pose,
    "behaviour": behaviour,
    "alarm": lambda vm, *a: __import__("engine_alarm").op_alarm(vm, *a),
}


def node_target(eng, e, b, node):
    """brain_node_target (0x80057474) into the brain: the node point +0xA0, the side waypoint
    +0xAC/+0xAE, whether to go by it +0x8C, the claimed node +0xB6 (engine_sense)."""
    from engine_sense import brain_node_target
    m = eng.m

    def write(f, v):
        if f == "node_point":
            for k in range(3):
                m.w16(b + 0xA0 + 2 * k, v[k])
        elif f == "side":
            m.w16(b + 0xAC, v[0])
            m.w16(b + 0xAE, v[1])
        elif f == "via":
            m.w32(b + 0x8C, v)
        elif f == "claimed":
            m.w16(b + 0xB6, v)
    brain_node_target(m, e, write, s32(node))
