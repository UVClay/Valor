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
    eng.think[CAMERA_THINK] = lambda e: camera_think(eng, e)
    eng.think[WORLD_THINK] = lambda e: None      # colour cycles: drawing only (objects.md)
    eng.think[FADE_THINK] = lambda e: fade_think(eng, e)


# ---------------------------------------------------------------- the camera

def camera_think(eng, e):
    """camera_think (0x800473DC), its normal mode (0x80): the eye at the soldier's x, z and its
    feet + the eye offset camera_obj+0x94, from the soldier's state at this point of the tick
    (the end of the last: the camera updates first); the shake."""
    m = eng.m
    co = m.u32(e + 0x388)
    sol = m.u32(G_SOLDIER)
    mode = m.u32(co + 0x9C)
    if mode != 0x80:
        eng.note("camera mode not implemented", hex(mode))
        return
    off = m.s32(co + 0x94)
    x, y, z = (m.s32(sol + 0x20C + 4 * k) for k in range(3))
    y = s32(y + (off << 16))
    # shake (camera_shake 0x80046C50): y += amplitude, amplitude = -amplitude / 2, count - 1
    m.w32(e + 0x20C, x)
    m.w32(e + 0x210, y)
    m.w32(e + 0x214, z)


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

def player_update(eng, e):
    """player_update (0x8007A6B0), in movement.md's order (Order of a tick)."""
    m = eng.m
    inv = e + 0x3CC
    flags = m.u32(inv)
    ground = m.s32(e + 0x284)
    # 2. fallen out of the world
    if s16(m.s32(e + 0x210) >> 16) > 5000:
        eng.note("player fell out of the world")
    # 3. dying
    if m.u32(e + 0x38) & 0x4000:
        eng.note("player death not implemented")
    # 4. aim mode (R2 held, on the ground)
    if eng.action_held(AIM) and ground:
        eng.note("aim mode not implemented")
    # 9. jump: pressed, on the ground, not crouched, not aiming
    if eng.action_pressed(JUMP) and ground and not flags & 2 and not flags & 0x20:
        m.w32(e + 0x284, 0)
        m.w32(e + 0x21C, -26 << 16)
    # 10. crouch toggle and the eye's easing
    crouch(eng, e, inv)
    # 12. player_controls: the movement and look roles
    controls(eng, e, inv)
    # 13. gravity
    m.w32(e + 0xC, m.u32(e + 0xC) | 0x20)
    # 14. moving -> script variable 2
    moving = m.s32(e + 0x200) != 0 or m.s32(e + 0x218) != 0
    m.w32(e + 0x4C + 4 * 2, int(moving))
    # 18. the soldier's model scripts (the first-person weapon's animation)
    from engine_scripts import EVM
    EVM(eng, e).think_scripts()
    # 20.
    m.w32(inv + 0x8C, m.s32(e + 0x284))
    # 21. the move, the world contact, the triggers, the weapon: entities_update (engine.py)


def player_weapon_update(eng, e):
    """player_weapon_update (0x8007B350): firing and throwing (player.md, Weapons in play)."""
    pass


def crouch(eng, e, inv):
    """Action 3 toggles (movement.md, Crouching); the camera's eye offset eases 20 per tick toward
    the target inv+0x3C; at the target, player_crouch_done."""
    m = eng.m
    flags = m.u32(inv)
    if eng.action_pressed(CROUCH):
        if not flags & 2:
            m.w32(inv, flags | 2)
            m.w32(inv + 0x3C, -80)
        elif not m.u32(e + 0x10) & 0x800:
            m.w32(inv, flags & ~2)
            m.w32(inv + 0x3C, -240)
            eng.note("player_stand: speeds restored (roles are inputs)")
            m.w32(e + 0x2D4, m.s32(inv + 0x34))
    co = m.u32(m.u32(G_PLAYER) + 0x388)
    cur, tgt = m.s32(co + 0x94), m.s32(inv + 0x3C)
    if cur != tgt:
        cur = cur + 20 if cur < tgt else cur - 20
        if abs(cur - tgt) < 20:
            cur = tgt
        m.w32(co + 0x94, cur)
        if cur == tgt and tgt == -80:
            m.w32(e + 0x2D4, 80 << 16)


def controls(eng, e, inv):
    """player_controls (0x8007F300), not aiming: forward = -role 0, strafe = role 1, the turn added
    to the view yaw, the pitch by the look mode (1: pitch -= role 3 / 2, clamped to +-682)."""
    m = eng.m
    fwd, strafe, pitch_in, turn = (eng.role(r) for r in range(4))
    m.w32(e + 0x200, -fwd)
    m.w32(e + 0x218, strafe)
    yaw = s32(m.s32(inv + 0x184) + turn)
    m.w32(inv + 0x184, yaw)
    mode = m.u8(0x8009E3A0)
    p = m.s32(inv + 0x178)
    if mode == 1:
        p = p - (pitch_in >> 1)
        p = max(-682 << 16, min(682 << 16, p))
    else:
        eng.note(f"look mode {mode} not implemented")
    m.w32(inv + 0x178, p)
    # the body follows the view (stances 7, 8: aim_update steers it; not reimplemented yet)
    m.w32(e + 0x228, p)
    m.w32(e + 0x22C, yaw)


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


