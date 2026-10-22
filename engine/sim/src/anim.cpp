// Animation playback's effects on the entity (docs/re/animation.md): root movement and the hit
// spheres (tools/sim/engine_anim.py), and what the drawing pass does to the game state: poses and
// joints of the models drawn, the drawn flags, the out-of-sight counters and their removals
// (tools/sim/engine_draw.py). Which models the renderer drew is taken from the original
// (VALOR_TRACE_DRAWN, build/engine/drawn.log): it is the recording's view frustum.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

#include "valor/model.h"
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/scripts.h"
#include "valor/sim/simgte.h"

namespace valor::sim {

namespace {

std::vector<Quat> slot_quats(Mem& m, U ptr, I n) {
    std::vector<Quat> q(size_t(imax(n, 0)));
    for (I i = 0; i < n; i++)
        for (int k = 0; k < 4; k++) q[size_t(i)][size_t(k)] = int16_t(m.s16(ptr + 8 * i + 2 * k));
    return q;
}

// combat_sim.Model: a model as the game holds it in RAM.
U model_frame(Mem& m, U model, I anim, I f) {
    U entry = U(m.u32(m.u32(model + 8) + 4 + 4 * anim));
    return U(m.u32(entry + 4) + 20 * f);
}

std::vector<Quat> quat_decode(Mem& m, U p, I n) {
    std::vector<Quat> out;
    auto sx = [](I v, int b) { return (v >> (b - 1)) ? v - (I(1) << b) : v; };
    I i = 0;
    while (I(out.size()) < n) {
        I w0 = m.u32(p + 4 * i), w1 = m.u32(p + 4 * i + 4);
        I w = sx(w0 >> 20, 12), x = sx((w0 >> 4) & 0xFFF, 12), y = sx(w1 & 0xFFF, 12);
        I z = sx((((w1 >> 12) & 0xF) << 8) | (((w0 >> 16) & 0xF) << 4) | (w0 & 0xF), 12);
        out.push_back({int16_t(x * 2), int16_t(y * 2), int16_t(z * 2), int16_t(w * 2)});
        if (I(out.size()) == n) break;
        I w2 = m.u32(p + 4 * i + 8);
        x = sx(w1 >> 20, 12), y = sx((w2 >> 16) & 0xFFF, 12), w = sx((w2 >> 4) & 0xFFF, 12);
        z = sx(((w2 >> 28) << 8) | ((w2 & 0xF) << 4) | ((w1 >> 16) & 0xF), 12);
        out.push_back({int16_t(x * 2), int16_t(y * 2), int16_t(z * 2), int16_t(w * 2)});
        i += 3;
    }
    return out;
}

std::vector<Quat> frame_pose(Engine& eng, U model, I anim, I f) {
    Mem& m = eng.m;
    U fp = model_frame(m, model, anim, f);
    I il = m.u8(fp + 0xE), n = m.u8(fp + 0xF);
    I nxt = f + (il >> 4), prv = f - (il & 0xF);
    if (m.u32(fp + 0x10)) return quat_decode(m, U(m.u32(fp + 0x10)), n);
    I k = nxt - prv == 2 ? 2 : f - prv;
    if (k < 0) k = prv - f;
    auto a = quat_decode(m, U(m.u32(model_frame(m, model, anim, prv) + 0x10)), n);
    auto b = quat_decode(m, U(m.u32(model_frame(m, model, anim, nxt) + 0x10)), n);
    std::vector<Quat> out;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) out.push_back(quat_interp(eng.gte, a[i], b[i], int(k)));
    return out;
}

V3 unpack3(I w) {
    auto f = [&](int lo, int n) {
        I v = (w >> lo) & ((I(1) << n) - 1);
        return (v >> (n - 1)) ? v - (I(1) << n) : v;
    };
    return {f(0, 10), f(10, 11), f(21, 10)};
}

// skeleton_pose / skeleton_walk: joints by bone index.
std::map<I, V3> model_joints(Mem& m, U model, const std::vector<Quat>& quats, const I root_off[3], I nbones) {
    std::map<I, V3> out;
    M3 ident = {{{0x1000, 0, 0}, {0, 0x1000, 0}, {0, 0, 0x1000}}};
    std::function<bool(U, const M3&, const V3&)> walk = [&](U b, const M3& prot, const V3& ppos) -> bool {
        while (b) {
            I idx = m.u16(b);
            I off[3] = {m.s16(b + 2), m.s16(b + 4), m.s16(b + 6)};
            U child = U(m.u32(b + 0xC)), sib = U(m.u32(b + 0x10));
            if (idx > nbones) return false;
            M3 rot = idx < I(quats.size()) ? quat_m3(quats[size_t(idx)][0], quats[size_t(idx)][1], quats[size_t(idx)][2], quats[size_t(idx)][3])
                                           : ident;
            V3 pos;
            for (int i = 0; i < 3; i++)
                pos[i] = ppos[i] + ((prot[i][0] * (off[0] * 16) + prot[i][1] * (off[1] * 16) + prot[i][2] * (off[2] * 16)) << 3);
            if (child) walk(child, rot, pos);
            out[idx] = {S16((2 * pos[0] + 0x80000) >> 20), S16((2 * pos[1] + 0x80000) >> 20), S16((2 * pos[2] + 0x80000) >> 20)};
            b = sib;
        }
        return true;
    };
    U root = U(m.u32(model + 0x9C));
    if (root) walk(root, ident, {root_off[0] << 19, root_off[1] << 19, root_off[2] << 19});
    return out;
}

void write_quats(Mem& m, U p, const std::vector<Quat>& q) {
    for (size_t i = 0; i < q.size(); i++)
        for (int k = 0; k < 4; k++) m.w16(p + 8 * i + 2 * k, q[i][size_t(k)]);
}

void joints(Engine& eng, U, U inst, U model) {
    Mem& m = eng.m;
    I nb = m.u16(inst + 0x72);
    U qp = U(m.u32(inst + 0x88));
    if (!qp) qp = U(m.u32(inst + 0x90));
    auto quats = slot_quats(m, qp, nb);
    I anim = m.s16(inst + 0x62), frame = m.s16(inst + 0x64);
    if (anim < 0) anim = m.s16(inst + 0x5C), frame = m.s16(inst + 0x5E);
    I nq = anim >= 0 ? m.u8(model_frame(m, model, anim, frame) + 0xF) : nb;
    I root[3] = {m.s16(inst + 0x98), m.s16(inst + 0x9A), m.s16(inst + 0x9C)};
    auto js = model_joints(m, model, quats, root, nq);
    U jp = U(m.u32(inst + 0x94));
    for (auto& [i, j] : js)
        for (int k = 0; k < 3; k++) m.w16(jp + 8 * i + 2 * k, j[k]);
}

void pose_update(Engine& eng, U e, U inst, bool drawn) {
    Mem& m = eng.m;
    I fc = m.u32(G_FRAME_COUNT);
    if (m.u32(e + 0x3A0) == fc) return;
    m.w32(e + 0x3A0, fc);
    U model = U(m.u32(inst + 8));
    I nb = m.u16(inst + 0x72);
    I anim = m.s16(inst + 0x5C), frame = m.s16(inst + 0x5E);
    I c = m.s16(inst + 0x3E);
    if (anim < 0) {
        m.w16(inst + 0x62, -1);
        return;
    }
    U buf = U(m.u32(inst + 0x90));
    if (c > 0 && m.s16(inst + 0x62) == -1) {
        c = 0;
        m.w16(inst + 0x3E, 0);
        if (!m.u32(inst + 0x88)) {
            write_quats(m, buf, frame_pose(eng, model, anim, frame));
            joints(eng, e, inst, model);
            return;
        }
    }
    if (c == 0) {
        if (m.s16(inst + 0x62) != anim || m.s16(inst + 0x64) != frame) {
            write_quats(m, buf, frame_pose(eng, model, anim, frame));
            m.w32(inst + 0x88, buf);
            m.w16(inst + 0x62, anim);
            m.w16(inst + 0x64, frame);
        }
    } else {
        I tgt = m.s16(inst + 0x40);
        if (tgt < 0) tgt = m.s16(inst + 0x60);
        I k = 4 - c;
        I a0[3] = {m.s16(inst + 0xA0), m.s16(inst + 0xA2), m.s16(inst + 0xA4)};
        V3 t = unpack3(m.u32(model_frame(m, model, anim, tgt) + 4));
        for (int j = 0; j < 3; j++) {
            I x = a0[j], y = t[j], r;
            if (k == 0)
                r = x;
            else if (k == 1)
                r = S16(x + ((y - x) >> 2));
            else if (k == 2)
                r = S16(x + ((y - x) >> 1));
            else
                r = S16(y - ((y - x) >> 2));
            m.w16(inst + 0x98 + 2 * j, r);
        }
        if (k == 0)
            m.w32(inst + 0x88, m.u32(inst + 0x8C));
        else {
            auto saved = slot_quats(m, U(m.u32(inst + 0x8C)), nb);
            auto b = frame_pose(eng, model, anim, tgt);
            I n = m.u8(model_frame(m, model, anim, tgt) + 0xF);
            std::vector<Quat> out;
            for (I i = 0; i < n && i < I(saved.size()) && i < I(b.size()); i++)
                out.push_back(quat_interp(eng.gte, saved[size_t(i)], b[size_t(i)], int(k)));
            write_quats(m, buf, out);
            m.w32(inst + 0x88, buf);
        }
        m.w16(inst + 0x62, anim);
        m.w16(inst + 0x64, tgt);
    }
    if (drawn) joints(eng, e, inst, model);
}

bool keep_near(Engine& eng, U e) {
    Mem& m = eng.m;
    U cam = U(m.u32(0x8009D638));
    U co = U(m.u32(cam + 0x388));
    I far = m.s32(co + 0xC4) > 0xFFFF ? m.s32(co + 0xC4) >> 16 : m.s32(co + 0xC4);
    I dv[3], d[3];
    for (int k = 0; k < 3; k++) dv[k] = S16(m.s16(e + 0x20E + 4 * k) - m.s16(cam + 0x20E + 4 * k)), d[k] = iabs(dv[k]);
    std::sort(d, d + 3);
    I t = d[2] + ((d[0] + d[1]) >> 2);
    // make room: a pickup (flags B 0x40000) at 3501 or more while soldiers >= cap - 1 and a spawn
    // waits, behind the camera's target: marked drawn and not kept (counted out this pass)
    if ((m.u32(e + 0x10) & 0x40000) && m.s32(G_SOLDIER_COUNT) >= m.s32(G_SOLDIER_CAP) - 1 && m.u32(0x8009E288) && t >= 0xDAD) {
        I h = m.u32(co + 0xC);
        U tgt = h ? U(m.u32(G_HANDLES + 4 * (h & 0xFFFF))) : 0;
        I dot = 0;
        for (int k = 0; k < 3; k++) dot += dv[k] * m.s16(tgt + 0x344 + 6 * k);
        if (S32(dot) < 0) {
            m.w32(e + 0x298, 1);
            m.w32(e + 0x290, 1);
            return false;
        }
    }
    return t <= far;
}

struct DrawnLog {
    // level -> tick -> handle -> (298, 29C, 290)
    std::map<std::string, std::map<I, std::map<U, std::array<I, 3>>>> by_level;
    bool loaded = false;
    void load(const std::string& path) {
        loaded = true;
        std::ifstream f(path);
        std::string line, level;
        while (std::getline(f, line)) {
            int mm, ml;
            if (line.rfind("replay: mark mission", 0) == 0 && std::sscanf(line.c_str(), "replay: mark mission %d %d", &mm, &ml) == 2) {
                level = std::to_string(mm) + ":" + std::to_string(ml);
                continue;
            }
            if (line.rfind("drawn ", 0) != 0) continue;
            long long vb, tick;
            int n = 0;
            if (std::sscanf(line.c_str(), "drawn %lld %lld:%n", &vb, &tick, &n) < 2) continue;
            auto& d = by_level[level][tick];
            const char* p = line.c_str() + n;
            for (;;) {
                unsigned h, a, b;
                int c, used = 0;
                if (std::sscanf(p, " %x:%x:%x:%d%n", &h, &a, &b, &c, &used) != 4) break;
                d[h] = {a, b, c};
                p += used;
            }
        }
    }
};

DrawnLog& drawn_log(Engine& eng) {
    static DrawnLog log;
    if (!log.loaded) log.load(eng.root + "/build/engine/drawn.log");
    return log;
}

}  // namespace

void root_motion(Engine& eng, U e) {
    Mem& m = eng.m;
    if (!(m.u32(e + 0xC) & 0x1000)) return;
    U inst = U(m.u32(e + 0x100));
    I mv[3], off[3], turn[3];
    if (!frame_data(m, inst, m.s16(inst + 0x5C), m.s16(inst + 0x5E), mv, off, turn)) return;
    for (int k = 0; k < 3; k++) m.w16(inst + 0x98 + 2 * k, off[k]);
    if (mv[1] < 0) m.w32(e + 0x284, 0);
    I sp = m.s32(e + 0x224);
    if (sp != 0x10000)
        for (I& v : mv) v = (v * sp) >> 16;
    for (int k = 0; k < 3; k++) m.w16(e + 0x27C + 2 * k, mv[k]);
    I rot[9];
    for (int k = 0; k < 9; k++) rot[k] = m.s16(e + 0x340 + 2 * k);
    V3 v = {S16(mv[0]), S16(mv[1]), S16(mv[2])};
    for (int i = 0; i < 3; i++) {
        I r = sat16i((rot[3 * i] * v[0] + rot[3 * i + 1] * v[1] + rot[3 * i + 2] * v[2]) >> 12);
        m.w32(e + 0x1F0 + 4 * i, S32(m.s32(e + 0x1F0 + 4 * i) + (r << 16)));
    }
}

void entity_bounds(Engine& eng, U e) {
    Mem& m = eng.m;
    M3 M;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) M[i][j] = m.s16(e + 0x340 + 6 * i + 2 * j);
    V3 t;
    for (int k = 0; k < 3; k++) {
        t[k] = S16(m.s32(e + 0x20C + 4 * k) >> 16);
        m.w32(e + 0x354 + 4 * k, t[k]);
    }
    U inst = U(m.u32(e + 0x100));
    I n = m.u16(e + 0x36);
    bool have_joints = false, have_quats = false;
    std::map<I, V3> js;
    std::vector<Quat> quats;
    if (inst) {
        I nb = m.u16(inst + 0x72);
        U jp = U(m.u32(inst + 0x94));
        if (jp) {
            have_joints = true;
            for (I i = 0; i < nb; i++) js[i] = {m.s16(jp + 8 * i), m.s16(jp + 8 * i + 2), m.s16(jp + 8 * i + 4)};
        }
        U qp = U(m.u32(inst + 0x88));
        if (qp) have_quats = true, quats = slot_quats(m, qp, nb);
    }
    U pp = U(m.u32(e + 0x304));
    I eye = m.s32(m.u32(m.u32(0x8009D638) + 0x388) + 0x94);
    for (I i = 0; i < n; i++) {
        U p = U(pp + 0x48 * i);
        I a = m.u16(p + 0x1C), b = m.u16(p + 0x1E);
        V3 off = {m.s16(p + 0x40), m.s16(p + 0x42), m.s16(p + 0x44)};
        I offw = m.u32(p + 0x40), offz = m.s16(p + 0x44);
        V3 c;
        if (a != 0xFFFE && a != 0xFFFF && !have_joints)
            c = {0, 0, 0};
        else if (a == 0xFFFE || a == 0xFFFF) {
            c = off;
            if (a == 0xFFFE) c[1] = S16(c[1] + eye);
        } else {
            V3 rot = {0, 0, 0};
            if ((offw || offz) && have_quats && a < I(quats.size())) {
                V3 r = mvmva12(quat_m3(quats[size_t(a)][0], quats[size_t(a)][1], quats[size_t(a)][2], quats[size_t(a)][3]), off);
                rot = {S16(r[0]), S16(r[1]), S16(r[2])};
            }
            V3 ja = js.count(a) ? js[a] : V3{0, 0, 0}, jb = js.count(b) ? js[b] : V3{0, 0, 0};
            for (int k = 0; k < 3; k++) c[k] = S16(rot[k] + ((ja[k] + jb[k]) >> 1));
        }
        V3 w = to_world(M, t, c);
        for (int k = 0; k < 3; k++) m.w32(p + 0x20 + 4 * k, w[k]);
    }
    I bone = m.s16(e + 0x2EA);
    V3 w;
    if (bone >= 0) {
        V3 j = js.count(bone) ? js[bone] : V3{0, 0, 0};
        w = to_world(M, t, j);
    } else {
        M3 MM = M;
        if (m.u32(e + 0xC) & 1) {
            auto r = rot_yxz9(0, m.s16(e + 0x22E), m.s16(e + 0x232));
            I sc[3] = {m.s32(e + 0x270) >> 4, m.s32(e + 0x274) >> 4, m.s32(e + 0x278) >> 4};
            for (int i = 0; i < 3; i++)
                for (int k = 0; k < 3; k++) MM[i][k] = S16((r[3 * i + k] * sc[k]) >> 12);
        }
        w = to_world(MM, t, {m.s16(e + 0x2D8), m.s16(e + 0x2DA), m.s16(e + 0x2DC)});
    }
    for (int k = 0; k < 3; k++) m.w16(e + 0x2E0 + 2 * k, w[k]);
}

// The camera's view matrix and translation (camera_setup: the camera object's +0x100).
static void view_of(Mem& m, U co, M3& M, V3& t) {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) M[i][j] = m.s16(co + 0x100 + 6 * i + 2 * j);
    for (int k = 0; k < 3; k++) t[k] = m.s32(co + 0x114 + 4 * k);
}

// entity_in_view (0x8008E9DC) -> box_in_view (0x8008E5B0): the cube around the collision sphere
// against the view (animation.md). The camera's view matrix (rows x2, x1.25, x2) and translation
// as camera_setup left them; for each of the six edges the cube's corner farthest along that
// edge's inward direction is projected (h 400, centre (256, 120), the recording's projection:
// Gte::set_projection) and tested: behind the eye, x < 0, x > the right edge (540), y < 0,
// y > 240, depth > the far distance. The directions are the edges' planes from the same view
// (only their signs per axis matter: which corner); the original keeps them in the camera's
// view block (frustum_build 0x8008D0CC).
// 0x8008CCC8: a vector (16.16) normalised: shifted down to 14 bits, VectorNormal, x16.
static V3 norm16(const Gte& g, V3 v) {
    U bit = 0x40000000;
    int a1 = 0x1E;
    I ax = iabs(S32(v[0]));
    while (!(ax & bit) && !(iabs(S32(v[1])) & bit) && !(iabs(S32(v[2])) & bit)) {
        a1--;
        bit >>= 1;
        if (a1 < 0xE) break;
    }
    int sh = a1 - 0xD;
    V3 t = v;
    if (sh > 0)
        for (I& x : t) x = S32(x) >> sh;
    V3 n = vnormal(g, t);
    return {S32(n[0] * 16), S32(n[1] * 16), S32(n[2] * 16)};
}

static V3 cross8(const V3& a, const V3& b) {
    I ax = S32(a[0]) >> 8, ay = S32(a[1]) >> 8, az = S32(a[2]) >> 8, bx = S32(b[0]) >> 8, by = S32(b[1]) >> 8, bz = S32(b[2]) >> 8;
    return {S32(ay * bz - az * by), S32(az * bx - ax * bz), S32(ax * by - ay * bx)};
}

// frustum_build (0x8008D0CC): the six directions box_in_view picks its corners with (the
// camera's view block +0x50, +0x6C, +0x88, +0xA4, +0xC0, +0xDC): forward, the four side planes'
// normals through the eye and the corners of the view rectangle at the projection distance,
// backward. widen: the recording's (Valor's runtime, hook_view_frustum without its mouse reach:
// the rectangle 1.1 / the projection's scale wider and taller).
struct ViewDirs {
    V3 d[6];
};
static ViewDirs view_dirs(Engine& eng, U cam, U co, bool widen) {
    Mem& m = eng.m;
    const Gte& g = eng.gte;
    U blk = U(m.u32(co + 0xC8));
    V3 eye = (m.u32(co + 0x9C) & 0x1800) ? V3{m.s32(co + 0x3C), m.s32(co + 0x40), m.s32(co + 0x44)}
                                          : V3{m.s32(cam + 0x20C), m.s32(cam + 0x210), m.s32(cam + 0x214)};
    V3 look = {m.s32(co), m.s32(co + 4), m.s32(co + 8)};
    V3 side = {m.s32(co + 0x6C), m.s32(co + 0x70), m.s32(co + 0x74)};
    I w = m.s32(blk + 0x30), h = m.s32(blk + 0x34);
    if (widen) {
        w = I(double(w) * 1.1 / g.proj_kx);
        h = I(double(h) * 1.1 / g.proj_ky);
    }
    U tgt = U(m.u32(G_HANDLES + 4 * (m.u32(co + 0xC) & 0xFFFF)));
    I dist = tgt ? S32(m.s32(m.u32(tgt + 0x388) + 0xA4) << 16) : 0x1900000;
    V3 f = norm16(g, {S32(look[0] - eye[0]), S32(look[1] - eye[1]), S32(look[2] - eye[2])});
    V3 sv = norm16(g, {S32(side[0] - eye[0]), S32(side[1] - eye[1]), S32(side[2] - eye[2])});
    V3 u = norm16(g, cross8(f, sv));
    V3 r = norm16(g, cross8(u, f));
    V3 nu = {S32(-u[0]), S32(-u[1]), S32(-u[2])};
    V3 R, Uv, c;
    for (int k = 0; k < 3; k++) {
        R[k] = S32((S32(r[k]) >> 8) * (w >> 9));
        Uv[k] = S32((S32(nu[k]) >> 8) * (h >> 9));
        c[k] = S32(eye[k] + (dist >> 8) * (S32(f[k]) >> 8));
    }
    V3 c70, c80, c90, cA0;
    for (int k = 0; k < 3; k++) {
        c70[k] = S32(c[k] + Uv[k] - R[k]);
        c80[k] = S32(c[k] + Uv[k] + R[k]);
        c90[k] = S32(c[k] - Uv[k] - R[k]);
        cA0[k] = S32(c[k] - Uv[k] + R[k]);
    }
    // 0x8008CE28: the plane through the eye: normalise(eye - p1) x normalise(p3 - eye)
    auto plane = [&](const V3& p1, const V3& p3) {
        V3 a = norm16(g, {S32(eye[0] - p1[0]), S32(eye[1] - p1[1]), S32(eye[2] - p1[2])});
        V3 b = norm16(g, {S32(p3[0] - eye[0]), S32(p3[1] - eye[1]), S32(p3[2] - eye[2])});
        return norm16(g, cross8(a, b));
    };
    ViewDirs out;
    out.d[0] = f;
    out.d[1] = plane(c70, c90);
    out.d[2] = plane(cA0, c80);
    out.d[3] = plane(c80, c70);
    out.d[4] = plane(c90, cA0);
    out.d[5] = {S32(-f[0]), S32(-f[1]), S32(-f[2])};
    return out;
}

// entity_in_view (0x8008E9DC) -> box_in_view (0x8008E5B0): the cube around the collision sphere
// against the view (animation.md). For each of the six directions the cube's corner farthest
// along it is projected through the camera's view (camera_setup's matrix, h 400, centre (256,
// 120), the recording's projection) and tested in turn: behind the eye (the depth's flag), x < 0,
// x > the right edge (540), y < 0, y > 240, depth > the far distance.
static bool box_in_view(Engine& eng, const ViewDirs& vd, U co, const I mn[3], const I mx[3]) {
    Mem& m = eng.m;
    M3 M;
    V3 t;
    view_of(m, co, M, t);
    I right_edge = m.s32(0x8009C828) ? m.s32(0x8009C828) : 540;
    I bottom = m.s32(0x8009C82C) ? m.s32(0x8009C82C) : 240;
    I far = m.s32(co + 0xC4) > 0xFFFF ? m.s32(co + 0xC4) >> 16 : m.s32(co + 0xC4);
    for (int edge = 0; edge < 6; edge++) {
        V3 v;
        for (int k = 0; k < 3; k++) v[k] = vd.d[edge][k] >= 0 ? mx[k] : mn[k];
        I acc = t[2] * 0x1000;
        for (int k = 0; k < 3; k++) acc += M[2][k] * v[k];
        I z = S32(acc >> 12);
        I sx, sy;
        rtps(eng.gte, M, t, 400, 256 << 16, 120 << 16, v, sx, sy);
        bool out = false;
        switch (edge) {
            case 0: out = z < 0 || z > 0xFFFF; break;
            case 1: out = sx < 0; break;
            case 2: out = sx > right_edge; break;
            case 3: out = sy < 0; break;
            case 4: out = sy > bottom; break;
            default: out = (z < 0 ? 0 : z > 0xFFFF ? 0xFFFF : z) > far; break;
        }
        if (out) return false;
    }
    return true;
}

// The test with the directions the camera's view block holds (a RAM dump's: the original's own,
// Valor's runtime widening included).
bool entity_in_view_mem(Engine& eng, U e) {
    Mem& m = eng.m;
    U co = U(m.u32(m.u32(G_PLAYER) + 0x388));
    U blk = U(m.u32(co + 0xC8));
    ViewDirs vd;
    static const U offs[6] = {0x50, 0x6C, 0x88, 0xA4, 0xC0, 0xDC};
    for (int i = 0; i < 6; i++)
        for (int k = 0; k < 3; k++) vd.d[i][k] = m.s32(blk + offs[i] + 4 * k);
    I r = m.u16(e + 0x2E8);
    I mn[3], mx[3];
    for (int k = 0; k < 3; k++) {
        I c = m.u16(e + 0x2E0 + 2 * k);
        mn[k] = S16(c - r), mx[k] = S16(c + r);
    }
    return box_in_view(eng, vd, co, mn, mx);
}

bool entity_in_view(Engine& eng, U e) {
    Mem& m = eng.m;
    U cam = U(m.u32(G_PLAYER));
    U co = U(m.u32(cam + 0x388));
    static ViewDirs vd;
    static I vd_tick = -1, vd_frame = -1;
    if (vd_tick != eng.tick_no || vd_frame != m.s32(G_FRAME_COUNT)) {
        vd = view_dirs(eng, cam, co, true);
        vd_tick = eng.tick_no, vd_frame = m.s32(G_FRAME_COUNT);
    }
    I r = m.u16(e + 0x2E8);
    I mn[3], mx[3];
    for (int k = 0; k < 3; k++) {
        I c = m.u16(e + 0x2E0 + 2 * k);
        mn[k] = S16(c - r), mx[k] = S16(c + r);
    }
    return box_in_view(eng, vd, co, mn, mx);
}

void draw_pass(Engine& eng) {
    Mem& m = eng.m;
    m.w32(G_FRAME_COUNT, m.u32(G_FRAME_COUNT) + 1);
    const std::map<U, std::array<I, 3>>* seen = nullptr;
    std::map<U, std::array<I, 3>> live_seen;
    if (eng.live_view) {
        // live: the host's view decides; a drawn entity's counter is back at its start value
        for (U e : eng.entities())
            if (eng.live_view(e)) live_seen[U(m.u32(e))] = {1, 1, m.s32(e + 0x294)};
        seen = &live_seen;
    }
    if (!eng.live_view || std::getenv("VALOR_VIEW_CHECK")) {
        auto& log = drawn_log(eng);
        auto lv = log.by_level.find(eng.level);
        if (lv != log.by_level.end()) {
            auto t = lv->second.find(eng.tick_no + 1);
            if (t != lv->second.end() && !eng.live_view) seen = &t->second;
            // the view test against the recording's drawn flags (entities whose draw tests the
            // view). They disagree often: the recordings drew through Valor's runtime, whose view
            // test ran on its own presentation camera (README, The view test)
            if (t != lv->second.end())
                for (U e : eng.entities()) {
                    if (!(m.u32(e + 0xC) & 0x03000000) || (m.u32(e + 0xC) & 0x80000)) continue;
                    auto d = t->second.find(U(m.u32(e)));
                    // in view: the out-of-sight counter back at its start value
                    bool drawn = d != t->second.end() && d->second[2] == m.s32(e + 0x294);
                    bool iv = entity_in_view(eng, e);
                    eng.stats[iv == drawn ? "view test: agrees with the recording"
                                          : (iv ? "view test: in view, not drawn in the recording"
                                                : "view test: out of view, drawn in the recording")]++;
                }
        }
    }
    static const std::map<U, std::array<I, 3>> empty;
    if (!seen) {
        eng.note("drawn flags: no record of the next tick");
        seen = &empty;
    }
    for (U e : eng.entities()) {
        if (m.u32(e + 0xC) & 0x80000) continue;
        U h = U(m.u32(e));
        auto it = seen->find(h);
        const std::array<I, 3>* d = it == seen->end() ? nullptr : &it->second;
        bool drawn_now = d && (*d)[1];
        I fa = m.u32(e + 0xC);
        U inst = U(m.u32(e + 0x100));
        if (inst && m.u32(inst + 0x74)) {
            if (!((fa & 0x02000000) && !drawn_now && m.u32(e + 0x298))) pose_update(eng, e, inst, drawn_now);
        }
        if (m.s32(e + 8) == 0x1402 && m.s32(e + 0x4C + 4 * 0xD) != -1) {
            U obj = U(m.u32(e + 0x388));
            for (int k = 0; k < 3; k++) {
                U B = obj + 0x1C + 0x144 * k;
                if ((m.u32(B) & 0x27) == 1) attachment_draw(eng, e, B, m.s32(B + 8));
            }
        }
        if (m.u32(e + 0x370) == 0x8005E760) {
            // draw_in_view_counter: drawn once always; in view (the recording's counter back at
            // its start value: this handler never sets +0x29C) resets it, else cull_countdown
            m.w32(e + 0x298, 1);
            if (d && (*d)[2] == m.s32(e + 0x294))
                m.w32(e + 0x290, m.s32(e + 0x294));
            else if (!keep_near(eng, e)) {
                m.w32(e + 0x29C, 0);
                m.w32(e + 0x290, m.s32(e + 0x290) - 1);
            }
        } else if (drawn_now) {
            m.w32(e + 0x298, 1);
            m.w32(e + 0x29C, 1);
            m.w32(e + 0x290, m.s32(e + 0x294));
        } else {
            m.w32(e + 0x29C, 0);
            // entity_draw_model: out of view with flag 0x01000000: cull_countdown (0x8006A570),
            // drawn before or not (the make-room case marks it drawn)
            if (fa & 0x01000000)
                if (!keep_near(eng, e)) {
                    m.w32(e + 0x29C, 0);
                    m.w32(e + 0x290, m.s32(e + 0x290) - 1);
                }
        }
        if (d && (*d)[2] != m.s32(e + 0x290) && (fa & 0x8000)) eng.note("out-of-sight counter differs from the original's");
        if ((fa & 0x8000) && m.s32(e + 0x290) <= 0 && m.u32(e + 0x298)) {
            auto en = eng.ents.find(e);
            if (en != eng.ents.end() && en->second->rec) eng.logic->run_message(nullptr, en->second->rec->pair, 2, "despawn");
            eng.remove(e, "out of sight");
        }
    }
}

}  // namespace valor::sim
