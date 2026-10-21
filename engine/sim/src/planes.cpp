// Objects with plane shapes (docs/re/objects.md, combat.md): tools/sim/engine_planes.py. A plane
// record (0x1C bytes, the entity's list +0x300, +0x2EE of them): +0 flags (bits 0/1: a surface a
// segment can hit), +4 the normal (3 x s16, 32767 long, in the object's frame), +0xA d, +0xC and
// +0x14 masks of the planes a hit point must be inside of.
#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/simgte.h"

namespace valor::sim {

namespace {
constexpr U PLANES_TEMPLATE = 0x8003A238;
constexpr U DESTRUCTIBLE_TOUCH = 0x8005D32C;

V3 to_local(Mem& m, U o, const I p[3]) {
    M3 R = rot_yxz3(S16(-m.s16(o + 0x22A)), S16(-m.s16(o + 0x22E)), S16(-m.s16(o + 0x232)));
    V3 d;
    for (int k = 0; k < 3; k++) d[k] = S16(p[k] - m.s16(o + 0x20E + 4 * k));
    V3 mac = gmul_mac(R, d, {0, 0, 0}, 12);
    return {S16(mac[0]), S16(mac[1]), S16(mac[2])};
}

I impact_material(Engine& eng, U e) {
    Mem& m = eng.m;
    U inst = U(m.u32(e + 0x100));
    if (inst) {
        U h = U(m.u32(m.u32(inst + 8)));
        I snd = -1;
        switch (h) {
            case 0x69960D1C: case 0x4D682091: case 0x5C8D7E20: snd = 0x23; break;
            case 0xD2E7F597: case 0xE68BC914: case 0xEBF2A379: case 0xA1EBF698: case 0x4A4B9B8A: snd = 0x25; break;
            case 0x5151A6DA: snd = 0x27; break;
            default: break;
        }
        if (snd != -1) eng.audio->play(0, snd, e, 0x20000, 0, 0x10000, 0x4000, 0, 0, 1, 0);
    }
    return 1;
}
}  // namespace

void box_sphere(Mem& m, U e, const I mn[3], const I mx[3]) {
    I d[3];
    for (int k = 0; k < 3; k++) d[k] = iabs(mx[k] - mn[k]) & 0xFFFF;
    I big = imax(d[0], d[1]);
    I r = big >= d[2] ? big + 0x1E : d[2] + 0x1E;
    m.w16(e + 0x2E8, r >> 1);
    for (int k = 0; k < 3; k++) m.w16(e + 0x2D8 + 2 * k, (mn[k] + (S32(d[k] << 16) >> 17)) & 0xFFFF);
}

void object_box(Engine& eng, U e, const I mn[3], const I mx[3]) {
    Mem& m = eng.m;
    U lst = U(m.u32(e + 0x300));
    m.w16(e + 0x2EE, 6);
    box_sphere(m, e, mn, mx);
    m.copy(lst, PLANES_TEMPLATE, 6 * 0x1C);
    I vals[6] = {mn[1], mx[1], mn[0], mx[0], mn[2], mx[2]};
    for (int i = 0; i < 6; i++) m.w16(lst + 0x1C * i + 0xA, iabs(S16(vals[i])) - 1);
    for (int k = 0; k < 3; k++) {
        m.w16(e + 0x2F0 + 2 * k, mn[k]);
        m.w16(e + 0x2F8 + 2 * k, mx[k]);
    }
}

bool segment_box(Engine& eng, const I a[3], const I b[3], U o, I point[3], I normal[3], I& flags) {
    Mem& m = eng.m;
    U lst = U(m.u32(o + 0x300));
    I n = m.u16(o + 0x2EE);
    V3 la = to_local(m, o, a), lb = to_local(m, o, b);
    V3 delta;
    for (int k = 0; k < 3; k++) delta[k] = S16(lb[k] - la[k]);
    struct Pl {
        U q;
        V3 nv;
        I d;
    };
    std::vector<Pl> planes;
    std::vector<std::pair<I, I>> ds;
    I cross = 0;
    for (I i = 0; i < n; i++) {
        U q = U(lst + 0x1C * i);
        V3 nv = {m.s16(q + 4), m.s16(q + 6), m.s16(q + 8)};
        I d = m.s16(q + 0xA);
        I d0 = (S32(la[0] * nv[0] + la[1] * nv[1] + la[2] * nv[2]) >> 15) - d;
        I d1 = (S32(lb[0] * nv[0] + lb[1] * nv[1] + lb[2] * nv[2]) >> 15) - d;
        planes.push_back({q, nv, d});
        ds.push_back({d0, d1});
        if ((d0 ^ d1) < 0) cross |= I(1) << i;
    }
    I best = 0x7FFFFFFF;
    bool have = false;
    V3 hp{}, hn{};
    I hf = 0;
    for (size_t i = 0; i < planes.size(); i++) {
        const Pl& pl = planes[i];
        if (!(m.u32(pl.q) & 3) || !((cross >> i) & 1)) continue;
        I d0 = ds[i].first, d1 = ds[i].second;
        if (d0 < 0 || iabs(d0) >= best) continue;
        I t = (iabs(d0) << 12) / (iabs(d0) + iabs(d1));
        V3 p;
        for (int k = 0; k < 3; k++) p[k] = S16(sat16i((t * delta[k]) >> 12) + la[k]);
        bool inside = true;
        I masks = m.u32(pl.q + 0xC) | m.u32(pl.q + 0x14);
        for (size_t j = 0; j < planes.size(); j++) {
            if (j == i || !((masks >> j) & 1)) continue;
            const Pl& pj = planes[j];
            if ((S32(p[0] * pj.nv[0] + p[1] * pj.nv[1] + p[2] * pj.nv[2]) >> 15) - pj.d >= 0) {
                inside = false;
                break;
            }
        }
        if (inside) {
            best = iabs(d0);
            have = true;
            hp = p, hn = pl.nv, hf = m.u32(pl.q);
        }
    }
    if (!have) return false;
    M3 M;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) M[r][c] = m.s16(o + 0x340 + 6 * r + 2 * c);
    V3 T = {m.s32(o + 0x354), m.s32(o + 0x358), m.s32(o + 0x35C)};
    V3 pw = gmul_mac(M, hp, T, 12), nw = gmul_mac(M, hn, {0, 0, 0}, 12);
    for (int k = 0; k < 3; k++) point[k] = S16(pw[k]), normal[k] = S16(nw[k]);
    flags = hf;
    return true;
}

bool projectile_planes(Engine& eng, U e, U o, I point[3], I normal[3], I& flags) {
    Mem& m = eng.m;
    if (!m.u16(o + 0x2EE) || (m.u32(e + 0x18) & 1)) return false;
    I a[3] = {m.s16(e + 0x25A), m.s16(e + 0x25E), m.s16(e + 0x262)};
    I b[3] = {m.s16(e + 0x20E), m.s16(e + 0x212), m.s16(e + 0x216)};
    return segment_box(eng, a, b, o, point, normal, flags);
}

I destructible_touch(Engine& eng, U e, U o) {
    Mem& m = eng.m;
    I fa = m.u32(o + 0xC);
    I t = m.s32(o + 8);
    if (!((fa & 0x101) || t == 0x31)) return 0;
    if ((fa & 4) && t != 0x31) return impact_material(eng, e);
    if (fa & 1) {
        U pp = U(m.u32(o + 0x304));
        if (m.s32(pp + 8) != 5) return 0;
        eng.note("destructible hit by the player's melee not implemented");
        return 0;
    }
    apply_damage(eng, e, m.s32(o + 0xF8), o);
    auto it = eng.ents.find(e);
    eng.logic->run_message(it == eng.ents.end() ? nullptr : it->second, 0, 1, "destructible hit");
    I fe = m.u32(e + 0xC);
    if ((fe & 0x20000000) && !(fe & 0x100000)) {
        m.w32(e + 0xC, fe | 0x100000);
        m.w32(0x80039D10, m.u32(0x80039D10) + 1);
        U rec = m.u32(e + 0x1C) ? U(m.u32(m.u32(e + 0x1C) + 4)) : 0;
        if (rec && m.u32(rec + 0x68)) eng.note("destructible drop (0x800838AC) not implemented");
    }
    if (t == 0x31) return 1;
    return impact_material(eng, e);
}

U object_spawn(Engine& eng, Record& rec, U model) {
    Mem& m = eng.m;
    U r = rec.addr;
    U e = eng.entity_create(m.s32(model + 0xFC), 0, 0, 0, -1, 0, 6);
    I g = m.u8(0x80099ECC);
    m.w8(0x80099ECC, g + 1);
    m.w8(e + 0x35, g);
    I hp = m.u8(r + 0x5C) << 16;
    m.w32(e + 0xF0, hp);
    m.w32(e + 0xF4, hp);
    for (int k = 0; k < 3; k++) {
        m.w32(e + 0x20C + 4 * k, S32(m.s16(r + 0x10 + 2 * k) << 16));
        m.w32(e + 0x264 + 4 * k, S32(m.s16(r + 0x18 + 2 * k) << 16));
        m.w32(e + 0x258 + 4 * k, S32(m.s16(r + 0x10 + 2 * k) << 16));
    }
    m.w32(e + 0x294, 5);
    m.w32(e + 0x290, 5);
    m.w16(e + 0x2EC, 0);
    m.w16(e + 0x36, 0);
    m.w32(e + 0x36C, 0);
    m.w32(e + 0x370, 0x8005E760);
    m.w32(e + 0x374, 0);
    m.w32(e + 0x378, 0);
    m.w32(e + 0x384, 0x8006A37C);
    I mn[3], mx[3];
    for (int k = 0; k < 3; k++) {
        I h = iabs(m.s16(r + 0x2A + 2 * k)) >> 1;
        mn[k] = S16(-h), mx[k] = S16(h);
    }
    if (m.s32(e + 8) == 0x1779) {
        m.w32(e + 0x14, 0);
        m.w32(e + 0xC, 0x13028000);
        m.w32(e + 0x10, 0x40213);
        box_sphere(m, e, mn, mx);
        m.w32(e + 0x37C, 0);
        m.w16(e + 0x2EE, 0);
    } else {
        m.w32(e + 0x10, 0x44011);
        m.w32(e + 0x14, 0x88214051);
        m.w32(e + 0xC, 0x13028400);
        m.w32(e + 0x37C, DESTRUCTIBLE_TOUCH);
        object_box(eng, e, mn, mx);
    }
    return e;
}

}  // namespace valor::sim
