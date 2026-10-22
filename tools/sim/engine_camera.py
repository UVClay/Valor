"""The camera entity (docs/re/movement.md, The camera): camera_think (0x800473DC) in its normal mode
0x80 and camera_setup (0x8004913C) with view_lookat (0x80048EEC), which build the view matrix
the player's aim reads (camera object +0x100)."""
import engine_gte as gt
from engine_gte import s16, s32


def camera_think(eng, e):
    m = eng.m
    co = m.u32(e + 0x388)
    sol = eng.resolve(m.u32(co + 0xC))
    if sol:
        if m.u32(co + 0x9C) & 0x800:
            # the death camera: its animation scripts run, its look points from the animation;
            # with no model it falls back to the normal mode
            inst = m.u32(e + 0x100)
            if inst and m.u32(inst + 8):
                from engine_scripts import EVM
                from engine_death import death_points
                vm = EVM(eng, e)
                vm.run_b()
                vm.run_a()
                death_points(eng, e, co)
            else:
                m.w32(co + 0x9C, (m.u32(co + 0x9C) & ~0x800) | 0x80)
        ease(m, co)
        look_points(eng, e, co, sol)
        place(eng, e, co, sol)
    if m.s16(co + 0xCE) > 0:
        a = m.s16(co + 0xCC)
        m.w32(e + 0x210, m.s32(e + 0x210) + (a << 16))
        m.w32(co + 4, m.s32(co + 4) + (a << 16))
        m.w32(co + 0x70, m.s32(co + 0x70) + (a << 16))
        m.w16(co + 0xCC, -(s32((a & 0xFFFF) << 16) >> 17))
        m.w16(co + 0xCE, m.s16(co + 0xCE) - 1)


def ease(m, co):
    """camera_ease (0x80046C64): current points toward their targets, >> obj+0x10."""
    sh = m.s32(co + 0x10)
    for cur, tgt in ((0x48, 0x54), (0x4C, 0x58), (0x50, 0x5C), (0x18, 0x24), (0x1C, 0x28), (0x20, 0x2C),
                     (0x78, 0x84), (0x7C, 0x88), (0x80, 0x8C)):
        a, b = m.s32(co + cur), m.s32(co + tgt)
        if a != b:
            m.w32(co + cur, s32(a + (s32(b - a) >> sh)))


def look_points(eng, e, co, sol):
    """camera_look_points (0x800479D8), modes 0x480: the look point (obj+0x18.. turned by the view
    yaw) and a side point (obj+0x78..), from the soldier's position."""
    m = eng.m
    mode = m.u32(co + 0x9C)
    if mode & 0x800:
        # the eased targets through the camera entity's own matrix
        from engine_death import cam_local
        a = cam_local(m, e, m.s16(co + 0x1A), m.s16(co + 0x1E), m.s16(co + 0x22))
        for k in range(3):
            m.w32(co + 4 * k, s32(a[k] << 16))
        m.w32(co + 0x98, a[1])
        b = cam_local(m, e, m.s16(co + 0x7A), m.s16(co + 0x7E), m.s16(co + 0x82))
        for k in range(3):
            m.w32(co + 0x6C + 4 * k, s32(b[k] << 16))
        return
    if not mode & 0x480:
        eng.note(f"camera look mode {mode:X} not implemented")
        return
    m.w32(co + 0xBC, m.s32(m.u32(sol + 0x388) + 0x184))
    rot = gt.rot_matrix_y(m.s16(co + 0xBE))
    a = gt.rot_trans_sv(rot, (0, 0, 0), (m.s16(co + 0x1A), 0, m.s16(co + 0x22)))
    b = gt.rot_trans_sv(rot, (0, 0, 0), (m.s16(co + 0x7A), 0, m.s16(co + 0x82)))
    sx, sy, sz = (m.s32(sol + 0x20C + 4 * k) for k in range(3))
    m.w32(co + 0, s32(sx + (a[0] << 16)))
    m.w32(co + 8, s32(sz + (a[2] << 16)))
    m.w32(co + 4, s32(sy + m.s32(co + 0x1C)))
    m.w32(co + 0x6C, s32(sx + (b[0] << 16)))
    m.w32(co + 0x74, s32(sz + (b[2] << 16)))
    m.w32(co + 0x70, s32(sy + (m.s32(co + 0x94) << 16)))


def place(eng, e, co, sol):
    """camera_place (0x800482A4), mode 0x80: the eye at the soldier, y + obj+0x94."""
    m = eng.m
    if m.u32(co + 0x9C) & 0x800:
        # the eye: the eased joint through the camera's matrix, kept 85 above its floor
        from engine_death import cam_local
        ly = m.s16(co + 0x4E)
        m.w32(co + 0x94, ly)
        p = cam_local(m, e, m.s16(co + 0x4A), ly, m.s16(co + 0x52))
        m.w32(co + 0x3C, s32(p[0] << 16))
        m.w32(co + 0x44, s32(p[2] << 16))
        floor = m.s32(e + 0x208)
        m.w32(co + 0x40, s32(floor - 0x550000) if (floor >> 16) - p[1] < 0x55 else s32(p[1] << 16))
        return
    if m.u32(co + 0x9C) & 0x80:
        m.w32(e + 0x20C, m.s32(sol + 0x20C))
        m.w32(e + 0x210, s32(m.s32(sol + 0x210) + (m.s32(co + 0x94) << 16)))
        m.w32(e + 0x214, m.s32(sol + 0x214))
    else:
        eng.note("camera place mode not implemented")


def camera_setup(eng, e):
    """camera_setup (0x8004913C): the look and eye points into obj+0xE4 and view_lookat; the
    frustum (drawing only) is left out."""
    m = eng.m
    co = m.u32(e + 0x388)
    look = [m.s16(co + 2), m.s16(co + 6), m.s16(co + 0xA)]
    if m.u32(co + 0x9C) & 0x1800:
        # the death modes: the eye camera_place put at co+0x3C
        eye = [m.s16(co + 0x3E), m.s16(co + 0x42), m.s16(co + 0x46)]
    else:
        eye = [m.s16(e + 0x20E), m.s16(e + 0x212), m.s16(e + 0x216)]
    side = [m.s16(co + 0x6E), m.s16(co + 0x72), m.s16(co + 0x76)]
    for k in range(3):
        m.w32(co + 0xE4 + 4 * k, look[k])
        m.w32(co + 0xF0 + 4 * k, eye[k])
    view_lookat(m, co, look, eye, side)


def view_lookat(m, co, look, eye, side):
    """view_lookat (0x80048EEC): rows = right, up (x 5/8), forward, each x 2; t = M . -eye."""
    d = [s32(look[k] - eye[k]) for k in range(3)]
    sv = [s32(side[k] - eye[k]) for k in range(3)]
    s_n = gt.vector_normal(sv)
    f_n = gt.vector_normal(d)
    up = gt.outer_product12(f_n, s_n)
    right = gt.outer_product12(up, f_n)
    M = [[s16(right[k] * 2) for k in range(3)],
         [s16(_cdiv(up[k] * 5, 8) * 2) for k in range(3)],
         [s16(f_n[k] * 2) for k in range(3)]]
    base = co + 0x100
    for i in range(3):
        for j in range(3):
            m.w16(base + 6 * i + 2 * j, M[i][j])
    t = gt.apply_matrix_lv(M, [-eye[0], -eye[1], -eye[2]])
    for k in range(3):
        m.w32(base + 0x14 + 4 * k, t[k])


def _cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def view_matrix(m, co):
    base = co + 0x100
    M = [[m.s16(base + 6 * i + 2 * j) for j in range(3)] for i in range(3)]
    t = [m.s32(base + 0x14 + 4 * k) for k in range(3)]
    return M, t


def view_pitch_target(m, sol, inv, co):
    """view_pitch_target (0x8007BADC): the look point's height target obj+0x28 = eye + 2 -
    d tan(pitch), d = the look offset's cheap length."""
    a, c = abs(m.s16(co + 0x26)), abs(m.s16(co + 0x2E))
    d = a + c - (min(a, c) >> 1)
    p = m.s16(inv + 0x17A)
    num = -(d * gt.rsin(p))
    v = _cdiv(num, gt.rcos(p)) + m.s32(co + 0x98)
    m.w32(co + 0x28, s32(v << 16))
