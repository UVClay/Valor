"""The GTE operations and PsyQ libgte routines the game logic uses, with the hardware's integer
behaviour (runtime/src/gte.cpp, psx-spx): 44-bit accumulators, IR saturation, the UNR division of
RTPS, and the recording's widened projection (Valor's widescreen and FOV scale every RTPS result
inside a level: runtime gte.cpp rtp, gte_set_projection with the recording's 21:9 and 110°)."""
import math

import combat_sim as cs


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def wrap44(v):
    v &= (1 << 44) - 1
    return v - (1 << 44) if v & (1 << 43) else v


def sat16(v, lm=False):
    lo = 0 if lm else -0x8000
    return lo if v < lo else 0x7FFF if v > 0x7FFF else v


def mul(m, v, t=(0, 0, 0), sf=12):
    """MVMVA: MAC = (t * 0x1000 + m . v) >> sf (44-bit accumulators); returns (mac, ir)."""
    mac = []
    for r in range(3):
        acc = t[r] * 0x1000
        acc = wrap44(acc + m[r][0] * v[0])
        acc = wrap44(acc + m[r][1] * v[1])
        acc = wrap44(acc + m[r][2] * v[2])
        mac.append(s32(acc >> sf))
    return mac, [sat16(x) for x in mac]


def op(d, ir, sf=12):
    """OP: the outer product of the matrix diagonal d and IR."""
    mac = [s32(wrap44(ir[2] * d[1] - ir[1] * d[2]) >> sf), s32(wrap44(ir[0] * d[2] - ir[2] * d[0]) >> sf),
           s32(wrap44(ir[1] * d[0] - ir[0] * d[1]) >> sf)]
    return mac, [sat16(x) for x in mac]


def sqr(ir, sf=0):
    mac = [s32((x * x) >> sf) for x in ir]
    return mac, [sat16(x) for x in mac]


def lzc(v):
    v &= 0xFFFFFFFF
    if v & 0x80000000:
        v = ~v & 0xFFFFFFFF
    return 32 - v.bit_length()


UNR = [max(0, (0x40000 // (i + 0x100) + 1) // 2 - 0x101) for i in range(0x101)]


def divide(h, sz3):
    if h >= sz3 * 2:
        return 0x1FFFF
    z = 16 - sz3.bit_length() if sz3 else 16
    n = h << z
    d = sz3 << z
    u = UNR[(d - 0x7FC0) >> 7] + 0x101
    d = (0x2000080 - d * u) >> 8
    d = (0x0000080 + d * u) >> 8
    r = (n * d + 0x8000) >> 16
    return min(r, 0x1FFFF)


# The recording's projection (settings.ini: aspect 21:9, fov 110; runtime defaults world_h 400,
# base tan 120/400): every RTPS result scaled about the screen centre by (kx, ky).
ASPECT, FOV, BASE_TAN = 21 / 9, 110.0, 120.0 / 400.0
_K = BASE_TAN / (math.tan(FOV * math.pi / 360.0) / ASPECT)
KX, KY = _K * ((4.0 / 3.0) / ASPECT), _K


def rtps(rt, tr, h, ofx, ofy, v, scaled=True):
    """RTPS (sf=1): the screen point (SX, SY), with the recording's scaling."""
    accs = []
    for r in range(3):
        acc = tr[r] * 0x1000
        acc = wrap44(acc + rt[r][0] * v[0])
        acc = wrap44(acc + rt[r][1] * v[1])
        acc = wrap44(acc + rt[r][2] * v[2])
        accs.append(acc)
    mac = [s32(a >> 12) for a in accs]
    ir1, ir2 = sat16(mac[0]), sat16(mac[1])
    z12 = s32(accs[2] >> 12)
    sz3 = 0 if z12 < 0 else 0xFFFF if z12 > 0xFFFF else z12
    q = divide(h, sz3)
    sx, sy = q * ir1, q * ir2
    if scaled:
        sx = int(float(sx) * KX)
        sy = int(float(sy) * KY)
    sx += ofx
    sy += ofy
    x, y = s32(sx) >> 16, s32(sy) >> 16
    x = -0x400 if x < -0x400 else 0x3FF if x > 0x3FF else x
    y = -0x400 if y < -0x400 else 0x3FF if y > 0x3FF else y
    return x, y


# ---------------------------------------------------------------- libgte

VN_TABLE_ADDR = 0x8002DD3C      # VectorNormal's table (lh at 0x80030000 - 0x22C4 + 2 * (n - 64))
_VN = None


def _vn_table():
    global _VN
    if _VN is None:
        import os
        import struct
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
        d = open(os.path.join(root, "extracted", "moh", "SLUS_009.74"), "rb").read()
        base = struct.unpack_from("<I", d, 0x18)[0]
        _VN = list(struct.unpack_from("<192h", d, 0x800 + VN_TABLE_ADDR - base))
    return _VN


def vector_normal(v):
    """VectorNormal / VectorNormalSS (0x80016A70): to length 4096, with the GTE (IR is 16-bit)."""
    t = [s16(x) for x in v]
    mac, _ = sqr(t, 0)
    n = s32(mac[0] + mac[1] + mac[2])
    lz = lzc(n) & ~1
    sh = (31 - lz) >> 1
    if lz - 24 >= 0:
        x = s32(n << (lz - 24))
    else:
        x = n >> (24 - lz)
    k = _vn_table()[x - 0x40]
    out = [s32(k * c) >> sh for c in t]
    return out


def outer_product12(a, b):
    mac, _ = op([s16(x) for x in a], [s16(x) for x in b], 12)
    return mac


def apply_matrix_lv(m, v):
    """ApplyMatrixLV (0x800163E8): a 32-bit vector through the matrix, in two 15-bit halves."""
    hi, lo = [], []
    for x in v:
        if x >= 0:
            hi.append(x >> 15)
            lo.append(x & 0x7FFF)
        else:
            y = -x
            hi.append(-(y >> 15))
            lo.append(-(y & 0x7FFF))
    mh, _ = mul(m, [s16(x) for x in hi], sf=0)
    ml, _ = mul(m, lo, sf=12)
    out = []
    for a, b in zip(ml, mh):
        b = -((-b) << 3) if b < 0 else b << 3
        out.append(s32(a + b))
    return out


def rot_matrix_y(r):
    """RotMatrixY(r, identity)."""
    s, c = cs._sc(r)
    return [[c, 0, s], [0, 0x1000, 0], [s16(-s), 0, c]]


def rot_trans_sv(m, t, v):
    """RotTransSV: (m . v >> 12) + t, saturated to s16."""
    _, ir = mul(m, v, t, 12)
    return ir


def rcos(a):
    a = abs(a) & 0xFFF
    S = cs.SIN1
    if a <= 0x400:
        return S[0x400 - a]
    if a <= 0x800:
        return -S[a - 0x400]
    if a <= 0xC00:
        return -S[0xC00 - a]
    return S[a - 0xC00]


def rsin(a):
    return cs.rsin(a)


def transpose(m):
    return [[m[j][i] for j in range(3)] for i in range(3)]


def iabs(x):
    return -x if x < 0 else x


def dist_cheap(a, b, c):
    """max + (sum of the other two) / 4 of the absolute values."""
    x, y, z = sorted((iabs(a), iabs(b), iabs(c)))
    return z + ((x + y) >> 2)
