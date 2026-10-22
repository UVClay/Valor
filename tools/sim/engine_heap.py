"""The game's heap (docs/re/levels.md, The heap; entities.md, Allocation): the exe's allocator
(0x80036B20 InitHeap, 0x80036C14 malloc, 0x80036DC4 calloc, 0x80036E44 free) run on the seed's
own heap, so every block the engine takes lands where the original's did, and a full heap refuses
as the original's does. Blocks the destructors free two frames late go through the queue at
0x8009F888 (0x8008A4F8), counted down at each frame's start (0x8008A580)."""
from engine import s32

GP = 0x80035D1C
CHECK = 0xB562439F
LATER = 0x8009F888          # count, then {block, frames} pairs


def u(v):
    return v & 0xFFFFFFFF


class Heap:
    def __init__(self, m):
        self.m = m

    def malloc(self, size, tag=0, flags=0):
        """malloc (0x80036C14): first fit from the rover's next, taken from the free block's
        tail; 16-byte headers {blocks, next | tag, 0x80000000 | flags | serial, check}."""
        m = self.m
        if size == 0:
            return 0
        n = ((size + 0xF) >> 4) + 1
        r8 = m.u32(GP + 0x3F24)
        start = r8
        r7 = m.u32(r8 + 4)
        while True:
            r4 = m.u32(r7)
            if r4 >= n:
                break
            back = r7 == start
            r8 = r7
            if back:
                return 0
            r7 = m.u32(r7 + 4)
        if r4 == n:
            if r7 == r8:
                return 0
            nx = m.u32(r7 + 4)
            m.w32(r8 + 4, nx)
            m.w32(r8 + 0xC, u(CHECK - (m.u32(r8) + nx + m.u32(r8 + 8))))
        else:
            nx, fl = m.u32(r7 + 4), m.u32(r7 + 8)
            r4 -= n
            m.w32(r7, r4)
            m.w32(r7 + 0xC, u(CHECK - (r4 + nx + fl)))
            r7 = u(r7 + (r4 << 4))
        m.w32(GP + 0x3F24, r8)
        serial = m.u32(GP + 0x4054)
        f = u(flags | 0x80000000 | serial)
        m.w32(r7 + 0xC, u(CHECK - (n + tag + f)))
        m.w32(r7, n)
        m.w32(r7 + 4, tag)
        m.w32(r7 + 8, f)
        fr = u(m.u32(GP + 0x406C) - n)
        m.w32(GP + 0x406C, fr)
        used = u(m.u32(GP + 0x405C) + n)
        m.w32(GP + 0x405C, used)
        if m.s32(GP + 0x403C) < s32(used):
            m.w32(GP + 0x403C, used)
        if s32(fr) < m.s32(GP + 0x4064):
            m.w32(GP + 0x4064, fr)
        m.w32(GP + 0x4054, u(serial + 0x10000))
        return r7 + 0x10

    def calloc(self, size, count=1):
        m = self.m
        if count != 1:
            size = s32(size * count)
        if size == 0:
            return 0
        p = self.malloc(size)
        if not p:
            return 0
        m.zero(p, (m.u32(p - 0x10) - 1) << 4)
        return p

    def free(self, p):
        """free (0x80036E44): a block whose check holds and that is in use goes back into the
        address-ordered free list, merged with its neighbours; the rover is left before it."""
        m = self.m
        t0 = u(p - 0x10)
        if u(m.u32(t0) + m.u32(t0 + 4) + m.u32(t0 + 8) + m.u32(t0 + 0xC)) != CHECK:
            return
        if m.s32(t0 + 8) >= 0:
            return
        a3 = m.u32(GP + 0x3F24)
        m.w32(t0 + 8, 0)
        size = m.u32(t0)
        m.w32(GP + 0x406C, u(m.u32(GP + 0x406C) + size))
        m.w32(GP + 0x405C, u(m.u32(GP + 0x405C) - size))
        a2 = m.u32(a3 + 4)
        v1 = a3 < t0
        if a3 != a2:
            while (not v1 or t0 >= a2) and (a3 < a2 or (not v1 and t0 >= a2)):
                a3 = a2
                a2 = m.u32(a3 + 4)
                v1 = a3 < t0
        ts = m.u32(t0)
        if u(t0 + (ts << 4)) == a2:
            m.w32(t0, ts + m.u32(a2))
            m.w32(t0 + 4, m.u32(a2 + 4))
            if a3 == a2:
                a3 = t0
        else:
            m.w32(t0 + 4, a2)
        a_s = m.u32(a3)
        if u(a3 + (a_s << 4)) == t0:
            m.w32(a3, a_s + m.u32(t0))
            nx = m.u32(t0 + 4)
            t0 = a3
            m.w32(a3 + 4, nx)
        else:
            m.w32(a3 + 4, t0)
            m.w32(a3 + 0xC, u(CHECK - (a_s + t0 + m.u32(a3 + 8))))
        m.w32(GP + 0x3F24, a3)
        m.w32(t0 + 0xC, u(CHECK - (m.u32(t0) + m.u32(t0 + 4) + m.u32(t0 + 8))))

    def free_later(self, p, frames):
        """0x8008A4F8: freed after that many frames (0: now)."""
        m = self.m
        if frames == 0:
            self.free(p)
            return
        n = m.s32(LATER)
        m.w32(LATER + 4 + 8 * n, p)
        m.w32(LATER + 8 + 8 * n, frames)
        m.w32(LATER, n + 1)

    def frame(self):
        """0x8008A580, at each frame's start: every entry counted down; at 0 freed, the last
        entry moved into its place (and not looked at again this frame)."""
        m = self.m
        i = 0
        while i < m.s32(LATER):
            e = LATER + 4 + 8 * i
            c = m.s32(e + 4) - 1
            m.w32(e + 4, c)
            if c <= 0:
                self.free(m.u32(e))
                n = m.s32(LATER) - 1
                m.w32(e, m.u32(LATER + 4 + 8 * n))
                m.w32(LATER, n)
                m.w32(e + 4, m.u32(LATER + 8 + 8 * n))
            i += 1

    def blocks(self):
        """Every block from the heap's base: (address, bytes, in use, serial)."""
        m = self.m
        a = m.u32(GP + 0x3F28)
        end = m.u32(GP + 0x3F2C)
        out = []
        while a < end:
            n = m.u32(a)
            if not n:
                break
            f = m.u32(a + 8)
            out.append((a, n << 4, bool(f & 0x80000000), (f >> 16) & 0x7FFF))
            a += n << 4
        return out


def pose_size(m, model, flags, extra_parts):
    """model_instance_init (0x8006A96C)'s one block: vertices, colours, the pose's quaternion
    arrays and joints, as the model and the instance flags ask."""
    size = 0
    if not flags & 1 and m.u32(model + 0x1C):
        if m.u32(model + 0x2C):
            size = (m.u32(m.u32(model + 0x2C)) << 4) + 0x14
        if m.u32(model + 0x30):
            size += 8 + (m.u32(m.u32(model + 0x30)) << 2)
        if m.u32(model + 0x34):
            size += 0x14 + (m.u32(m.u32(model + 0x34)) << 4)
    if not flags & 2:
        size += m.u16(model + 0x8C) << 3
    if not m.u32(model + 0x14):
        if not flags & 0x40:
            size += m.u16(model + 0x8C) << 3
        if not flags & 0x100:
            size += m.u16(model + 0x8C) << 2
    if not flags & 4:
        size += m.u16(model + 0x8E) << 3
    if not flags & 0x10:
        size += 0x1000
    if extra_parts:
        # model 0x42A7A5EF loaded: room for an instance per hit part
        size += m.u16(model + 0xBE) * 0xB0
    if m.u32(model + 8):
        size += (m.u8(m.u32(m.u32(model + 8) + 4) + 1) << 3) * 3
    return size


def model_index(m, h):
    """model_find: the index of the model with that hash, -1 if none."""
    for i in range(m.u32(0x8009EC54)):
        if m.u32(m.u32(0x8009EC58) + 0x100 * i) == h:
            return i
    return -1


def instance_init(eng, inst, model, flags, e):
    """model_instance_init (0x8006A96C): the instance's one block (pose_size: characters' from the
    pose pool, others malloc'd, not cleared) laid out as vertices, colours, normals' table,
    the three quaternion/joint arrays; the model's scale, eye height and hit parts on the entity.
    Returns 0 when the block can't be had."""
    m = eng.m
    m.w32(inst + 0x6C, 0x80)
    m.w32(inst, 0)
    m.w32(inst + 0x68, 0)
    m.w32(inst + 4, flags)
    m.w32(inst + 8, model)
    size = pose_size(m, model, flags, model_index(m, 0x42A7A5EF) >= 0)
    if not m.u32(model + 8):
        for o in (0x74, 0x7C, 0x84, 0x88, 0x90, 0x78, 0x80, 0x8C, 0x94):
            m.w32(inst + o, 0)
        m.w16(inst + 0x70, 0)
        m.w16(inst + 0x72, 0)
    buf = 0
    if size > 0:
        if m.s32(model + 0xFC) in (0x1402, 0x1405):
            from engine_enemy import pose_alloc
            buf = pose_alloc(eng)
        else:
            buf = eng.heap.malloc(size)
        if not buf:
            return 0
        m.w32(inst, buf)
    s1 = buf
    if m.u32(model + 8):
        tbl = m.u32(model + 8)
        m.w16(inst + 0x70, m.u16(tbl))
        m.w32(inst + 0x78, tbl + 4)
        nb = m.u8(m.u32(tbl + 4) + 1)
        m.w16(inst + 0x72, nb)
        m.w32(inst + 0x74, tbl)
        m.w32(inst + 0x7C, m.u32(model + 0x38))
        m.w32(inst + 0x80, m.u32(model + 0x84))
        m.w32(inst + 0x90, s1)
        s1 += nb << 3
        m.w32(inst + 0x8C, s1)
        s1 += nb << 3
        m.w32(inst + 0x94, s1)
        m.w32(inst + 0x84, m.u32(model + 0x9C))
        fr = m.u32(m.u32(m.u32(inst + 0x78)) + 4)
        m.copy(m.u32(inst + 0x8C), m.u32(fr + 0x10), nb << 3)
        s1 += nb << 3
        m.copy(m.u32(inst + 0x90), m.u32(fr + 0x10), nb << 3)
        m.w32(inst + 0x88, 0)
        w = m.u32(fr + 4)
        m.w16(inst + 0x98, s32(w << 22) >> 22)
        m.w16(inst + 0x9A, s32(w << 11) >> 21)
        m.w16(inst + 0x9C, s32(w << 1) >> 22)
        m.copy(inst + 0xA0, inst + 0x98, 8)
    if flags & 1 or not m.u32(model + 0x1C):
        m.w32(inst + 0x14, m.u32(model + 0x20))
        m.w32(inst + 0x1C, m.u32(model + 0x28))
        m.w32(inst + 0x18, m.u32(model + 0x24))
        m.w32(inst + 0x20, m.u32(model + 0x2C))
        m.w32(inst + 0x24, m.u32(model + 0x30))
        m.w32(inst + 0x28, m.u32(model + 0x34))
        if not m.u32(model + 0x1C):
            m.w32(model + 0x1C, 1)
            m.w32(inst + 4, m.u32(inst + 4) | 8)
    else:
        for o in (0x14, 0x18, 0x1C, 0x20, 0x24, 0x28):
            m.w32(inst + o, 0)
        if m.u32(model + 0x2C):
            m.w32(inst + 0x20, m.u32(model + 0x2C))
        if m.u32(model + 0x30):
            m.w32(inst + 0x24, s1)
            src = m.u32(model + 0x30)
            n = (m.u32(src) << 2) + 8
            m.copy(s1, src, n)
            s1 += n
        if m.u32(model + 0x34):
            m.w32(inst + 0x28, m.u32(model + 0x34))
    m.w16(inst + 0x3C, m.u16(model + 0x8C))
    if flags & 2:
        m.w32(inst + 0x2C, m.u32(model + 0x88))
    else:
        m.w32(inst + 0x2C, s1)
        n = m.u16(model + 0x8C) << 3
        m.copy(s1, m.u32(model + 0x88), n)
        s1 += n
    if not m.u32(model + 0x14):
        if not flags & 0x40:
            m.w32(inst + 0x30, s1)
            s1 += m.u16(model + 0x8C) << 3
        m.w32(inst + 0x34, -1)
        m.w32(inst + 0x38, -1)
        if flags & 0x100:
            m.w32(inst + 0x48, m.u32(model + 0xC4))
        else:
            m.w32(inst + 0x48, s1)
            m.copy(s1, m.u32(model + 0xC4), m.u16(model + 0x8C) << 2)
            s1 += m.u16(model + 0x8C) << 2
    m.w16(inst + 0x42, m.u16(model + 0x8E))
    if flags & 4:
        m.w32(inst + 0x44, m.u32(model + 0x90))
    elif m.u32(inst + 0x44):
        m.w32(inst + 0x44, s1)
        n = m.u16(model + 0x8E) << 3
        m.copy(s1, m.u32(model + 0x90), n)
        s1 += n
    else:
        m.w32(inst + 0x44, 0)
    if not flags & 0x10:
        m.w32(inst + 0x4C, s1)
        s1 += 0x800
        m.w32(inst + 0x50, s1)
    else:
        m.w32(inst + 0x4C, 0)
        m.w32(inst + 0x50, 0)
    m.w16(inst + 0x5C, -1)
    m.w16(inst + 0x5E, 0)
    m.w32(inst + 0x54, 0)
    m.w32(inst + 0x58, 0)
    if e:
        m.w32(e + 0x2D4, m.u32(model + 0xA0))
        for k in range(3):
            m.w32(e + 0x270 + 4 * k, m.u32(model + 0xA4 + 4 * k))
        n = m.u16(model + 0xBE)
        m.w16(e + 0x36, n)
        for i in range(n):
            q, t = m.u32(e + 0x304) + 0x48 * i, m.u32(model + 0xC0) + 0x48 * i
            m.w32(q + 4, m.u32(t + 4))
            m.w32(q + 8, m.u32(t + 0xC))
            m.w32(q + 0xC, m.u32(t + 0xC))
            m.w16(q + 0x1C, m.u16(t + 0x1C))
            m.w16(q + 0x1E, m.u16(t + 0x1E))
            for o in (0x34, 0x30, 0x38, 0x10, 0x14):
                m.w32(q + o, m.u32(t + o))
            for o in (0x40, 0x42, 0x44):
                m.w16(q + o, m.u16(t + o))
            m.w32(q + 0x18, inst)
            if flags & 0x200:
                for o in (0x34, 0x30, 0x38):
                    m.w32(q + o, s32(m.u32(q + o) << 1))
            m.w32(q + 0x3C, 0)
    return 1


def instance_release(eng, inst, e, delay):
    """0x8006B234: the parts' instances first (2 frames late), the model's 'first user' bit
    given back, the block freed now or after delay frames (characters' to their pool)."""
    m = eng.m
    if not inst:
        return
    if e:
        for i in range(m.u16(e + 0x36)):
            instance_release(eng, m.u32(m.u32(e + 0x304) + 0x48 * i + 0x3C), 0, 2)
    f = m.u32(inst + 4)
    if f & 8:
        m.w32(inst + 4, f & ~8)
        m.w32(m.u32(inst + 8) + 0x1C, 0)
    t = m.s32(e + 8) if e else 0
    if t in (0x1402, 0x1405):
        return      # the character's destructor gives its pose buffer back (pose_release)
    buf = m.u32(inst)
    if buf:
        eng.heap.free_later(buf, delay)
    m.w32(inst, 0)
