"""The player's soldier and the camera (docs/re/movement.md, player.md, combat.md Aiming)."""
import move_sim as ms
from engine import s16, s32, G_SOLDIER, G_PLAYER

PLAYER_UPDATE = 0x8007A6B0
CAMERA_THINK = 0x800473DC
WORLD_THINK = 0x80053C00
FADE_THINK = 0x8004F488

# actions (the controller's action table 0x800B7790: port * 9 + action)
FIRE, JUMP, SWITCH, CROUCH, RELOAD, USE, A6, CENTER, AIM = range(9)


def register(eng):
    eng.think[PLAYER_UPDATE] = lambda e: player_update(eng, e)
    import engine_camera
    eng.think[CAMERA_THINK] = lambda e: engine_camera.camera_think(eng, e)
    eng.think[WORLD_THINK] = lambda e: None      # colour cycles: drawing only (objects.md)
    eng.think[FADE_THINK] = lambda e: fade_think(eng, e)


def fade_think(eng, e):
    """A fade (hud.md, Fades): delay, duration, hold, then removed. Drawing only, but it is an
    entity in the list."""
    m = eng.m
    o = m.u32(e + 0x388)       # +0x4 delay, +0x8 duration, +0xC hold (ticks left)
    for off in (4, 8):
        if m.s32(o + off) > 0:
            m.w32(o + off, m.s32(o + off) - 1)
            return
    if m.s32(o + 0xC) > 0:
        m.w32(o + 0xC, m.s32(o + 0xC) - 1)
        return
    eng.remove(e, "fade done")


# ---------------------------------------------------------------- the soldier

G_WEAPONS = 0x80093640
G_SLOT_WEAPON = 0x8009E878
G_SLOT_COUNT = 0x8009E874
G_CHEATS = 0x800386A4
G_SUBDIV = 0x800AA168


def weapon_flags(m, w):
    return m.u16(G_WEAPONS + 0x44 * w + 0x2A)


def get_var(m, e, v):
    return m.s32(e + 0x4C + 4 * v)


def set_var(m, e, v, x):
    m.w32(e + 0x4C + 4 * v, x)


def player_update(eng, e):
    """player_update (0x8007A6B0), as its code runs (movement.md, Order of a tick)."""
    m = eng.m
    inv = m.u32(e + 0x388)
    if m.s32(e + 0x210) > 0x13880000:
        eng.note("player fell out of the world (10000 damage) not implemented")
    if m.u32(e + 0x38) & 0x4000:
        eng.note("player death not implemented")
    else:
        # aim mode: R2 held on the ground
        aimed = False
        if m.s32(e + 0x284):
            if get_var(m, e, 3) & 2:
                m.w32(e + 0x38, m.u32(e + 0x38) | 2)
            if m.s32(e + 0x284) and eng.action_held(AIM):
                aimed = True
                f = m.u32(inv) | 0x20
                m.w32(inv, f)
                if m.u8(inv + 0x55) == 5:
                    m.w32(inv, (f & 0xFFDFFFFF) | 0x140)
                    m.w32(inv + 0xA8, 0x98F)
                elif f & 0x400000 and m.s16(G_WEAPONS + 0x44 * m.u8(inv + 0x55) + 0x28):
                    m.w32(inv, (f & ~0x40) | 0x200000)
                    m.w32(inv + 0xA8, 0x1DB)
        if not aimed:
            m.w32(inv, m.u32(inv) & ~0x20 & ~0x40 & 0xFFDFFFFF)
        m.w32(inv + 0x1C8, player_stance(m, inv))
        sc = m.s16(e + 0x11E)
        if sc in (2, 9, 10, 13, 16) or (m.s32(inv + 0x48) > 0 and sc == 8):
            m.w32(inv, m.u32(inv) | 0x800000)
        else:
            m.w32(inv, m.u32(inv) & 0xFF7FFFFF)
        if m.s32(inv + 0x13C) == 0 and m.s32(inv + 0x124) == 0 and eng.action_pressed(SWITCH):
            eng.note("weapon_holster not implemented")
        if get_var(m, e, 0x11):
            eng.note("weapon draw (variable 0x11) not implemented")
            set_var(m, e, 0x11, 0)
        else:
            slot = m.s8(inv + 0x54)
            w = m.u8(G_SLOT_WEAPON + slot)
            if w in (3, 10) and m.s16(inv + 0x66 + 2 * slot) == 0 and m.s16(inv + 0x56 + 2 * slot) == 0:
                eng.note("weapon draw (empty grenades) not implemented")
        if eng.action_pressed(USE):
            eng.zones_use(e)
            slot = m.s8(inv + 0x54)
            if m.s32(inv + 0x13C) == 0 and not weapon_flags(m, m.u8(G_SLOT_WEAPON + slot)) & 1 \
                    and weapon_reload(eng, e, inv) == 0:
                set_var(m, e, 0x10, 1)
        # recoil kick
        k = s32(m.s32(inv + 0x4C) + m.s32(inv + 0x50))
        k = min(k, 0xF0000)
        if k > 0:
            k -= 0x7FFF
        elif k < -0x2000:
            k += 0x1FFF
        else:
            k = 0
        m.w32(inv + 0x4C, k)
        c = m.s32(inv + 0x50)
        m.w32(inv + 0x50, c - 0x2000 if c >= 0x2001 else 0)
        m.w32(inv, m.u32(inv) & 0xFFFBFFFF)
        # the trigger
        w = m.u8(inv + 0x55)
        if w in (3, 10):
            if m.s32(inv + 0x48) > 0:
                m.w32(inv + 0x48, m.s32(inv + 0x48) - 1)
                m.w32(inv + 0x128, 0)
            elif m.s32(inv + 0x124) == 0:
                held = int(eng.action_held(FIRE))
                if held:
                    if m.s32(inv + 0x128):
                        if m.s32(inv + 0x11C) < 0x1E:
                            m.w32(inv + 0x11C, m.s32(inv + 0x11C) + 1)
                    else:
                        m.w32(inv + 0x11C, 0)
                elif m.s32(inv + 0x128):
                    m.w32(e + 0x3C, m.u32(e + 0x3C) | 2)
                m.w32(inv + 0x128, held)
        else:
            if m.s32(inv + 0x48) > 0:
                m.w32(inv + 0x48, m.s32(inv + 0x48) - 1)
            else:
                fire = eng.action_held(FIRE) if m.s32(inv + 0x44) > 0 else eng.action_pressed(FIRE)
                if fire:
                    m.w32(e + 0x3C, m.u32(e + 0x3C) | 2)
        # jump
        if not m.u32(inv) & 0x20 and m.s32(e + 0x284) and eng.action_pressed(JUMP) and not m.u32(inv) & 2:
            m.w32(e + 0x284, 0)
            m.w32(e + 0x21C, -0x68000 if m.u32(inv) & 0x10000000 else -0x1A0000)
        crouch(eng, e, inv)
        if m.s32(inv + 0x70) and m.u32(inv) & 2:
            player_stand(eng, e, inv)
        controls(eng, e, inv)
        if m.s32(inv + 0x70) == 0:
            m.w32(e + 0xC, m.u32(e + 0xC) | 0x20)
        if m.u32(inv) & 0x20 or (m.s32(e + 0x200) == 0 and m.s32(e + 0x218) == 0):
            set_var(m, e, 2, 0)
            m.w32(e + 0x3C, m.u32(e + 0x3C) | 0x80)
        else:
            set_var(m, e, 2, 1)
            m.w32(e + 0x3C, m.u32(e + 0x3C) | 0x40)
        slot = m.s8(inv + 0x54)
        if not weapon_flags(m, m.u8(G_SLOT_WEAPON + slot)) & 1:
            if m.s16(inv + 0x66 + 2 * slot) == 0 and m.s32(inv + 0x48) <= 0 and m.s16(inv + 0x56 + 2 * slot) != 0:
                weapon_reload(eng, e, inv)
        c = m.s32(inv + 0x38)
        if c:
            m.w32(inv + 0x38, c - 1)
            eng.note("player melee part (inv+0x38) not implemented")
        elif m.u32(m.u32(e + 0x114)) & 8 if m.u32(e + 0x114) else False:
            if m.s16(m.u32(e + 0x100) + 0x5E) == 0:
                m.w32(inv + 0x38, 4)
                eng.note("player melee part (0x8004C64C) not implemented")
        if m.u16(inv + 0x154):
            if m.u32(inv) & 2:
                m.w16(inv + 0x154, 0)
            else:
                for k in range(3):
                    m.w32(e + 0x1F0 + 4 * k, s32(m.s32(e + 0x1F0 + 4 * k) - (m.s32(e + 0x218 + 4 * k) >> 2)
                                                 + m.s32(inv + 0x80 + 4 * k)))
                if not m.u16(inv + 0x154) & 1:
                    for o in (0x24C, 0x254, 0x250):
                        m.w32(e + o, m.s32(e + o) >> 1)
                m.w16(inv + 0x154, m.u16(inv + 0x154) - 1)
        if m.u32(inv) & 0x200040:
            if m.u32(inv + 0xA4) < m.u32(inv + 0xA8):
                if m.u32(inv + 0xAC) < 0x40:
                    m.w32(inv + 0xAC, m.u32(inv + 0xAC) + 2)
                m.w32(inv + 0xA4, m.u32(inv + 0xA4) + m.u32(inv + 0xAC))
                m.w32(G_SUBDIV, m.s32(G_SUBDIV) + m.u32(inv + 0xAC))
        else:
            m.w32(inv + 0xA4, 0x190)
            m.w32(G_SUBDIV, 0x12C if m.u32(inv) & 2 else 0xFA)
            m.w32(inv, m.u32(inv) & ~0x100)
        if m.u32(inv + 0xA4) == 0x190:
            m.w32(inv + 0xAC, 1)
    from engine_scripts import EVM
    vm = EVM(eng, e)
    vm.run_b()
    if m.s32(e + 0x288) < 0:
        m.w32(e + 0x288, 1)
        m.w32(e + 0x28C, 1)
        m.w32(e + 0x22C, s32(m.s32(e + 0x22C) + 0x08000000))
    vm.run_a()
    lad = m.s32(inv + 0x70)
    m.w32(inv + 4, m.u32(inv))
    if lad:
        m.w32(inv + 0x74, lad)
        m.w32(inv + 0x70, 0)
    elif m.s32(inv + 0x74):
        m.w32(e + 0x21C, 0)
        m.w32(inv + 0x74, 0)
    m.w32(inv + 0x8C, m.s32(e + 0x284))
    m.w32(inv, m.u32(inv) & ~0xE00)


def player_stance(m, inv):
    """player_stance (0x8007BA20)."""
    w = m.u8(inv + 0x55)
    if m.u32(inv) & 0x20:
        return {0: 0, 4: 0, 8: 0, 9: 0, 12: 0, 1: 1, 6: 1, 11: 1, 5: 2, 2: 5, 3: 6, 7: 6, 10: 6}.get(w, 7)
    return {1: 8, 6: 8, 11: 8, 2: 9, 3: 9, 7: 9, 10: 9}.get(w, 7)


def crouch_target(m, inv):
    if m.s32(0x800939F4) == 5 and m.s32(0x800939F8) == 4:
        m.w32(inv + 0x3C, -0x96)
        return
    m.w32(inv + 0x3C, -0x50)
    if m.s32(G_SUBDIV) < 0x12C:
        m.w32(G_SUBDIV, 0x12C)
    if m.s32(0x8009E830) == 0:
        if m.s32(0x800AA164) < 2:
            m.w32(0x800AA164, 2)
        m.w32(0x8009E830, 1)


def stand_target(m, inv):
    if m.s32(G_SUBDIV) >= 0xFB:
        m.w32(G_SUBDIV, 0xFA)
    if m.s32(0x8009E830):
        if m.s32(0x800AA164) >= 2:
            m.w32(0x800AA164, 1)
        m.w32(0x8009E830, 0)
    m.w32(inv + 0x3C, -0xF0)


def player_stand(eng, e, inv):
    """player_stand (0x80079718): the roles' speeds restored (they are inputs here)."""
    m = eng.m
    m.w32(inv, m.u32(inv) & ~2 & ~0x10)
    m.w32(e + 0x2D4, m.s32(inv + 0x34))
    m.w16(e + 0x2DA, -0x64)
    if m.u32(inv) & 2:
        crouch_target(m, inv)
    else:
        stand_target(m, inv)


def player_crouch_done(eng, e, inv):
    """player_crouch_done (0x800798AC)."""
    m = eng.m
    m.w32(inv, (m.u32(inv) | 2) & ~0x10)
    if eng.handle_dead(m.u32(e + 0x2C)):
        m.w32(e + 0x2D4, 0x500000)
        m.w16(e + 0x2DA, 0)
    if m.u32(inv) & 2:
        crouch_target(m, inv)
    else:
        stand_target(m, inv)


def crouch(eng, e, inv):
    """Action 3 toggles (movement.md, Crouching); the camera's eye offset eases toward the target
    inv+0x3C and the look points with it; at the target, player_crouch_done."""
    m = eng.m
    co = m.u32(m.u32(G_PLAYER) + 0x388)
    if eng.action_pressed(CROUCH):
        f = m.u32(inv)
        if not f & 2:
            m.w32(inv, f | 2)
            crouch_target(m, inv)
        elif not m.u32(e + 0x10) & 0x800:
            player_stand(eng, e, inv)
    cur, tgt = m.s32(co + 0x94), m.s32(inv + 0x3C)
    if cur != tgt:
        if tgt + 0x12 < cur:
            step = -0x14
        else:
            step = 0x14
            if cur >= tgt - 0x12:
                if m.u32(inv) & 2 and (m.s32(e + 0x200) or m.s32(e + 0x218) or m.s32(e + 0x238)):
                    step = cur - tgt
                else:
                    step = tgt - cur
        cur = s32(cur + step)
        m.w32(co + 0x94, cur)
        m.w32(co + 0x98, s32(m.s32(co + 0x98) + step))
        m.w32(co + 0x4C, s32(cur << 16))
        m.w32(co + 0x58, s32(cur << 16))
        m.w32(co + 0x1C, s32(m.s32(co + 0x1C) + (step << 16)))
        m.w32(co + 0x28, s32(m.s32(co + 0x28) + (step << 16)))
        if cur == tgt and m.u32(inv) & 2:
            player_crouch_done(eng, e, inv)
    elif m.u32(inv) & 2 and eng.handle_dead(m.u32(e + 0x2C)):
        m.w32(e + 0x2D4, 0x500000)
        m.w16(e + 0x2DA, 0)


def weapon_reload(eng, e, inv):
    """weapon_reload (0x800460F4): the rounds move at once, script 9 starts; 0 if nothing done."""
    m = eng.m
    slot = m.s8(inv + 0x54)
    w = m.u8(inv + 0x55)
    clip = m.u16(G_WEAPONS + 0x44 * w + 0xA)
    a = inv + 2 * slot
    have = m.u16(a + 0x66)
    need = s16(clip - have)
    if need <= 0:
        return 1
    if m.u32(G_CHEATS) & 0x40:
        m.w16(a + 0x66, clip)
    else:
        res = m.s16(a + 0x56)
        if need < res:
            m.w16(a + 0x66, clip)
            m.w16(a + 0x56, res - need)
        elif res <= 0:
            return int(s16(have) > 0)
        else:
            m.w16(a + 0x66, have + res)
            m.w16(a + 0x56, 0)
    m.w32(0x80039CBC, m.s32(0x80039CBC) + 1)
    if w in (3, 10):
        if m.s16(a + 0x66):
            set_var(m, e, 0x10, 0)
    elif m.s32(inv + 0x13C) == 0:
        from engine_scripts import EVM
        EVM(eng, e).script_start(9, 1, 0)
        snd = m.s16(G_WEAPONS + 0x44 * w + 0x20)
        if snd >= 0:
            bank = m.s16(G_WEAPONS + 0x44 * w + 0x22)
            if bank < 0:
                bank = m.u16(m.u32(m.u32(e + 0x100) + 8) + 0xC8)
            eng.audio.play(bank, snd, e, 0x20000, 0, 0x7FFF, 0, 0, 0, 0, (weapon_flags(m, w) >> 3) & 2)
    return 1


def controls(eng, e, inv):
    """player_controls (0x8007F300)."""
    m = eng.m
    co = m.u32(m.u32(G_PLAYER) + 0x388)
    f0 = m.u32(inv)
    m.w32(inv, f0 & ~0xC)
    if not f0 & 0x20:
        if m.u32(inv + 4) & 0x20:
            for k in range(3):
                m.w32(e + 0x20C + 4 * k, m.s32(inv + 0x90 + 4 * k))
            m.w32(inv + 0x9C, 0)
            m.w32(e + 0xC, m.u32(e + 0xC) | 0x20)
            m.w32(e + 0x10, m.u32(e + 0x10) & ~0x10)
        climbed = 0
        if m.s32(inv + 0x70):
            eng.note("ladder_climb not implemented")
        if not climbed:
            m.w32(e + 0x200, -eng.role(0))
        m.w32(e + 0x218, eng.role(1))
    else:
        if not m.u32(inv + 4) & 0x20:
            for k in range(3):
                m.w32(inv + 0x90 + 4 * k, m.s32(e + 0x20C + 4 * k))
            m.w32(e + 0xC, m.u32(e + 0xC) & ~0x20)
            m.w32(e + 0x10, m.u32(e + 0x10) | 0x10)
        m.w32(e + 0x200, 0)
        r = eng.role(1)
        m.w32(e + 0x1F0, s32(m.s32(e + 0x1F0) + m.s32(inv + 0x90) - m.s32(e + 0x20C)))
        m.w32(e + 0x1F4, 0)
        m.w32(e + 0x210, m.s32(inv + 0x94))
        m.w32(e + 0x1F8, s32(m.s32(e + 0x1F8) + m.s32(inv + 0x98) - m.s32(e + 0x214)))
        v = s32(r * 2 + m.s32(inv + 0x9C) * 3) >> 2
        m.w32(e + 0x218, v)
        m.w32(inv + 0x9C, v)
    stance = m.u32(inv + 0x1C8)
    if stance < 2:
        eng.note("aiming stances (crosshair box) not implemented")
    if stance != m.u32(inv + 0x1CC):
        m.w32(inv + 0x188, 0)
        m.w32(inv, m.u32(inv) & 0xFFEFFFFF & 0x7FFFFFFF)
        m.w32(e + 0x238, 0)
        m.w32(inv + 0x17C, 0)
        m.w32(e + 0x234, 0)
        if stance == 5:
            m.w16(inv + 0x190, 0x10E)
            m.w16(inv + 0x192, 0x73)
        else:
            m.w16(inv + 0x190, 0x100)
            m.w16(inv + 0x192, 0x83)
    t = scale_look(m, inv, eng.role(3))
    r = (m.s32(inv + 0x188) * 3 + t) >> 2 if ((t + 0x10000) & 0xFFFFFFFF) > 0x20000 else (m.s32(inv + 0x188) + 1) >> 4
    m.w32(inv + 0x188, s32(r))
    m.w32(inv + 0x184, s32(m.s32(inv + 0x184) + t))
    mode = m.u8(0x8009E3A0)
    if mode == 1:
        if eng.action_held(CENTER):
            p = m.s32(inv + 0x178)
            if abs(p >> 2) <= 0x27FFFF:
                m.w32(inv + 0x178, s32(p * 3 + 1) >> 2)
            else:
                m.w32(inv + 0x178, s32(p * 7 + 1) >> 3)
        else:
            pin = eng.role(2)
            if pin == 0 and m.u32(inv) & 0x20:
                eng.note("role_axis_alt (pitch from the forward stick) not implemented")
            pin = scale_look(m, inv, pin)
            if pin:
                p = s32(m.s32(inv + 0x178) - (pin >> 1))
                p = min(p, 0x02AA0000)
                p = max(p, -0x02AA0000)
                m.w32(inv + 0x178, p)
    else:
        eng.note(f"look mode {mode} not implemented")
    if 0 <= stance - 7 < 2:
        from engine_aim import aim_update
        aim_update(eng, e, inv)
    else:
        m.w32(e + 0x228, m.s32(inv + 0x178))
        m.w32(e + 0x22C, m.s32(inv + 0x184))
    from engine_camera import view_pitch_target
    view_pitch_target(m, e, inv, co)
    m.w32(inv + 0x1CC, stance)


def scale_look(m, inv, v):
    """The look input's scaling outside the aiming stances: the scope / 8, else the aim friction
    (inv 0x100000: / 2, with bit 31: / 4)."""
    f = m.u32(inv)
    if f & 0x100:
        return v >> 3
    if m.u32(inv + 0x1C8) < 2:
        v = (v >> 1) - (v >> 3) if f & 0x80000 else (v >> 1) + (v >> 4)
        if f & 0x100000:
            v = v >> 2 if f & 0x80000000 else v >> 1
        return v
    if f & 0x100000:
        return v >> 2 if f & 0x80000000 else v >> 1
    return v


def move(eng, e):
    """entity_move for the player (move_sim.py's rules), with player_world_contact's landing."""
    m = eng.m
    st = dict(pos=[m.s32(e + 0x20C + 4 * k) for k in range(3)], vy=m.s32(e + 0x21C), fwd=m.s32(e + 0x200),
              strafe=m.s32(e + 0x218), yaw=m.s32(e + 0x3CC + 0x184), ground=m.s32(e + 0x284),
              floor=m.s32(e + 0x208), old=[m.s32(e + 0x3BC + 4 * k) for k in range(3)],
              radius=m.s16(e + 0x2EC), eye=m.s16(e + 0x2D6), flags_b=m.u32(e + 0x10),
              push=[m.s32(e + 0x1F0 + 4 * k) for k in range(3)], gravity=bool(m.u32(e + 0xC) & 0x20))
    for k in range(3):
        m.w32(e + 0x258 + 4 * k, st["pos"][k])
        m.w32(e + 0x264 + 4 * k, m.s32(e + 0x228 + 4 * k))
        m.w32(e + 0x228 + 4 * k, s32(m.s32(e + 0x228 + 4 * k) + m.s32(e + 0x234 + 4 * k)))
    ms.entity_move(eng.world.w, st)
    for k in range(3):
        m.w32(e + 0x20C + 4 * k, st["pos"][k])
        m.w32(e + 0x354 + 4 * k, s16(st["pos"][k] >> 16))
        m.w32(e + 0x3BC + 4 * k, st["old"][k])
        m.w32(e + 0x1F0 + 4 * k, 0)
    m.w32(e + 0x21C, st["vy"])
    m.w32(e + 0x284, st["ground"])
    m.w32(e + 0x208, st["floor"])
    if "zone" in st:
        m.w32(e + 0x390, st["zone"])
    if st.get("strafe_cleared"):
        m.w32(e + 0x218, 0)
        m.w32(e + 0x220, 0)
    if st.get("landed"):
        # player_world_contact (0x8007B4B8): on landing the whole velocity is zeroed
        for o in (0x200, 0x218, 0x21C, 0x220):
            m.w32(e + o, 0)




def player_weapon_update(eng, e):
    """player_weapon_update (0x8007B350): throw, papers, or fire_gun with the fire variable."""
    m = eng.m
    inv = m.u32(e + 0x388)
    if not m.u32(e + 0x38) & 0x4000:
        w = m.u8(inv + 0x55)
        if w in (3, 10):
            if get_var(m, e, 1) & 0xFF or m.s32(inv + 0x124):
                eng.note("throw_weapon not implemented")
        elif w == 7:
            if get_var(m, e, 1):
                d = m.s32(inv + 0x40)
                m.w32(inv + 0x44, d)
                m.w32(inv + 0x48, abs(d))
        else:
            slot = m.s8(inv + 0x54)
            if not weapon_flags(m, m.u8(G_SLOT_WEAPON + slot)) & 1:
                from engine_combat import fire_gun
                fire_gun(eng, e, inv, get_var(m, e, 1) & 0xFF)
    set_var(m, e, 1, 0)
