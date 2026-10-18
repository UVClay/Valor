// Frame interpolation: the game logic runs at a fixed 30 Hz (see docs/RECON.md, Frame timing).
// To show more frames than that, the last frame the game drew is drawn again with a camera
// between that tick's and the next one's.
//
// Each game tick has a projection phase (the GTE projects the world into ordering tables) and is
// drawn at the start of the following one. The next tick's projections are complete by the time
// a frame is displayed, so moving the camera from the displayed tick toward the next one adds
// no latency.
//
// Every GTE projection is recorded with its camera-space position, its model-space input and the
// transform used, keyed by the screen coordinate it produced. When the GPU draws, each vertex is
// matched to its projection by that coordinate. Vertices nobody projected (the 2D HUD) stay where
// they are, as do other projections (the weapon, H != world).
//
// The camera is the transform most world vertices used; it moves everything. Moving objects
// (enemies are one transform per limb) are matched to the next tick's transform from the same
// code with the same mesh, nearest in the world, and their own transform is blended as well.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

#include "game.h"
#include "gpu.h"
#include "interp.h"
#include "runtime.h"

namespace psx {

namespace {

std::deque<std::shared_ptr<InterpTick>> ticks;  // oldest first; the last one is being built
bool look_applies = true;  // the camera turns by the mouse look the game takes (see pair_ticks)
uint64_t next_seq = 1;
bool drawn_since_projection = true;

using Mat = double[3][3];

void mul(const Mat a, const Mat b, Mat out) {
    Mat r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    memcpy(out, r, sizeof r);
}

bool invert(const Mat m, Mat out) {
    double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                 m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (std::abs(det) < 1e-12) return false;
    double k = 1.0 / det;
    out[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * k;
    out[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * k;
    out[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * k;
    out[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * k;
    out[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * k;
    out[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * k;
    out[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * k;
    out[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * k;
    out[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * k;
    return true;
}

void apply(const Mat m, const double v[3], double out[3]) {
    double r[3];
    for (int i = 0; i < 3; i++) r[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
    memcpy(out, r, sizeof r);
}

// Split M into per-row scale and rotation: M = diag(s) * R.
void split(const Mat m, double s[3], Mat r) {
    for (int i = 0; i < 3; i++) {
        s[i] = std::sqrt(m[i][0] * m[i][0] + m[i][1] * m[i][1] + m[i][2] * m[i][2]);
        for (int j = 0; j < 3; j++) r[i][j] = s[i] > 0 ? m[i][j] / s[i] : 0;
    }
}

// Rotation matrix -> axis and angle, then a fraction of that rotation as a matrix.
bool partial_rotation(const Mat q, double alpha, Mat out) {
    double c = std::clamp((q[0][0] + q[1][1] + q[2][2] - 1) * 0.5, -1.0, 1.0);
    double angle = std::acos(c);
    if (angle > M_PI * 0.5) return false;  // more than 90 degrees in one tick: a camera cut
    double ax = q[2][1] - q[1][2], ay = q[0][2] - q[2][0], az = q[1][0] - q[0][1];
    double n = std::sqrt(ax * ax + ay * ay + az * az);
    if (n < 1e-9 || angle < 1e-9) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) out[i][j] = i == j;
        return true;
    }
    ax /= n; ay /= n; az /= n;
    double t = angle * alpha, ct = std::cos(t), st = std::sin(t), v = 1 - ct;
    out[0][0] = ct + ax * ax * v;      out[0][1] = ax * ay * v - az * st; out[0][2] = ax * az * v + ay * st;
    out[1][0] = ay * ax * v + az * st; out[1][1] = ct + ay * ay * v;      out[1][2] = ay * az * v - ax * st;
    out[2][0] = az * ax * v - ay * st; out[2][1] = az * ay * v + ax * st; out[2][2] = ct + az * az * v;
    return true;
}

}  // namespace

// Only the OpenGL renderer can redraw frames. Projections are recorded whenever it draws: they
// also serve the widescreen HUD and near clipping.
bool interp_enabled() { return hw::hwgl::active(); }
bool interp_presenting() { return config.interpolate && hw::hwgl::active(); }

void interp_note_draw() { drawn_since_projection = true; }

void interp_record(int16_t sx, int16_t sy, const int64_t acc[3], uint16_t h, double kx, double ky, int32_t ofx,
                   int32_t ofy, const int16_t rt[3][3], const int32_t tr[3], const int16_t mv[3], uint32_t pc) {
    if (drawn_since_projection) {  // a new tick's projection phase
        drawn_since_projection = false;
        auto t = std::make_shared<InterpTick>();
        t->seq = next_seq++;
        t->frame = hw::gpu.frames;
        kbm_tick_look(t->look_yaw, t->look_pitch);
        t->vertices.reserve(4096);
        ticks.push_back(t);
        while (ticks.size() > 4) ticks.pop_front();
    }
    InterpTick &t = *ticks.back();

    // The transform group.
    int32_t gi = t.last_group;
    if (gi < 0 || memcmp(t.groups[gi].rt, rt, sizeof t.groups[gi].rt) || memcmp(t.groups[gi].tr, tr, sizeof t.groups[gi].tr) ||
        t.groups[gi].h != h || t.groups[gi].pc != pc) {
        uint64_t key = pc * 0x9E3779B97F4A7C15ull ^ h;
        for (int i = 0; i < 9; i++) key = (key ^ uint16_t(rt[i / 3][i % 3])) * 0x100000001B3ull;
        for (int i = 0; i < 3; i++) key = (key ^ uint32_t(tr[i])) * 0x100000001B3ull;
        auto [it, added] = t.group_index.try_emplace(key, uint32_t(t.groups.size()));
        if (added) {
            InterpGroup g{};
            for (int i = 0; i < 9; i++) g.rt[i] = rt[i / 3][i % 3];
            memcpy(g.tr, tr, sizeof g.tr);
            g.h = h;
            g.pc = pc;
            g.sig = 2166136261u;
            t.groups.push_back(g);
        }
        gi = int32_t(it->second);
        t.last_group = gi;
    }
    InterpGroup &g = t.groups[gi];
    g.count++;
    if (g.sig_n < 4) {
        for (int i = 0; i < 3; i++) g.sig = (g.sig ^ uint16_t(mv[i])) * 16777619u;
        g.sig_n++;
    }

    InterpVertex v{};
    v.x = float(double(acc[0]) / 4096);
    v.y = float(double(acc[1]) / 4096);
    v.z = float(double(acc[2]) / 4096);
    v.kx = float(kx);
    v.ky = float(ky);
    v.h = float(h);
    memcpy(v.v, mv, sizeof v.v);
    v.group = gi;
    v.ofx = float(ofx / 65536.0);
    v.ofy = float(ofy / 65536.0);
    v.translated = tr[0] || tr[1] || tr[2];
    auto [it, added] = t.by_sxy.try_emplace(interp_key(sx, sy), uint32_t(t.vertices.size()));
    int32_t previous = -1;
    if (!added) {
        // The same point projected again (shared vertices) replaces the old entry; a different
        // one is chained to it.
        const InterpVertex &o = t.vertices[it->second];
        bool same = std::abs(o.x - v.x) <= 1 && std::abs(o.y - v.y) <= 1 && std::abs(o.z - v.z) <= 1 && o.h == v.h &&
                    o.translated == v.translated;
        previous = same ? t.previous[it->second] : int32_t(it->second);
    }
    t.previous.push_back(previous);
    it->second = uint32_t(t.vertices.size());
    t.vertices.push_back(v);
}

std::shared_ptr<const InterpTick> interp_drawing_tick() { return ticks.empty() ? nullptr : ticks.back(); }

std::shared_ptr<const InterpTick> interp_tick(uint64_t seq) {
    for (const auto &t : ticks)
        if (t->seq == seq) return t;
    return nullptr;
}

namespace {

struct Transform {
    Mat m;
    double t[3];
};

Transform group_transform(const InterpGroup &g) {
    Transform x;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) x.m[i][j] = g.rt[i * 3 + j] / 4096.0;
        x.t[i] = g.tr[i];
    }
    return x;
}

bool world_group(const InterpGroup &g) {
    return g.h == config.world_h && (g.tr[0] || g.tr[1] || g.tr[2]);
}

// Where the camera is in this transform's space: -M^-1 T.
bool eye(const Transform &x, double out[3]) {
    Mat inv;
    if (!invert(x.m, inv)) return false;
    apply(inv, x.t, out);
    for (int i = 0; i < 3; i++) out[i] = -out[i];
    return true;
}

double dist2(const double a[3], const double b[3]) {
    double d = 0;
    for (int i = 0; i < 3; i++) d += (a[i] - b[i]) * (a[i] - b[i]);
    return d;
}

// Per pair of ticks: the camera transforms and the objects' transforms relative to the camera
// (object -> world), for groups found in both ticks.
struct Pairing {
    uint64_t from = 0, to = 0;
    bool ok = false;
    Transform cam0, cam1;
    struct Match { int32_t group; Mat w0, w1; double p0[3], p1[3]; };
    std::vector<Match> matches;
    std::vector<int32_t> group_match;  // per group of `from`: index into matches, or -1
};

void pair_ticks(const InterpTick &from, const InterpTick &to, Pairing &p) {
    p.from = from.seq;
    p.to = to.seq;
    p.ok = false;
    p.matches.clear();
    p.group_match.assign(from.groups.size(), -1);

    // The camera: the transform most world vertices used, and the next tick's transform from the
    // same code whose eye is nearest (the same mesh, viewed from the moved camera).
    int32_t best = -1;
    for (size_t i = 0; i < from.groups.size(); i++)
        if (world_group(from.groups[i]) && (best < 0 || from.groups[i].count > from.groups[best].count)) best = int32_t(i);
    if (best < 0) return;
    p.cam0 = group_transform(from.groups[best]);
    double e0[3];
    if (!eye(p.cam0, e0)) return;
    double nearest = 1500.0 * 1500.0;  // farther: a camera cut
    for (const auto &g : to.groups) {
        if (!world_group(g) || g.pc != from.groups[best].pc) continue;
        Transform x = group_transform(g);
        double e[3];
        if (eye(x, e) && dist2(e, e0) < nearest) {
            nearest = dist2(e, e0);
            p.cam1 = x;
            p.ok = true;
        }
    }
    if (!p.ok) return;
    // Does the game turn the camera by the mouse look it takes? Not in aim mode (it moves the
    // reticle) or cutscenes: showing the look early would then turn the view and snap it back.
    if (std::abs(from.look_yaw) > 0.3 || std::abs(from.look_pitch) > 0.3) {
        double s0[3], s1[3];
        Mat r0, r1;
        split(p.cam0.m, s0, r0);
        split(p.cam1.m, s1, r1);
        double yaw = std::remainder((std::atan2(r1[2][0], r1[2][2]) - std::atan2(r0[2][0], r0[2][2])) * 180 / M_PI, 360.0);
        double pitch = (std::asin(std::clamp(r0[2][1], -1.0, 1.0)) - std::asin(std::clamp(r1[2][1], -1.0, 1.0))) * 180 / M_PI;
        auto close = [](double got, double want) { return std::abs(got - want) < std::abs(want) * 0.35 + 0.2; };
        look_applies = close(yaw, from.look_yaw) && close(pitch, from.look_pitch);
    }

    // Objects: their transform relative to the camera, W = Mcam^-1 * M and w = Mcam^-1 * (T - Tcam)
    // (W is the object's rotation, w its position in the world).
    Mat inv0, inv1;
    if (!invert(p.cam0.m, inv0) || !invert(p.cam1.m, inv1)) {
        p.ok = false;
        return;
    }
    auto relative = [](const Mat inv, const Transform &cam, const Transform &x, Mat w, double pos[3]) {
        mul(inv, x.m, w);
        double d[3] = {x.t[0] - cam.t[0], x.t[1] - cam.t[1], x.t[2] - cam.t[2]};
        apply(inv, d, pos);
    };
    std::unordered_multimap<uint64_t, uint32_t> next;  // (pc, mesh) -> groups of `to`
    for (size_t i = 0; i < to.groups.size(); i++) {
        const auto &g = to.groups[i];
        if (world_group(g) && g.sig_n) next.emplace(uint64_t(g.pc) << 32 | g.sig, uint32_t(i));
    }
    for (size_t i = 0; i < from.groups.size(); i++) {
        const auto &g = from.groups[i];
        if (!world_group(g) || !g.sig_n || int32_t(i) == best) continue;
        Pairing::Match m;
        m.group = int32_t(i);
        relative(inv0, p.cam0, group_transform(g), m.w0, m.p0);
        auto range = next.equal_range(uint64_t(g.pc) << 32 | g.sig);
        double nearest_obj = 256.0 * 256.0;  // farther in one tick: not the same object
        bool found = false;
        for (auto it = range.first; it != range.second; ++it) {
            Mat w1;
            double p1[3];
            relative(inv1, p.cam1, group_transform(to.groups[it->second]), w1, p1);
            if (dist2(p1, m.p0) < nearest_obj) {
                nearest_obj = dist2(p1, m.p0);
                memcpy(m.w1, w1, sizeof w1);
                memcpy(m.p1, p1, sizeof m.p1);
                found = true;
            }
        }
        if (!found) continue;
        // A static object (same placement) needs nothing beyond the camera's motion.
        double change = dist2(m.p0, m.p1);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) change += (m.w1[r][c] - m.w0[r][c]) * (m.w1[r][c] - m.w0[r][c]) * 4096;
        if (change < 0.01) continue;
        p.group_match[i] = int32_t(p.matches.size());
        p.matches.push_back(m);
    }
}

}  // namespace

// Rotation by `angle` radians about the unit axis (x, y, z).
void axis_rotation(double x, double y, double z, double angle, Mat out) {
    double c = std::cos(angle), s = std::sin(angle), v = 1 - c;
    out[0][0] = c + x * x * v;     out[0][1] = x * y * v - z * s; out[0][2] = x * z * v + y * s;
    out[1][0] = y * x * v + z * s; out[1][1] = c + y * y * v;     out[1][2] = y * z * v - x * s;
    out[2][0] = z * x * v - y * s; out[2][1] = z * y * v + x * s; out[2][2] = c + z * z * v;
}

bool interp_look_applies() { return look_applies; }

bool interp_motion(const InterpTick &from, const InterpTick &to, double alpha, double yaw, double pitch,
                   InterpMotion &out) {
    static Pairing p;
    if (p.from != from.seq || p.to != to.seq) pair_ticks(from, to, p);
    if (!p.ok) return false;
    if (!look_applies) yaw = pitch = 0;

    // The camera between the two: rotation slerped (scale per axis lerped), position lerped.
    double s0[3], s1[3];
    Mat r0, r1, r0t, q, qa, ra, ma, m0inv, m1inv;
    split(p.cam0.m, s0, r0);
    split(p.cam1.m, s1, r1);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r0t[i][j] = r0[j][i];
    mul(r1, r0t, q);  // rotation from tick 0's camera to tick 1's
    if (!partial_rotation(q, alpha, qa)) return false;
    mul(qa, r0, ra);
    if (yaw != 0 || pitch != 0) {
        // Turning the camera turns the world the other way about the camera's position: yaw
        // about the world's vertical axis (as seen from the camera), pitch about the camera's
        // horizontal one, kept within the game's pitch limit (looking down: forward.y > 0).
        constexpr double LIMIT = 53.0;
        double down = std::asin(std::clamp(ra[2][1], -1.0, 1.0)) * 180 / M_PI;
        pitch = std::clamp(pitch, down - LIMIT, down + LIMIT);
        Mat ey, ep, e;
        double n = std::sqrt(ra[0][1] * ra[0][1] + ra[1][1] * ra[1][1] + ra[2][1] * ra[2][1]);
        axis_rotation(ra[0][1] / n, ra[1][1] / n, ra[2][1] / n, -yaw * M_PI / 180, ey);
        axis_rotation(1, 0, 0, -pitch * M_PI / 180, ep);
        mul(ep, ey, e);
        mul(e, ra, ra);
    }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) ma[i][j] = (s0[i] + (s1[i] - s0[i]) * alpha) * ra[i][j];
    if (!invert(p.cam0.m, m0inv) || !invert(p.cam1.m, m1inv)) return false;
    double e0[3], e1[3], ea[3], ta[3];
    eye(p.cam0, e0);
    eye(p.cam1, e1);
    for (int i = 0; i < 3; i++) ea[i] = e0[i] + (e1[i] - e0[i]) * alpha;
    apply(ma, ea, ta);
    for (int i = 0; i < 3; i++) ta[i] = -ta[i];  // Ta = -Ma * eye

    // Static things: p' = Ma * M0^-1 * (p - T0) + Ta.
    mul(ma, m0inv, out.a);
    double at0[3];
    apply(out.a, p.cam0.t, at0);
    for (int i = 0; i < 3; i++) out.b[i] = ta[i] - at0[i];

    // Moving objects: p' = Ma * (W * v + w) + Ta with W, w blended.
    out.objects.resize(p.matches.size());
    out.group_object = p.group_match;
    for (size_t k = 0; k < p.matches.size(); k++) {
        const auto &m = p.matches[k];
        Mat w;
        double pos[3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) w[i][j] = m.w0[i][j] + (m.w1[i][j] - m.w0[i][j]) * alpha;
            pos[i] = m.p0[i] + (m.p1[i] - m.p0[i]) * alpha;
        }
        auto &o = out.objects[k];
        mul(ma, w, o.m);
        apply(ma, pos, o.t);
        for (int i = 0; i < 3; i++) o.t[i] += ta[i];
    }
    return true;
}

}  // namespace psx
