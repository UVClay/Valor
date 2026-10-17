// Frame interpolation: the game logic runs at a fixed 30 Hz (see docs/RECON.md, Frame timing).
// To show more frames than that, the last frame the game drew is drawn again with a camera
// between that tick's and the next one's.
//
// Each game tick has a projection phase (the GTE projects the world into ordering tables) and is
// drawn at the start of the following one. The next tick's projections are complete by the time
// a frame is displayed, so moving the camera from the displayed tick toward the next one adds
// no latency.
//
// Only the camera is interpolated: objects keep their 30 Hz positions. Every GTE projection is
// recorded with its camera-space position, keyed by the screen coordinate it produced. When the
// GPU draws, each vertex is matched to its projection by that coordinate. Vertices nobody
// projected (the 2D HUD) stay where they are, as do other projections (the weapon, H != world).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

#include "interp.h"
#include "runtime.h"

namespace psx {

namespace {

std::deque<std::shared_ptr<InterpTick>> ticks;  // oldest first; the last one is being built
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

bool interp_enabled() { return config.interpolate; }

bool InterpTick::camera(InterpCamera &out) const {
    const MatrixUse *best = nullptr;
    for (const auto &m : matrices)
        if (!best || m.count > best->count) best = &m;
    if (!best) return false;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) out.m[i][j] = best->rt[i * 3 + j] / 4096.0;
        out.t[i] = best->tr[i];
    }
    return true;
}

void interp_note_draw() { drawn_since_projection = true; }

void interp_record(int16_t sx, int16_t sy, const int64_t acc[3], uint16_t h, double kx, double ky,
                   const int16_t rt[3][3], const int32_t tr[3]) {
    if (drawn_since_projection) {  // a new tick's projection phase
        drawn_since_projection = false;
        auto t = std::make_shared<InterpTick>();
        t->seq = next_seq++;
        t->vertices.reserve(4096);
        ticks.push_back(t);
        while (ticks.size() > 4) ticks.pop_front();
    }
    InterpTick &t = *ticks.back();
    bool translated = tr[0] || tr[1] || tr[2];
    InterpVertex v{float(double(acc[0]) / 4096), float(double(acc[1]) / 4096), float(double(acc[2]) / 4096),
                   float(kx), float(ky), float(h), translated, false};
    // The GTE clamps the divide for z < h/2 and the result to -0x400..0x3FF: such points can't
    // be moved from their screen position.
    v.exact = v.z >= h * 0.5f && sx > -0x400 && sx < 0x3FF && sy > -0x400 && sy < 0x3FF;
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
    if (h != config.world_h || !translated) return;
    for (auto &m : t.matrices)
        if (!memcmp(m.rt, rt, sizeof m.rt) && !memcmp(m.tr, tr, sizeof m.tr)) {
            m.count++;
            return;
        }
    InterpTick::MatrixUse m{};
    memcpy(m.rt, rt, sizeof m.rt);
    memcpy(m.tr, tr, sizeof m.tr);
    m.count = 1;
    t.matrices.push_back(m);
}

std::shared_ptr<const InterpTick> interp_drawing_tick() { return ticks.empty() ? nullptr : ticks.back(); }

std::shared_ptr<const InterpTick> interp_tick(uint64_t seq) {
    for (const auto &t : ticks)
        if (t->seq == seq) return t;
    return nullptr;
}

bool interp_transform(const InterpTick &from, const InterpTick &to, double alpha, InterpTransform &out) {
    InterpCamera c0, c1;
    if (!from.camera(c0) || !to.camera(c1)) return false;
    double s0[3], s1[3];
    Mat r0, r1, r0t, q, qa, ra, ma, m0inv, m1inv;
    split(c0.m, s0, r0);
    split(c1.m, s1, r1);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r0t[i][j] = r0[j][i];
    mul(r1, r0t, q);  // rotation from tick 0's camera to tick 1's
    if (!partial_rotation(q, alpha, qa)) return false;
    mul(qa, r0, ra);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) ma[i][j] = (s0[i] + (s1[i] - s0[i]) * alpha) * ra[i][j];
    if (!invert(c0.m, m0inv) || !invert(c1.m, m1inv)) return false;
    // Camera positions: C = -M^-1 T.
    double p0[3], p1[3], pa[3], ta[3];
    apply(m0inv, c0.t, p0);
    apply(m1inv, c1.t, p1);
    double moved = 0;
    for (int i = 0; i < 3; i++) {
        p0[i] = -p0[i];
        p1[i] = -p1[i];
        pa[i] = p0[i] + (p1[i] - p0[i]) * alpha;
        moved += (p1[i] - p0[i]) * (p1[i] - p0[i]);
    }
    if (moved > 1500.0 * 1500.0) return false;  // teleport or a different transform won
    apply(ma, pa, ta);
    for (int i = 0; i < 3; i++) ta[i] = -ta[i];
    // p' = Ma * M0^-1 * (p - T0) + Ta
    mul(ma, m0inv, out.a);
    double at0[3];
    apply(out.a, c0.t, at0);
    for (int i = 0; i < 3; i++) out.b[i] = ta[i] - at0[i];
    return true;
}

}  // namespace psx
