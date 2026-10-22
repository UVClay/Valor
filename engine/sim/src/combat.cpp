// Contacts and hits (docs/re/entities.md Collisions, combat.md): tools/sim/engine_combat.py.
#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/simgte.h"

namespace valor::sim {

bool segment_sphere(Mem& m, U e, U o, const V3& u, I& key);

namespace {
constexpr U G_STAT_HITS = 0x80039CAC;
constexpr U G_STAT_WOUNDED = 0x80039CB0;
constexpr U G_STAT_KILLS = 0x80039CB8;
constexpr U G_WEAPONS = 0x80093640;
constexpr U G_CHEATS = 0x800386A4;
constexpr U G_ENEMY_WEAPONS = 0x80097FF0;

bool attack_kind(I k) { return k == 9 || k == 5 || k == 6; }

V3 centre(Mem& m, U e) { return {m.s16(e + 0x2E0), m.s16(e + 0x2E2), m.s16(e + 0x2E4)}; }

I call_hit_part(Engine& eng, U fn, U e, I p, U o, I op) {
    if (!fn) return 0;
    I r;
    if (!run_hit_part(eng, fn, e, p, o, op, r)) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "hit_part %08X not implemented", fn);
        eng.note(buf);
        return 0;
    }
    return r;
}

bool spheres_touch(Mem& m, U a, U b) {
    I ra = m.u16(a + 0x2E8), rb = m.u16(b + 0x2E8);
    if (!ra || !rb) return false;
    V3 ca = centre(m, a), cb = centre(m, b);
    I d[3];
    for (int k = 0; k < 3; k++) d[k] = S16(ca[k] - cb[k]);
    return (ra + rb) * (ra + rb) - (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) > 0;
}

bool parts_touch(Mem& m, U a, U b, I& fi, I& fj) {
    bool first = false;
    U pa = U(m.u32(a + 0x304)), pb = U(m.u32(b + 0x304));
    for (I i = 0; i < m.u16(a + 0x36); i++) {
        U qa = U(pa + 0x48 * i);
        if (m.s32(qa + 8) == 7) continue;
        for (I j = 0; j < m.u16(b + 0x36); j++) {
            U qb = U(pb + 0x48 * j);
            if (m.s32(qb + 8) == 7) continue;
            I d[3];
            for (int k = 0; k < 3; k++) d[k] = S16(m.s32(qa + 0x20 + 4 * k) - m.s32(qb + 0x20 + 4 * k));
            I r = S32(m.s32(qa + 0x34) + m.s32(qb + 0x34));
            I v = S32(r * r - (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
            if (v > 0) {
                m.w32(qa, 1);
                m.w32(qb, 1);
                m.w32(a + 0x314, v);
                m.w32(b + 0x314, v);
                if (!first) first = true, fi = i, fj = j;
            }
        }
    }
    return first;
}


I segment_parts(Mem& m, U e, const V3& u, I len2, U o) {
    V3 start = {m.s16(e + 0x25A), m.s16(e + 0x25E), m.s16(e + 0x262)};
    I first = -1;
    U pp = U(m.u32(o + 0x304));
    for (I j = 0; j < m.u16(o + 0x36); j++) {
        U q = U(pp + 0x48 * j);
        if (m.s32(q + 8) == 7) continue;
        V3 d;
        for (int k = 0; k < 3; k++) d[k] = S16(m.s32(q + 0x20 + 4 * k) - start[k]);
        I r2 = m.s32(q + 0x38);
        I a1 = S32(d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - r2);
        bool hit = a1 <= 0;
        if (!hit) {
            I dot = S32(d[0] * u[0] + d[1] * u[1] + d[2] * u[2]);
            if (dot > 0 && len2 >= a1) {
                V3 x = gop(d, u, 12);
                a1 = r2;
                hit = !(r2 < S32(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]));
            }
        }
        if (hit) {
            m.w32(q, 1);
            m.w32(o + 0x314, a1);
            if (first < 0) first = j;
        }
    }
    return first;
}

void swept(Engine& eng, U e, U head) {
    Mem& m = eng.m;
    I fb = m.u32(e + 0x10);
    bool nearest = fb & 0x80000;
    V3 path;
    for (int k = 0; k < 3; k++) path[k] = S16(S32(m.s32(e + 0x20C + 4 * k) - m.s32(e + 0x258 + 4 * k)) >> 16);
    V3 sq = gsqr(path, 0);
    I len2 = S32(sq[0] + sq[1] + sq[2]);
    V3 u = {0, 0, 0};
    if (path[0] || path[1] || path[2]) {
        V3 n = vnormal(eng.gte, path);
        u = {S16(n[0]), S16(n[1]), S16(n[2])};
    }
    if (u[0] == 0 && u[1] == 0 && u[2] == 0) u = {m.s16(e + 0x344), m.s16(e + 0x34A), m.s16(e + 0x350)};
    struct Hit {
        U o;
        I key, part, done;
    };
    std::vector<Hit> hits;
    int best = -1;
    U o = head;
    while (o) {
        U onext = U(m.u32(o + 4));
        if (!(m.u32(o + 0xC) & REMOVED) && !(m.u32(o + 0x10) & 6) && (m.u32(e + 0x14) & m.u32(o + 0xC)) && o != e) {
            I key;
            if (segment_sphere(m, e, o, u, key)) {
                if (!nearest) {
                    call_touch(eng, U(m.u32(e + 0x37C)), e, o);
                    I part = segment_parts(m, e, u, len2, o);
                    if (part >= 0) {
                        call_hit_part(eng, U(m.u32(e + 0x374)), e, -1, o, part);
                        call_hit_part(eng, U(m.u32(o + 0x374)), o, part, e, -1);
                        if (m.u32(e + 0xC) & REMOVED) break;
                    }
                } else if (hits.size() < 0x9F) {
                    if (m.u16(o + 0x36)) {
                        I part = segment_parts(m, e, u, len2, o);
                        if (part >= 0) {
                            if (best < 0 || (key & 0xFFFF) < hits[size_t(best)].key) best = int(hits.size());
                            hits.push_back({o, key & 0xFFFF, part, 0});
                        }
                    } else {
                        if (best < 0 || (key & 0xFFFF) < hits[size_t(best)].key) best = int(hits.size());
                        hits.push_back({o, key & 0xFFFF, -1, 0});
                    }
                }
            }
        }
        o = onext;
    }
    while (best >= 0) {
        U tgt = hits[size_t(best)].o;
        I key = hits[size_t(best)].key, part = hits[size_t(best)].part;
        hits[size_t(best)].done = 1;
        I r0 = call_touch(eng, U(m.u32(e + 0x37C)), e, tgt);
        I r1 = 0, r2 = 0;
        if (m.u16(tgt + 0x36) && part != -1) {
            r1 = call_hit_part(eng, U(m.u32(e + 0x374)), e, -1, tgt, part);
            r2 = call_hit_part(eng, U(m.u32(tgt + 0x374)), tgt, part, e, -1);
        }
        if (r0 || r1 || r2) break;
        int nb = -1;
        I nkey = 0x3B9AC9FF;
        for (size_t i = 0; i < hits.size(); i++) {
            I k = S16(hits[i].key);
            if (!hits[i].done && k >= S16(key) && k < nkey && hits[i].o != tgt) nb = int(i), nkey = k;
        }
        best = nb;
    }
    if (!(m.u32(e + 0xC) & REMOVED) && (m.u32(e + 0x10) & 8)) {
        U wc = U(m.u32(e + 0x378));
        if (!run_world_contact(eng, wc, e, 0)) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "world_contact %08X not implemented", wc);
            eng.note(buf);
        }
    }
}

U part_of(Mem& m, U e, I i) { return U(m.u32(e + 0x304) + 0x48 * i); }

bool facing(Mem& m, U e, const V3& a, const V3& b) {
    I d[3];
    for (int k = 0; k < 3; k++) d[k] = S16(b[k] - a[k]);
    I f[3] = {m.s16(e + 0x344), m.s16(e + 0x34A), m.s16(e + 0x350)};
    return S32(f[0] * d[0] + f[1] * d[1] + f[2] * d[2]) > 0;
}

V3 part_centre(Mem& m, U q) { return {S16(m.u16(q + 0x20)), S16(m.u16(q + 0x24)), S16(m.u16(q + 0x28))}; }

void stat_hit(Engine& eng) {
    Mem& m = eng.m;
    U inv = U(m.u32(m.u32(G_SOLDIER) + 0x388));
    m.w32(G_STAT_HITS, m.u32(G_STAT_HITS) + 1);
    m.w32(inv + 0x148, m.u32(inv + 0x148) + 1);
}

void stat_kill(Engine& eng, U attacker, I n) {
    Mem& m = eng.m;
    U inv = U(m.u32(m.u32(G_SOLDIER) + 0x388));
    if (!n) return;
    I v = (m.u16(inv + 0x142) + n) & 0xFFFF;
    m.w16(inv + 0x142, v);
    m.w32(G_STAT_KILLS, m.u32(G_STAT_KILLS) + n);
    if (v & 0x8000) {
        m.w32(G_STAT_KILLS, 0);
        m.w16(inv + 0x142, 0);
    }
    I w = m.s16(attacker + 0x3CA);
    if (w != -1) {
        if (w < 0xD) {
            U a = U(G_WEAPONS + 0x44 * w + 0x30);
            m.w16(a, m.u16(a) + 1);
        }
    } else if (m.u32(attacker + 0xC) & 1) {
        U a = U(G_WEAPONS + 0x44 * m.u8(inv + 0x55) + 0x30);
        m.w16(a, m.u16(a) + 1);
    }
}

I get_var(Mem& m, U e, I v) { return m.s32(e + 0x4C + 4 * v); }
void set_var(Mem& m, U e, I v, I x) { m.w32(e + 0x4C + 4 * v, S32(x)); }

void bump(Mem& m, U a) { m.w32(a, m.u32(a) + 1); }

}  // namespace

bool segment_sphere(Mem& m, U e, U o, const V3& u, I& key) {
    I r = m.u16(o + 0x2E8);
    if (!r) return false;
    V3 start = {m.s16(e + 0x25A), m.s16(e + 0x25E), m.s16(e + 0x262)};
    V3 c0 = centre(m, o);
    V3 d;
    for (int k = 0; k < 3; k++) d[k] = S32(c0[k] - start[k]);
    V3 c = gop({S16(d[0]), S16(d[1]), S16(d[2])}, u, 12);
    if (r * r < c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) return false;
    key = dist_cheap(d[0], d[1], d[2]);
    return true;
}

I call_touch(Engine& eng, U fn, U e, U other) {
    if (!fn) return 0;
    I r;
    if (!run_touch(eng, fn, e, other, r)) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "touch %08X not implemented", fn);
        eng.note(buf);
        return 0;
    }
    return r;
}

void entities_collide(Engine& eng) {
    Mem& m = eng.m;
    U head = U(m.u32(G_ENTITIES));
    U e = head;
    while (e) {
        U nxt = U(m.u32(e + 4));
        I fb = m.u32(e + 0x10);
        if ((m.u32(e + 0xC) & REMOVED) || (fb & 2)) {
            e = nxt;
            continue;
        }
        if (!(fb & 4)) {
            U o = nxt;
            while (o) {
                U onext = U(m.u32(o + 4));
                if (!(m.u32(o + 0xC) & REMOVED) && !(m.u32(o + 0x10) & 6) && (m.u32(e + 0x14) & m.u32(o + 0xC)) && spheres_touch(m, e, o)) {
                    call_touch(eng, U(m.u32(e + 0x37C)), e, o);
                    call_touch(eng, U(m.u32(o + 0x37C)), o, e);
                    I pi, pj;
                    if (parts_touch(m, e, o, pi, pj)) {
                        call_hit_part(eng, U(m.u32(e + 0x374)), e, pi, o, pj);
                        call_hit_part(eng, U(m.u32(o + 0x374)), o, pj, e, pi);
                        if (m.u32(e + 0xC) & REMOVED) break;
                    }
                }
                o = onext;
            }
        } else
            swept(eng, e, head);
        e = nxt;
    }
}

I damage_from_hit(Engine& eng, U victim, I vpart, U attacker, I apart) {
    Mem& m = eng.m;
    if (vpart < 0) return 0;
    U vq = part_of(m, victim, vpart);
    U aq = 0;
    I dmg;
    if (apart < 0)
        dmg = m.s32(attacker + 0xF8);
    else {
        aq = part_of(m, attacker, apart);
        dmg = attack_kind(m.s32(aq + 8)) ? m.s32(aq + 0x10) : 0;
    }
    I kind = m.s32(vq + 8);
    if (kind == 10)
        dmg *= 2;
    else if (kind == 11)
        dmg *= 4;
    else if (kind == 12)
        dmg = 0x7FFFFFFF;
    else if (kind == 2)
        dmg = (dmg >> 1) + (dmg >> 2);
    else if (kind == 3)
        dmg >>= 1;
    else if (kind == 4)
        dmg >>= 2;
    dmg = S32(dmg);
    if (apart < 0) {
        V3 start = {m.s16(attacker + 0x25A), m.s16(attacker + 0x25E), m.s16(attacker + 0x262)};
        I side = facing(m, victim, part_centre(m, vq), start) ? 0x40 : 0x20;
        m.w32(victim + 0x38, m.u32(victim + 0x38) | side);
        if (dmg) apply_damage(eng, victim, dmg, attacker);
        return 1;
    }
    I akind = m.s32(aq + 8);
    if ((akind == 8 || kind == 8) && m.s32(victim + 8) != 0x1B && m.s32(attacker + 8) != 0x1B) return 0;
    I dy = S32(m.s32(victim + 0x210) - m.s32(attacker + 0x210)) >> 16;
    bool front = facing(m, victim, part_centre(m, vq), part_centre(m, aq));
    if (attack_kind(akind)) {
        m.w32(attacker + 0x38, m.u32(attacker + 0x38) | 0x200000);
        I bits;
        if (akind == 5)
            bits = front ? 0x40 : 0x20;
        else
            bits = (akind == 6 ? 0x100000 : 0x80000) | (front ? 0x40 : 0x20);
        m.w32(victim + 0x38, m.u32(victim + 0x38) | bits);
        if (m.u32(attacker + 0x10) & 0x20000)
            for (I i = 0; i < m.u16(attacker + 0x36); i++) {
                U q = part_of(m, attacker, i);
                if (attack_kind(m.s32(q + 8))) m.w32(q + 8, 8);
            }
    } else if (!attack_kind(kind))
        m.w32(victim + 0x38, m.u32(victim + 0x38) | (front ? 0x400 : 0x200));
    if (dy > 0 && attack_kind(akind)) m.w32(victim + 0x38, m.u32(victim + 0x38) | 0x80);
    if (dy < 0 && attack_kind(akind)) m.w32(victim + 0x38, m.u32(victim + 0x38) | 0x100);
    if (dmg) apply_damage(eng, victim, dmg, attacker);
    return 1;
}

void apply_damage(Engine& eng, U victim, I dmg, U attacker) {
    Mem& m = eng.m;
    if (m.s32(victim + 0x48) > 0) return;
    if ((m.u32(attacker + 0xC) & 1) && (m.u32(m.u32(attacker + 0x388)) & 0x20000000)) dmg *= 4;
    I h = S32(m.s32(victim + 0xF4) - dmg);
    m.w32(victim + 0xF4, h);
    if (h <= 0) {
        I fa = m.u32(victim + 0xC);
        if ((fa & 0x20000001) != 0x20000000) {
            if (!(fa & 1) && (m.u32(victim + 0x10) & 0x8000)) stat_kill(eng, attacker, 1);
            m.w32(victim + 0x38, m.u32(victim + 0x38) | 0x4000);
            m.w32(victim + 0xC, m.u32(victim + 0xC) | 0x20000000);
        }
        m.w32(victim + 0xF4, 0);
    }
    I fa = m.u32(victim + 0xC);
    if (fa & 1) {
        hit_indicator(eng, victim, attacker);
        U inv = U(m.u32(victim + 0x388));
        m.w32(G_STAT_WOUNDED, m.u32(G_STAT_WOUNDED) + 1);
        m.w32(inv + 0x144, m.u32(inv + 0x144) + 1);
        return;
    }
    if ((fa & 0x4440) && !(m.u32(attacker + 0xC) & 1) && !(m.u32(attacker + 0x10) & 0x100000) && m.s16(attacker + 0x3CA) != -1) {
        stat_hit(eng);
        m.w32(attacker + 0x10, m.u32(attacker + 0x10) | 0x100000);
    }
}

namespace {
bool helmet_off(Engine& eng, U e) {
    Mem& m = eng.m;
    U B = attach_entry(m, e, 1), T = attach_template(1);
    U h = U(m.u32(T + 0x20));
    U who = U(m.u32(0x800A0000));
    if (m_hash(eng, B) != h && h) {
        instance_release(eng, B + 0x94);
        instance_set(eng, B + 0x94, h, 0x156);
        who = U(m.u32(G_SOLDIER));
    }
    knock_off(eng, B, who, m.s32(T + 0x10), m.s32(T + 0x14), m.s32(T), m.s32(T + 8), m.s32(T + 0xC), m.s32(T + 4), m.s32(T + 0x1C),
              0x40000000);
    m.w32(B + 0xE8, 0);
    m.w32(B + 0xEC, 0);
    m.w32(B + 8, 0);
    m.w32(B, m.u32(B) | 2);
    fall_step(eng, B, m.s32(e + 0x210));
    I hh = m_hash(eng, B);
    return hh == 0x8A522920 || hh == 0xAA1714FE;
}
}  // namespace

I walker_world_contact(Engine& eng, U e, I res) {
    Mem& m = eng.m;
    if (res & 1) {
        m.w32(e + 0x200, 0);
        m.w32(e + 0x38, m.u32(e + 0x38) | 2);
    } else
        m.w32(e + 0x3C, m.u32(e + 0x3C) | 2);
    return 0;
}

I dog_hit_part(Engine& eng, U e, I p, U o, I op) {
    Mem& m = eng.m;
    I akind = op >= 0 ? m.s32(part_of(m, o, op) + 8) : -999;
    damage_from_hit(eng, e, p, o, op);
    if (get_var(m, e, 0xD) != -1) set_var(m, e, 0xD, cdiv(m.s32(e + 0xF4) * 100, m.s32(e + 0xF0)));
    set_var(m, e, 0x18, m.s32(o + 8));
    set_var(m, e, 0xC, p);
    if (m.s32(o + 8) == 1 && akind == 5) {
        m.w32(part_of(m, o, op) + 8, 1);
        eng.audio->play(0, 0x22, e, 0x20000, 0, 0x10000, 0x3333, 0, 0, 0, 0);
    }
    return 1;
}

I enemy_hit_part(Engine& eng, U e, I p, U o, I op) {
    Mem& m = eng.m;
    I hp0 = m.s32(e + 0xF4);
    I ot = m.s32(o + 8);
    bool proj = 0x13F0 <= ot && ot < 0x13FA;
    if (proj && handle_same(eng, U(m.u32(o + 0x388) + 0x54), e)) return 0;
    if (ot == m.s32(e + 8) || (proj && (m.u32(o + 0xC) & 4))) {
        damage_from_hit(eng, e, p, o, op);
        set_var(m, e, 0x18, ot);
        set_var(m, e, 0xC, p);
        if (!(m.u32(e + 0xC) & 0x20000000) && m.s32(e + 0xF4) >= 0) return 1;
        set_var(m, e, 0xD, 0);
        m.w32(e + 0xF4, 0);
        return 1;
    }
    I akind = op >= 0 ? m.s32(part_of(m, o, op) + 8) : -999;
    damage_from_hit(eng, e, p, o, op);
    set_var(m, e, 0x18, ot);
    set_var(m, e, 0xC, p);
    if (hp0 != m.s32(e + 0xF4)) {
        if (!(m.u32(o + 0x10) & 0x100000) && !(m.u32(o + 0xC) & 1) && m.s16(o + 0x3CA) != -1) {
            m.w32(G_STAT_HITS, m.u32(G_STAT_HITS) + 1);
            m.w32(o + 0x10, m.u32(o + 0x10) | 0x100000);
        }
        if (0 <= p && p < 0x11) {
            if (p == 0) {
                U obj = U(m.u32(e + 0x388) + 0x18);
                I hf = m.s32(obj + 0x144 + 4);
                if ((hf & 1) && !(hf & 0x1E) && ((eng.rand.draw(FN("enemy_hit")) * 100) >> 15) < 0x32) {
                    I snd;
                    if (helmet_off(eng, e))
                        snd = 0x23;
                    else {
                        snd = 0x25;
                        apply_damage(eng, e, m.s32(e + 0xF4), o);
                    }
                    if (ot != 0x31) {
                        eng.audio->play(0, snd, e, 0x20000, 0, 0x10000, 0x1800, 0, 0, 0, 0);
                        bump(m, 0x80039CC0);
                    } else
                        bump(m, 0x80039CC4), bump(m, 0x80039CE8);
                } else {
                    apply_damage(eng, e, m.s32(e + 0xF4), o);
                    if (ot != 0x31)
                        bump(m, 0x80039CC0);
                    else
                        bump(m, 0x80039CC4), bump(m, 0x80039CE8);
                }
            } else if (p == 7 || ((p == 1 || p == 2) && ot == 0x31))
                bump(m, 0x80039CC4), bump(m, 0x80039CE8);
            else if (p == 1)
                bump(m, 0x80039CC8), bump(m, 0x80039CE0);
            else if (p == 2)
                bump(m, 0x80039CC8), bump(m, 0x80039CE4);
            else {
                static const std::pair<I, std::pair<U, U>> HEAD[] = {
                    {3, {0x80039CC4, 0x80039CD0}},  {4, {0x80039CC4, 0x80039CD4}},  {5, {0x80039CC8, 0x80039CD8}},
                    {6, {0x80039CC8, 0x80039CDC}},  {8, {0x80039CC4, 0x80039CEC}},  {9, {0x80039CCC, 0x80039CF0}},
                    {10, {0x80039CCC, 0x80039CF4}}, {11, {0x80039CCC, 0x80039CF8}}, {12, {0x80039CCC, 0x80039CFC}},
                    {13, {0x80039CCC, 0x80039D00}}, {14, {0x80039CCC, 0x80039D04}}, {15, {0x80039CCC, 0x80039D08}},
                    {16, {0x80039CCC, 0x80039D0C}}};
                for (auto& h : HEAD)
                    if (h.first == p) bump(m, h.second.first), bump(m, h.second.second);
            }
        }
        if (ot == 1 && akind == 5) {
            m.w32(part_of(m, o, op) + 8, 1);
            eng.audio->play(0, 0x22, e, 0x20000, 0, 0x10000, 0x3333, 0, 0, 0, 0);
        }
    }
    if (get_var(m, e, 0xD) != -1) {
        if (!(m.u32(e + 0xC) & 0x20000000) && m.s32(e + 0xF4) > 0) {
            set_var(m, e, 0xD, cdiv(m.s32(e + 0xF4) * 100, m.s32(e + 0xF0)));
            if (!(m.u32(e + 0xC) & 0x20000000) && get_var(m, e, 0xD) < 0xA) apply_damage(eng, e, m.s32(e + 0xF4), o);
        } else {
            set_var(m, e, 0xD, 0);
            m.w32(e + 0xF4, 0);
            m.w32(e + 0xC, m.u32(e + 0xC) | 0x20000000);
        }
    }
    if (proj && !(m.u32(o + 0xC) & 4)) {
        auto it = eng.ents.find(e);
        eng.logic->run_message(it == eng.ents.end() ? nullptr : it->second, 0, 1, "hit by a projectile");
    }
    return 1;
}

I player_hit_part(Engine& eng, U e, I p, U o, I op) {
    Mem& m = eng.m;
    U inv = U(m.u32(e + 0x388));
    I ot = m.s32(o + 8);
    if (ot == 0x1772) {
        eng.note("player_hit_part: mounted gun use not implemented");
        m.w32(inv + 0x1C0, ot);
        m.w32(m.u32(e + 0x304) + 0x50, 7);
        return 1;
    }
    if (ot == 0x1405 || ot == 0x1402) {
        for (I i = 0; i < m.u16(o + 0x36); i++) {
            U q = part_of(m, o, i);
            if (m.s32(q + 8) == 5 && m.s32(q)) {
                op = i;
                break;
            }
        }
    }
    if (0x13F0 <= ot && ot < 0x13FA && handle_same(eng, U(m.u32(o + 0x388) + 0x54), e)) {
        m.w32(inv + 0x1C0, ot);
        m.w32(m.u32(e + 0x304) + 0x50, 7);
        return 0;
    }
    I hp0 = m.s32(e + 0xF4);
    if (ot == 0x31) {
        // an explosion whose fireballs still hold, within 300: its part's damage is 1000.0 for
        // this hit
        U q = part_of(m, o, op);
        I save = m.u32(q + 0x10);
        if (dist2d_approx(m, e, o) < 0x12C && m.s16(m.u32(o + 0x388) + 0xC + 6) > 0) m.w32(q + 0x10, 0x03E80000);
        if (!(m.u32(G_CHEATS) & 0x80)) damage_from_hit(eng, e, p ? p : 1, o, op);
        m.w32(q + 0x10, save);
    } else if (!(m.u32(G_CHEATS) & 0x80))
        damage_from_hit(eng, e, p, o, op);
    if (hp0 != m.s32(e + 0xF4)) {
        m.w16(e + 0x2E8, 0xC8);
        m.w32(inv + 0x1C0, ot);
        I f[3] = {m.s16(o + 0x344), m.s16(o + 0x34A), m.s16(o + 0x350)};
        m.w32(inv + 0x80, S32((S32(f[0] << 16) >> 0xB) * 3));
        m.w32(inv + 0x84, S32(f[1] * 0x60));
        m.w16(inv + 0x154, 8);
        m.w32(inv + 0x88, S32(f[2] * 0x60));
        U cam = eng.resolve(m.u32(inv + 0xA0));
        if (cam) {
            U co = U(m.u32(cam + 0x388));
            m.w16(co + 0xCC, 0x14);
            m.w16(co + 0xCE, 0xA);
        }
        if (((eng.rand.draw(FN("player_hit")) * 100) >> 15) < 0x1E) {
            I snd = ((eng.rand.draw(FN("player_hit")) * 100) >> 15) >= 0x33 ? 0x1B : 0x1C;
            eng.audio->play(0, snd, e, 0x20000, 0, 0x8000, 0x1000, 0, 0, 0, 0);
        }
        I snd, pitch;
        if (ot == 0x1402) {
            snd = 0x22, pitch = 0x3333;
            m.w32(part_of(m, o, op) + 8, 1);
        } else {
            snd = ((eng.rand.draw(FN("player_hit")) * 100) >> 15) < 0x32 ? 0xC : 0xD;
            pitch = 0x7FFF;
        }
        eng.audio->play(0, snd, e, 0x20000, 0, 0x10000, pitch, 0, 0, 0, 0);
        rumble_set(m, m.u8(inv + 0x152), 1, 0xFF, 2, 0);
    }
    return 1;
}

namespace {
I object_planes(Engine& eng, U e, U o) { return sphere_vs_object_planes(eng, e, o); }
}  // namespace

// enemy_touch (0x8005291C): against an object's planes: on top, forward speed 0 and +0x38 | 2;
// against a side, +0x3C | 2. With brain +4 bit 4 it turns along the side (engine_combat.py).
I enemy_touch(Engine& eng, U e, U o) {
    Mem& m = eng.m;
    V3 nrm;
    I r = sphere_vs_object_planes(eng, e, o, &nrm);
    if (!r) return 0;
    if (r & 1) {
        m.w32(e + 0x200, 0);
        m.w32(e + 0x38, m.u32(e + 0x38) | 2);
    } else if (r & 2)
        m.w32(e + 0x3C, m.u32(e + 0x3C) | 2);
    U b = U(m.u32(e + 0x38C));
    if (m.u32(b + 4) & 4) {
        I fx = m.s16(e + 0x344), fz = m.s16(e + 0x350);
        I dx = S16(m.s16(o + 0x20E) - m.s16(e + 0x20E));
        I dz = S16(m.s16(o + 0x216) - m.s16(e + 0x216));
        I side = sat16i((fz * dx - fx * dz) >> 12);
        I a = eng.gte.ratan2(int32_t(nrm[0]), int32_t(nrm[2]));
        if (side < 0)
            a = (m.u32(b + 8) & 0x10) ? a + 0x400 : a - 0x400;
        else
            a = (m.u32(b + 8) & 8) ? a - 0x400 : a + 0x400;
        m.w32(e + 0x22C, S32(a << 16));
        eng.matrix(e);
        m.w32(b + 8, m.u32(b + 8) | 0x20);
    }
    return 1;
}

I player_touch(Engine& eng, U e, U o) {
    Mem& m = eng.m;
    I ot = m.s32(o + 8);
    if ((ot == 0x1402 || ot == 0x1405) && m.s32(m.u32(o + 0x100) + 0x6C) == 0x80) {
        I dx = S16(m.s16(o + 0x20E) - m.s16(e + 0x20E));
        I dz = S16(m.s16(o + 0x216) - m.s16(e + 0x216));
        I a = iabs(dx), b = iabs(dz);
        if (a < b) std::swap(a, b);
        I dist = a + (b >> 2);
        I over = m.s16(o + 0x2EC) + m.s16(e + 0x2EC) - dist;
        if (over > 0) {
            V3 n = vnormal(eng.gte, {dx, 0, dz});
            over = imin(over, 0x41);
            m.w32(e + 0x1F0, S32(m.s32(e + 0x1F0) + (((-n[0] * over) >> 12) << 16)));
            m.w32(e + 0x200, 0);
            m.w32(e + 0x218, 0);
            m.w32(e + 0x220, 0);
            m.w32(e + 0x1F8, S32(m.s32(e + 0x1F8) + (((-n[2] * over) >> 12) << 16)));
            return 1;
        }
        return 0;
    }
    if (object_planes(eng, e, o)) {
        eng.matrix(e);
        return 1;
    }
    return 0;
}

namespace {
void pull_back_enemy(Mem& m, U p, U shooter) {
    I dx = iabs(S16(m.s16(p + 0x20E) - m.s16(shooter + 0x20E)));
    I dz = iabs(S16(m.s16(p + 0x216) - m.s16(shooter + 0x216)));
    I a = dx >= dz ? dx : dz, b = dx >= dz ? dz : dx;
    I t = a + (b >> 2);
    t -= t >> 2;
    U offs[3] = {0x344, 0x34A, 0x350};
    for (int k = 0; k < 3; k++) m.w32(p + 0x20C + 4 * k, S32(m.s32(p + 0x20C + 4 * k) + (((-m.s16(p + offs[k]) * t) >> 12) << 16)));
    U o = U(m.u32(p + 0x388));
    m.w16(o + 0x38, m.u16(o + 0x38) | 0x2000);
}
}  // namespace

void enemy_weapon(Engine& eng, U e) {
    Mem& m = eng.m;
    auto rd = [&]() { return eng.rand.draw(FN("enemy_weapon")); };
    U obj = U(m.u32(e + 0x388));
    U player = U(m.u32(G_SOLDIER));
    I kind = m.s32(e + 0x4C + 4 * 0xF) & 0xFF;
    U w = U(G_ENEMY_WEAPONS + 0x30 * kind);
    U tp = U(m.u32(m.u32(e + 0x1C) + 4));
    I E = m.s16(m.u32(e + 0x38C) + 0xC8);
    I state = m.u8(obj + 0x11);
    bool crouched = m.u32(m.u32(player + 0x388)) & 2;
    auto spread = [&](U p, U sp) {
        I a = m.s32(p + sp);
        m.w32(p + sp, E * 2 == 0 ? S32(a - (E << 16)) : S32(a + ((((rd() * 2 * E) >> 15) - E) << 16)));
    };
    auto shoot = [&](const V3* target) -> U {
        if (m.s32(w + 4) == 0x1773)  // a Panzerfaust
            return rocket_spawn(eng, e, m.s32(w + 8), m.s32(w + 0xC), target, 0, 0, 0, 0x13F1, 0x800, 0x10000, 0x08000451);
        U p = bullet_spawn(eng, e, m.s32(w + 8), m.s32(w + 0xC), target, obj + 0x1C, 0, 0, m.u8(tp + 0x64) << 16, 0, 0, 0x13F4, 0x800,
                           0, 0x08000401);
        if (p) pull_back_enemy(m, p, e);
        return p;
    };
    auto middle = [&]() -> V3 {
        return {m.s16(player + 0x20E), S16(S32(m.s32(player + 0x210) - (m.s32(player + 0x2D4) >> 1)) >> 16), m.s16(player + 0x216)};
    };
    if (state == 0) {
        I v = m.u8(obj + 0x14);
        m.w8(obj + 0x14, v ? v - 1 : 0);
        if (m.u8(obj + 0x13) == 0 && m.u8(obj + 0x12)) m.w8(obj + 0x12, m.u8(obj + 0x12) - 1);
    } else if (state == 2) {
        V3 mid = middle();
        U p = shoot(&mid);
        if (p) {
            if (crouched) {
                I a = m.s32(p + 0x228);
                I d = E == 0 ? (E >> 1) : ((-E) >> 1) + ((rd() * E) >> 15);
                m.w32(p + 0x228, S32(a + (d << 16)));
            } else
                spread(p, 0x228);
            spread(p, 0x22C);
        }
        m.w16(obj + 0xB68 + 2, 1);
        m.w8(obj + 0x11, 0);
    } else if (state == 1 || state == 3 || state == 4) {
        if (m.u8(obj + 0x12) == 0) {
            I v = m.u8(w + 0x20) + (m.s32(w + 0x24) ? (rd() * m.s32(w + 0x24)) >> 15 : 0);
            m.w8(obj + 0x12, v);
            v = m.u8(w + 0x18) + (m.s32(w + 0x1C) ? (rd() * m.s32(w + 0x1C)) >> 15 : 0);
            m.w8(obj + 0x13, v);
            m.w8(obj + 0x14, 0);
        } else if (m.u8(obj + 0x14))
            m.w8(obj + 0x14, m.u8(obj + 0x14) - 1);
        else if (!m.u8(obj + 0x13))
            m.w8(obj + 0x12, m.u8(obj + 0x12) - 1);
        else {
            I v = m.u8(w + 0x10) + (m.s32(w + 0x14) ? (rd() * m.s32(w + 0x14)) >> 15 : 0);
            m.w8(obj + 0x14, v);
            if (iabs(angle_to(eng, e, player)) < 0x15E || state == 3 || state == 4) {
                if (state == 3)
                    shoot(nullptr);
                else {
                    V3 mid = middle();
                    U p = shoot(&mid);
                    if (p) {
                        if (crouched) {
                            m.w32(p + 0x228, S32(m.s32(p + 0x228) + ((((rd() * 0x28) >> 15) - 0x14) << 16)));
                            m.w32(p + 0x22C, S32(m.s32(p + 0x22C) + ((((rd() * 0xB4) >> 15) - 0x5A) << 16)));
                        } else {
                            spread(p, 0x228);
                            spread(p, 0x22C);
                        }
                    }
                }
                m.w16(obj + 0xB68 + 2, 1);
                I snd = m.s32(w + 0x28);
                if (snd >= 0) {
                    I bank = m.s32(w + 0x2C);
                    if (bank < 0) bank = m.u16(m.u32(m.u32(e + 0x100) + 8) + 0xC8);
                    I cls = m.s32(w + 4) == 0x1773 ? 3 : 0;
                    eng.audio->play(S16(bank), snd, e, 0x10000, 0, 0x10000, 0x1800, 0, 0, cls, 0);
                }
                I left = (m.u8(obj + 0x13) - 1) & 0xFF;
                m.w8(obj + 0x13, left);
                if (!left) m.w32(e + 0x3C, m.u32(e + 0x3C) | 1);
            }
        }
    }
    m.w8(obj + 0x10, m.u8(obj + 0x11));
}

}  // namespace valor::sim
