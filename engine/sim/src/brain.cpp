// The enemies' movement (docs/re/enemies.md "The brain", entities.md "Moving", levels.md Collision):
// tools/sim/brain_sim.py's entity_move and brain_update on the engine's state, as
// tools/sim/engine_move.py and engine_enemy.py drive them (read_st, the rules, write_st).
#include "valor/movement.h"
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/simgte.h"

namespace valor::sim {

namespace {
constexpr U G_PATHS = 0x8009E290;

// brain_sim's state dictionary.
struct St {
    U addr, inst, brain;
    I type, flags_a, flags_b, attach, contact;
    I push[3], fwd_acc, fwd, fwd_max, floor, pos[3], vel[3], speed, ang[3], angvel[3], prev[3], root[3];
    I ground, eye, sphere, probe, rot[9], old[3], r3c8, vars[0x28], anim, frame;
};

St read_st(Mem& m, U e) {
    St st{};
    auto g = [&](U o) { return m.s32(e + o); };
    st.addr = e;
    st.inst = U(m.u32(e + 0x100));
    st.type = m.u32(e + 8), st.flags_a = m.u32(e + 0xC), st.flags_b = m.u32(e + 0x10);
    st.attach = m.u32(e + 0x18), st.contact = m.u32(e + 0x38);
    for (int k = 0; k < 3; k++) {
        st.push[k] = g(0x1F0 + 4 * k), st.pos[k] = g(0x20C + 4 * k), st.vel[k] = g(0x218 + 4 * k);
        st.ang[k] = g(0x228 + 4 * k), st.angvel[k] = g(0x234 + 4 * k), st.prev[k] = g(0x258 + 4 * k);
        st.root[k] = m.s16(e + 0x27C + 2 * k), st.old[k] = g(0x3BC + 4 * k);
    }
    st.fwd_acc = g(0x1FC), st.fwd = g(0x200), st.fwd_max = g(0x204), st.floor = g(0x208), st.speed = g(0x224);
    st.ground = g(0x284), st.eye = g(0x2D4), st.sphere = m.u16(e + 0x2E8), st.probe = m.s16(e + 0x2EC);
    for (int k = 0; k < 9; k++) st.rot[k] = m.s16(e + 0x340 + 2 * k);
    st.r3c8 = m.s16(e + 0x3C8);
    st.brain = U(m.u32(e + 0x38C));
    for (int k = 0; k < 0x28; k++) st.vars[k] = g(0x4C + 4 * k);
    st.anim = st.inst ? m.s16(st.inst + 0x5C) : -1;
    st.frame = st.inst ? m.s16(st.inst + 0x5E) : -1;
    return st;
}

void write_st(Mem& m, U e, const St& st) {
    m.w32(e + 0x38, st.contact);
    for (int k = 0; k < 3; k++) {
        m.w32(e + 0x1F0 + 4 * k, st.push[k]);
        m.w32(e + 0x20C + 4 * k, st.pos[k]);
        m.w32(e + 0x218 + 4 * k, st.vel[k]);
        m.w32(e + 0x228 + 4 * k, st.ang[k]);
        m.w32(e + 0x234 + 4 * k, st.angvel[k]);
        m.w32(e + 0x258 + 4 * k, st.prev[k]);
        m.w16(e + 0x27C + 2 * k, st.root[k]);
        m.w32(e + 0x3BC + 4 * k, st.old[k]);
    }
    m.w32(e + 0x200, st.fwd);
    m.w32(e + 0x208, st.floor);
    m.w32(e + 0x224, st.speed);
    m.w32(e + 0x284, st.ground);
    m.w16(e + 0x2E8, st.sphere);
    m.w16(e + 0x2EC, st.probe);
    m.w16(e + 0x3C8, st.r3c8);
    for (int k = 0; k < 0x28; k++) m.w32(e + 0x4C + 4 * k, st.vars[k]);
}

// The brain block on the engine's memory.
struct Br {
    Mem& m;
    U a;
    I s16(I o) const { return m.s16(a + o); }
    I u16(I o) const { return m.u16(a + o); }
    I s32(I o) const { return m.s32(a + o); }
    I u32(I o) const { return m.u32(a + o); }
    void w16(I o, I v) { m.w16(a + o, v); }
    void w32(I o, I v) { m.w32(a + o, v); }
    I goals() const { return u32(4); }
    I state() const { return u32(8); }
    void set_state(I v) { w32(8, v); }
};

I dist2(I dx, I dz) {
    I a = S16(iabs(S16(dx))), b = S16(iabs(S16(dz)));
    I lo = 0;
    if (a < 0) lo = a, a = 0;
    if (a < b) std::swap(a, b);
    return a + ((lo + b) >> 2);
}

void wall_planes(const World& w, const I c[3], I r, I h, const I delta[3], bool use_delta, std::vector<WallPlane>& out) {
    int32_t cc[3] = {int32_t(c[0]), int32_t(c[1]), int32_t(c[2])}, dd[3] = {int32_t(delta[0]), int32_t(delta[1]), int32_t(delta[2])};
    w.walls_gather(cc, int32_t(r), int32_t(h), dd, out, 32, use_delta);
}

int first_hit(const std::vector<WallPlane>& planes, const I old[3], const I pos[3], I r, I fp) {
    int32_t o[3] = {int32_t(old[0]), int32_t(old[1]), int32_t(old[2])}, p[3] = {int32_t(pos[0]), int32_t(pos[1]), int32_t(pos[2])};
    return wall_first_hit(planes, o, p, int32_t(r), int32_t(fp));
}

// Returns (planes of the final gather, result: 0 none, 1 still in a wall, 2 free).
std::pair<int, int> walker_slide(Engine& eng, St& st, const I delta[3]) {
    const World& world = eng.world.w;
    Mem& m = eng.m;
    I r = st.probe;
    I eye = S16(st.eye >> 16);
    I fp = eye - 16;
    I* pos = st.pos;
    I* old = st.old;
    I c[3] = {S16(pos[0] >> 16), S16((pos[1] >> 16) - fp), S16(pos[2] >> 16)};
    bool walk = false;
    Br b{m, st.brain};
    if (st.brain) {
        b.set_state(b.state() & ~I(0x1C));
        walk = b.goals() & 4;
    }
    std::vector<WallPlane> planes;
    I fwd[3] = {0, 0, 0};
    if (walk) {
        I g = b.s16(0xC0);
        I gait = b.s16(0xCC + 2 * g);
        if (b.s32(0xD8))
            fwd[0] = S16(-st.rot[2]), fwd[2] = S16(-st.rot[8]);
        else
            fwd[0] = st.rot[2], fwd[2] = st.rot[8];
        b.w16(0xCC + 2 * g, b.u16(0xD0 + 2 * g));
        wall_planes(world, c, imax(gait, r), fp - 4, delta, false, planes);
    } else
        wall_planes(world, c, r, eye - 20, delta, true, planes);
    if (planes.empty()) return {0, 0};
    int count = int(planes.size());
    if (walk) {
        I best[3] = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};
        std::vector<WallPlane> keep;
        for (const WallPlane& p : planes) {
            I n[3] = {p.n[0], p.n[1], p.n[2]}, d = p.d;
            if (iabs(n[1]) < 0x5A80 && (S32(fwd[0] * n[0] + fwd[2] * n[2]) >> 12) < 0x2EE0) {
                I cross = S32(n[0] * fwd[2] - n[2] * fwd[0]) >> 12;
                I mm = S32(c[0] * n[0] + c[1] * n[1] + c[2] * n[2]) + d;
                I a0 = S32(mm >= 0 ? mm : -mm);
                int slot = -1;
                U off = 0;
                I bit = 0;
                if (iabs(cross) < 0x5001)
                    slot = 0, off = 0xF4, bit = 4;
                else if (cross >= 0x5001)
                    slot = 1, off = 0x104, bit = 8;
                else if (cross < -0x5000)
                    slot = 2, off = 0x114, bit = 0x10;
                if (slot >= 0 && a0 < best[slot]) {
                    best[slot] = a0;
                    b.w16(0xEC, fwd[0]);
                    b.w16(0xEE, 0);
                    b.w16(0xF0, fwd[2]);
                    for (int k = 0; k < 3; k++) b.w16(off + 2 * k, n[k]);
                    b.w16(off + 6, d);
                    b.w32(off + 8, a0);
                    b.w32(off + 12, cross);
                    b.set_state(b.state() | bit);
                }
            }
            int32_t cc[3] = {int32_t(c[0]), int32_t(c[1]), int32_t(c[2])};
            V3s nn{int16_t(n[0]), int16_t(n[1]), int16_t(n[2])};
            const CollFace& f = p.chunk->faces[p.face];
            bool ok = wall_tri_near(*p.chunk, f, cc, int32_t(r), int32_t(fp - 4)) &&
                      wall_plane_dot(nn, int32_t(delta[0]), int32_t(delta[1]), int32_t(delta[2])) < 0 &&
                      wall_plane_crosses(cc, int32_t(r), int32_t(fp - 4), nn, int32_t(d));
            if (ok) keep.push_back(p);
        }
        planes = keep;
    }
    int left = count;
    bool pushed = false;
    for (;;) {
        int i = planes.empty() ? -1 : first_hit(planes, old, pos, r, fp);
        if (i < 0) break;
        pushed = true;
        left--;
        int32_t p32[3] = {int32_t(pos[0]), int32_t(pos[1]), int32_t(pos[2])};
        wall_push_out(planes[size_t(i)], p32, int32_t(fp), int32_t(r + 4));
        for (int k = 0; k < 3; k++) pos[k] = p32[k];
        if (left < 0) break;
    }
    if (!pushed) return {0, 2};
    I delta2[3], c2[3];
    for (int k = 0; k < 3; k++) delta2[k] = S16((pos[k] >> 16) - (old[k] >> 16)), c2[k] = S16(pos[k] >> 16);
    std::vector<WallPlane> planes2;
    wall_planes(world, c2, r, fp - 4, delta2, true, planes2);
    if (planes2.empty()) return {0, 2};
    if (first_hit(planes2, old, pos, r, fp) < 0) return {int(planes2.size()), 2};
    return {int(planes2.size()), 1};
}

// brain_sim.entity_move: an enemy's move; returns the engine's contact bits (1 landing, 2 slide).
I brain_entity_move(Engine& eng, St& st, const I* turn) {
    for (int k = 0; k < 3; k++) st.prev[k] = st.pos[k];
    I on_ground0 = st.ground;
    I* ang = st.ang;
    for (int k = 0; k < 3; k++) ang[k] = S32(ang[k] + st.angvel[k]);
    if (turn)
        for (int k = 0; k < 3; k++) ang[k] = S32(ang[k] + (turn[k] << 16)) & 0x0FFF0000;
    if (st.fwd_acc) st.fwd = imin(S32(st.fwd + st.fwd_acc), st.fwd_max);
    I* vel = st.vel;
    vel[2] = st.fwd;
    if (!st.ground && (st.flags_a & 0x20)) vel[1] = imin(S32(vel[1] + 0x20000), 0x4B0000);
    I sp = st.speed;
    I v[3];
    if (sp == 0x10000)
        for (int k = 0; k < 3; k++) v[k] = S16((vel[k] & 0xFFFFFFFF) >> 12);
    else
        for (int k = 0; k < 3; k++) v[k] = S16((S16(vel[k] >> 16) * sp) >> 12);
    auto mr = rot_yxz9(S16(ang[0] >> 16), S16(ang[1] >> 16), S16(ang[2] >> 16));
    I mv[3];
    for (int i = 0; i < 3; i++) mv[i] = (mr[3 * i] * v[0] + mr[3 * i + 1] * v[1] + mr[3 * i + 2] * v[2]) >> 12;
    I* pos = st.pos;
    I p0[3] = {pos[0], pos[1], pos[2]};
    for (int k = 0; k < 3; k++) pos[k] = S32(pos[k] + (mv[k] << 12) + st.push[k]);
    I dy4 = mv[1] >> 4;
    I* old = st.old;
    I delta[3];
    for (int k = 0; k < 3; k++) delta[k] = S16((pos[k] >> 16) - (old[k] >> 16));
    bool walker = (st.flags_a & 8) && !(st.flags_a & 1) && !(st.flags_b & 0x45);
    I res = 0;
    const char* event = nullptr;
    if (walker) {
        auto [n, r] = walker_slide(eng, st, delta);
        if (r == 2) st.contact |= 0x8000;
        if (n > 0) res |= 2;
        if (n > 0) {
            if (delta[1] >= 0x801 && dy4 < 0 && vel[1] > 0) {
                vel[1] = 0;
                pos[1] = old[1];
            }
            vel[0] = vel[2] = 0;
            st.contact |= 0x8000;
            if (st.flags_a & 0x80) {
                pos[0] = S32(p0[0] + (((delta[0] << 5) + 0x8000) & 0xFFFF0000));
                pos[2] = S32(p0[2] + (((delta[2] << 5) + 0x8000) & 0xFFFF0000));
            }
        }
    } else if (st.flags_b & 0x44)
        eng.note("brain: world_sweep mover");
    if (!(st.attach & 1) && !(st.flags_b & 0x10) && (st.ground || (st.flags_a & 8))) {
        int32_t p32[3] = {int32_t(pos[0]), int32_t(pos[1]), int32_t(pos[2])};
        Floor fl = eng.world.w.floor_find(p32);
        I f = fl.height;
        if (f == -0x7FFF) {
            if ((st.flags_a & 7) && !(st.flags_b & 0x10000)) {
                for (int k = 0; k < 3; k++) pos[k] = old[k];
                if (!on_ground0) pos[1] = st.floor;
                st.ground = 1;
                st.contact |= 0x80008000;
                event = "no floor";
            }
        } else {
            st.floor = S32(f << 16);
            I d = S16(pos[1] >> 16) - f;
            if (st.ground) {
                if (d >= -32)
                    pos[1] = S32(f << 16);
                else if ((st.flags_b & 0x200000) && d < -57) {
                    vel[0] = vel[1] = vel[2] = 0;
                    st.fwd = 0;
                    for (int k = 0; k < 3; k++) pos[k] = old[k];
                    st.ground = on_ground0;
                    st.contact |= 0x80000000;
                } else {
                    st.ground = 0;
                    st.contact |= 1;
                }
            } else if (st.flags_a & 0x20) {
                I lim = dy4 < 48 ? dy4 + 16 : 64;
                if (d >= 0 || lim >= iabs(d)) {
                    if ((st.flags_a & 0x80) && iabs(dy4) >= 8) {
                        st.ground = 0;
                        pos[1] = S32(f << 16);
                        vel[1] = -iabs(vel[1]);
                    } else {
                        st.ground = 1;
                        pos[1] = S32(f << 16);
                        st.push[1] = 0;
                        vel[1] = 0;
                        st.contact &= ~I(1);
                        event = "landing";
                    }
                }
            }
        }
    }
    for (int k = 0; k < 3; k++) st.push[k] = 0, old[k] = pos[k];
    if (event && std::string(event) == "landing") res |= 1;
    return res;
}

// ---------------------------------------------------------------- the brain (0x80040DF4)

void set_var(St& st, I var, I value) {
    if (0 <= var && var < 0x28) st.vars[var] = S32(value);
}

I brain_steer(Engine& eng, St& st) {
    Br b{eng.m, st.brain};
    I s = b.state();
    if ((s & 0x18) == 0x18) s &= b.s32(0x11C) < b.s32(0x10C) ? ~I(8) : ~I(0x10);
    if (s & 4) {
        st.angvel[1] = 0;
        I t = eng.gte.ratan2(int32_t(b.s16(0xF4)), int32_t(b.s16(0xF8)));
        if (s & 0x10) {
            if (!(s & 2)) st.ang[1] = S32((t + 0x400) << 16);
            b.set_state(b.state() & ~I(1));
        } else if (s & 8) {
            if (!(s & 1)) st.ang[1] = S32((t - 0x400) << 16);
            b.set_state(b.state() & ~I(2));
        } else {
            if (b.s32(0x100) > 0) {
                st.ang[1] = S32((t - 0x400) << 16);
                b.set_state(b.state() & ~I(2));
            } else {
                st.ang[1] = S32((t + 0x400) << 16);
                b.set_state(b.state() & ~I(1));
            }
        }
        return 0;
    }
    if (s & 8) {
        if (!(s & 1)) st.ang[1] = S32((eng.gte.ratan2(int32_t(b.s16(0x104)), int32_t(b.s16(0x108))) - 0x400) << 16);
        b.set_state(b.state() & ~I(2));
        return 0;
    }
    if (s & 0x10) {
        if (!(s & 2)) st.ang[1] = S32((eng.gte.ratan2(int32_t(b.s16(0x114)), int32_t(b.s16(0x118))) + 0x400) << 16);
        b.set_state(b.state() & ~I(1));
        return 0;
    }
    return 1;
}

void brain_seek(Engine& eng, St& st, const I fwd[3], const I d[3], I rate, I dist) {
    Br b{eng.m, st.brain};
    I cross = S32(d[0] * fwd[2] - d[2] * fwd[0]) >> 12;
    I slow = b.s16(0xC2);
    I g = b.s16(0xC0);
    if (slow != 0) {
        if (!(slow < dist)) {
            I sp = S32(cdiv(S32(dist << 16), slow));
            st.speed = sp;
            b.w32(0x18, sp);
            st.probe = S16((b.s16(0xC4) * sp) >> 16);
            st.r3c8 = S16((b.s16(0xCA) * sp) >> 16);
            b.w16(0xCC + 2 * g, (b.s16(0xD0 + 2 * g) * sp) >> 16);
        } else {
            st.speed = 0x10000;
            b.w32(0x18, b.s32(0xDC));
            st.probe = b.s16(0xC4);
            st.sphere = b.u16(0xC6);
            b.w16(0xCC + 2 * g, b.u16(0xD0 + 2 * g));
        }
    }
    I c = dist >= 2001 ? cross >> 2 : cross >> 1;
    I s1 = rate;
    if (c < -0x20) {
        b.set_state(b.state() | 2);
        s1 = c + S16(rate) >= 0 ? (c & 0xFFFF) : -rate;
    } else if (c > 0x20) {
        b.set_state(b.state() | 1);
        s1 = c - S16(rate) <= 0 ? (c & 0xFFFF) : rate;
    } else {
        I dot = S32(d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]);
        if (dot < 0) {
            if (c >= 0)
                b.set_state(b.state() | 1);
            else {
                s1 = -rate;
                b.set_state(b.state() | 2);
            }
        } else
            s1 = 0;
    }
    if (b.goals() & 4) brain_steer(eng, st);
    if (b.state() & 3) {
        I h = S32(st.ang[1] + (s1 << 16));
        while (h < -0x8000000) h += 0x10000000;
        while (h > 0x8000000) h -= 0x10000000;
        st.ang[1] = h;
    }
}

void brain_move_to(Engine& eng, St& st, bool flee) {
    Mem& m = eng.m;
    Br b{m, st.brain};
    U tp = U(b.u32(0x10));
    if (tp == 0 || (m.u32(tp + 0xC) & 0x180000)) {
        st.contact |= 0x10;
        return;
    }
    I tx = S16(m.s32(tp + 0x20C) >> 16), tz = S16(m.s32(tp + 0x214) >> 16);
    I fwd[3];
    if (flee)
        fwd[0] = S16(-st.rot[2]), fwd[1] = 0, fwd[2] = S16(-st.rot[8]);
    else
        fwd[0] = st.rot[2], fwd[1] = 0, fwd[2] = st.rot[8];
    I dx = S16(tx - S16(st.pos[0] >> 16)), dz = S16(tz - S16(st.pos[2] >> 16));
    I d[3] = {dx, 0, dz};
    brain_seek(eng, st, fwd, d, b.s16(0xD4), dist2(dx, dz));
}

void brain_goto_point(Engine& eng, St& st) {
    Mem& m = eng.m;
    Br b{m, st.brain};
    U p = U(b.u32(0x40));
    I dx = S16(m.u16(p) - S16(st.pos[0] >> 16)), dz = S16(m.u16(p + 4) - S16(st.pos[2] >> 16));
    I dist = dist2(dx, dz);
    I var = b.s32(0x80);
    set_var(st, var, dist);
    if (dist < b.s16(0xA8)) {
        set_var(st, var, 0);
        st.contact |= 0x400000;
    }
    I fwd[3] = {st.rot[2], 0, st.rot[8]}, d[3] = {dx, 0, dz};
    brain_seek(eng, st, fwd, d, 0x100, dist);
}

void brain_goto_node(Engine& eng, St& st) {
    Mem& m = eng.m;
    Br b{m, st.brain};
    I node = b.s32(0x3C);
    U nodes = U(m.u32(m.u32(m.u32(G_WORLD) + 0x388) + 8));
    U nd = U(nodes + node * 0x34);
    I via = b.s32(0x8C);
    I tx = via ? b.u16(0xAC) : b.u16(0xA0), tz = via ? b.u16(0xAE) : b.u16(0xA4);
    I dx = S16(tx - S16(st.pos[0] >> 16)), dz = S16(tz - S16(st.pos[2] >> 16));
    I dist = dist2(dx, dz);
    I var = b.s32(0x7C);
    set_var(st, var, dist);
    I radius = b.s16(0xA8);
    if (via) {
        I dot = S32(m.s16(nd + 0x24) * -dx + m.s16(nd + 0x28) * -dz);
        if (dist < radius || dot < 0) b.w32(0x8C, 0);
    } else if (dist < radius) {
        set_var(st, var, 0);
        st.contact |= 0x400000;
    }
    I fwd[3] = {st.rot[2], 0, st.rot[8]}, d[3] = {dx, 0, dz};
    brain_seek(eng, st, fwd, d, 0x100, dist);
}

void brain_follow_path(Engine& eng, St& st) {
    Mem& m = eng.m;
    Br b{m, st.brain};
    U P = U(m.u32(G_PATHS) + 8 * b.s16(0xB8));
    U pts = U(m.u32(P + 4));
    I count = m.s16(P + 2);
    I x = S16(st.pos[0] >> 16), z = S16(st.pos[2] >> 16);
    I moved = dist2(x - S16(st.prev[0] >> 16), z - S16(st.prev[2] >> 16));
    I last = count - 1;
    for (I it = 0; it < 4 * count + 8; it++) {
        I i = b.s16(0xBC);
        U pt = U(pts + i * 0x14);
        I dx = S16(m.u16(pt) - x), dz = S16(m.u16(pt + 4) - z);
        I dist = dist2(dx, dz);
        I j = i - b.s16(0xBE);
        if (j < 0)
            j = 1;
        else if (j >= count)
            j = count - 2;
        U pj = U(pts + j * 0x14);
        I px = S16(m.u16(pt) - m.u16(pj)), pz = S16(m.u16(pt + 4) - m.u16(pj + 4));
        I dot = S32(dx * px + dz * pz);
        if (moved < dist && dist >= 0x4C && dot > 0) {
            I fwd[3] = {st.rot[2], 0, st.rot[8]}, d[3] = {dx, 0, dz};
            brain_seek(eng, st, fwd, d, 0xC0, dist);
            return;
        }
        I mode = b.s16(0xBA);
        if (mode == 0) {
            if (i >= last) {
                st.contact |= 0x1000000;
                b.w16(0xBC, 0);
            } else {
                st.contact |= 0x400000;
                b.w16(0xBC, i + 1);
            }
        } else if (mode == 1) {
            if (i <= 0) {
                st.contact |= 0x800000;
                b.w16(0xBC, count - 1);
            } else {
                st.contact |= 0x400000;
                b.w16(0xBC, i - 1);
            }
        } else if (mode == 2) {
            if (i <= 0) {
                st.contact |= 0x800000;
                b.w16(0xBC, 1);
                b.w16(0xBE, 1);
            } else if (i >= last) {
                st.contact |= 0x1000000;
                b.w16(0xBC, count - 2);
                b.w16(0xBE, -1);
            } else {
                st.contact |= 0x400000;
                b.w16(0xBC, i + b.s16(0xBE));
            }
        }
    }
    eng.note("brain: path loop hangs");
}

void brain_update_st(Engine& eng, St& st) {
    Br b{eng.m, st.brain};
    for (int k = 0; k < 3; k++) b.w32(0x94 + 4 * k, st.pos[k]);
    I goals = b.goals();
    b.set_state(b.state() & ~I(3));
    if (!(goals & 4)) b.set_state(b.state() & ~I(0x1C));
    if (goals & 1) brain_move_to(eng, st, false);
    if (goals & 0x4000) brain_move_to(eng, st, true);
    if (goals & 0x1000) brain_goto_point(eng, st);
    if (goals & 0x800) brain_goto_node(eng, st);
    if ((goals & 4) && !(goals & 0x5C03)) brain_steer(eng, st);
    if (goals & 2) brain_follow_path(eng, st);
    b.set_state(b.state() & ~I(0x20));
}

}  // namespace

bool frame_data(Mem& m, U inst, I anim, I frame, I mv[3], I off[3], I turn[3]) {
    if (anim < 0 || frame < 0 || !m.u32(inst + 0x74)) return false;
    U entry = U(m.u32(m.u32(inst + 0x78) + 4 * anim));
    if (!entry) return false;
    U f = U(m.u32(entry + 4) + 0x14 * frame);
    I w0 = m.u32(f), w1 = m.u32(f + 4);
    auto unpack = [](I w, I o[3]) {
        o[0] = S32(w << 22) >> 22, o[1] = S32(w << 11) >> 21, o[2] = S32(w << 1) >> 22;
    };
    unpack(w0, mv);
    unpack(w1, off);
    turn[0] = m.s16(f + 8), turn[1] = m.s16(f + 0xA), turn[2] = m.s16(f + 0xC);
    return true;
}

I walker_move(Engine& eng, U e) {
    St st = read_st(eng.m, e);
    I mv[3], off[3], turn[3];
    bool fd = st.inst && frame_data(eng.m, st.inst, st.anim, st.frame, mv, off, turn);
    I res = brain_entity_move(eng, st, fd ? turn : nullptr);
    write_st(eng.m, e, st);
    I pos[3] = {st.pos[0], st.pos[1], st.pos[2]};
    eng.m.w32(e + 0x390, eng.world.floor(pos).zone);
    return res;
}

I entity_move(Engine& eng, U e) {
    Mem& m = eng.m;
    I fa = m.u32(e + 0xC);
    if (fa & 1) {
        player_move(eng, e);
        return 0;
    }
    if ((fa & 8) && !(m.u32(e + 0x10) & 0x45)) return walker_move(eng, e);
    return generic_move(eng, e);
}

void brain_update_entity(Engine& eng, U e) {
    St st = read_st(eng.m, e);
    brain_update_st(eng, st);
    write_st(eng.m, e, st);
}

I path_nearest_point(Mem& m, U e, I path) {
    U P = U(m.u32(G_PATHS) + 8 * path);
    U pts = U(m.u32(P + 4));
    I n = m.s16(P + 2);
    I best = 0x7FFF, bi = -1;
    for (I i = 0; i < n; i++) {
        U p = U(pts + 0x14 * i);
        I dx = S16(m.s32(e + 0x20C) >> 16) - m.u16(p), dy = S16(m.s32(e + 0x210) >> 16) - m.u16(p + 2),
          dz = S16(m.s32(e + 0x214) >> 16) - m.u16(p + 4);
        I a[3] = {iabs(S16(dx)), iabs(S16(dy)), iabs(S16(dz))};
        std::sort(a, a + 3);
        I d = a[2] + ((a[0] + a[1]) >> 2);
        if ((d & 0xFFFF) < (best & 0xFFFF)) best = d, bi = i;
    }
    return bi;
}

}  // namespace valor::sim
