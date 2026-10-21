#include "valor/movement.h"

#include <algorithm>

#include "valor/fixed.h"
#include "valor/gte.h"

namespace valor {

namespace {

// 0x80078BCC: the signed distance (16.16) of the feet (up-facing plane) or of the point h above
// them (others), less off.
int32_t capsule_dist(const WallPlane& p, const int32_t pos[3], int32_t h, int32_t off) {
    int64_t t = s32(int64_t(p.n[0]) * (pos[0] >> 15) + int64_t(p.n[2]) * (pos[2] >> 15));
    int64_t yy = p.n[1] < 0 ? (pos[1] >> 15) : s32((pos[1] >> 15) - 2 * int64_t(h));
    return s32(t + p.n[1] * yy + (int64_t(p.d) << 16) - (int64_t(off) << 16));
}

// 0x80078D1C: of the planes the walker now penetrates, the one nearest at the old position.
int first_hit(const std::vector<WallPlane>& planes, const int32_t old[3], const int32_t pos[3], int32_t r,
              int32_t fp) {
    int32_t best = 0x7FFFFFFF;
    int idx = -1;
    for (size_t i = 0; i < planes.size(); i++) {
        if (capsule_dist(planes[i], pos, fp, r) < 0) {
            int32_t d0 = capsule_dist(planes[i], old, fp, 0);
            if (d0 < best) best = d0, idx = int(i);
        }
    }
    return idx;
}

// 0x80078C4C: out along the normal, horizontally, to off (radius + 4) in front.
void wall_push(const WallPlane& p, int32_t pos[3], int32_t fp, int32_t off) {
    int64_t v = capsule_dist(p, pos, fp, off) >> 8;
    pos[0] = s32(pos[0] - int64_t(p.n[0] >> 7) * v);
    pos[2] = s32(pos[2] - int64_t(p.n[2] >> 7) * v);
}

// walker_slide 0x80078F60. Returns the planes gathered at the end and the result (0 none, 1 still
// in a wall, 2 free).
struct Slide {
    int planes, result;
};

Slide walker_slide(const World& world, Walker& w, const int32_t delta[3], const int32_t old[3]) {
    int32_t r = w.radius, eye = w.eye, fp = eye - 16;
    int32_t* pos = w.pos;
    int32_t c[3] = {s16(pos[0] >> 16), s16((pos[1] >> 16) - fp), s16(pos[2] >> 16)};
    std::vector<WallPlane> planes;
    world.walls_gather(c, r, eye - 20, delta, planes);
    if (planes.empty()) return {0, 0};
    int left = int(planes.size());
    bool pushed = false;
    while (true) {
        int i = first_hit(planes, old, pos, r, fp);
        if (i < 0) break;
        pushed = true;
        left--;
        w.wall_pushes++;
        wall_push(planes[i], pos, fp, r + 4);
        if (left < 0) break;
    }
    if (!pushed) return {0, 2};
    int32_t delta2[3];
    for (int k = 0; k < 3; k++) delta2[k] = s16((pos[k] >> 16) - (old[k] >> 16));
    // The re-check is gathered at the feet, not at the centre: the segment runs from the feet down
    // to eye - 20 below them.
    int32_t c2[3] = {s16(pos[0] >> 16), s16(pos[1] >> 16), s16(pos[2] >> 16)};
    std::vector<WallPlane> planes2;
    world.walls_gather(c2, r, fp - 4, delta2, planes2);
    if (planes2.empty()) return {0, 2};
    if (first_hit(planes2, old, pos, r, fp) < 0) return {int(planes2.size()), 2};
    return {int(planes2.size()), 1};
}

}  // namespace

void entity_move(const World& world, Walker& w) {
    int32_t* pos = w.pos;
    int32_t on_ground0 = w.ground;
    w.events = 0;
    w.wall_pushes = 0;
    if (!w.ground && w.gravity) w.vy = std::min(w.vy + kGravity, kTerminalVy);
    int64_t vx = s16(uint32_t(w.strafe) >> 12);
    int64_t vy = s16(uint32_t(w.vy) >> 12);
    int64_t vz = s16(uint32_t(w.fwd) >> 12);
    // The local velocity turned by RotMatrixYXZ(0, yaw, roll), a GTE row each, >> 12.
    int16_t m[3][3];
    rot_yxz(0, w.yaw >> 16, w.roll >> 16, m);
    int64_t mx = (m[0][0] * vx + m[0][1] * vy + m[0][2] * vz) >> 12;
    int64_t my = (m[1][0] * vx + m[1][1] * vy + m[1][2] * vz) >> 12;
    int64_t mz = (m[2][0] * vx + m[2][1] * vy + m[2][2] * vz) >> 12;
    pos[0] = s32(pos[0] + (mx << 12) + w.push[0]);
    pos[1] = s32(pos[1] + (my << 12) + w.push[1]);
    pos[2] = s32(pos[2] + (mz << 12) + w.push[2]);
    int64_t dy = my >> 4;
    int32_t old[3] = {w.old[0], w.old[1], w.old[2]};
    int32_t delta[3];
    for (int k = 0; k < 3; k++) delta[k] = s16((pos[k] >> 16) - (old[k] >> 16));
    w.contact = 0;
    if (w.walls) {
        Slide s = walker_slide(world, w, delta, old);
        if (w.wall_pushes) w.events |= kEvWallPush;
        if (s.result == 1) {
            w.events |= kEvStillInWall;
            std::copy(old, old + 3, pos);
            w.ground = on_ground0;
            w.contact |= 4;
        } else if (s.result == 2 && s.planes) {
            w.contact |= 2;
        }
        if (s.planes > 0) w.contact |= 2;
    }
    if (!(w.flags_b & 0x10)) {
        Floor f = world.floor_find(pos);
        w.zone = f.zone;
        w.floor_found = f.height;
        if (f.height == kNoFloor) {
            w.events |= kEvNoFloor;
            if (!(w.flags_b & 0x10000)) {
                std::copy(old, old + 3, pos);
                if (!on_ground0) pos[1] = w.floor;
                w.ground = 1;
            }
        } else {
            w.floor = s32(int64_t(f.height) << 16);
            int32_t d = s16(pos[1] >> 16) - f.height;
            if (w.ground) {
                if (-32 <= d) {
                    if (d) w.events |= d < 0 ? kEvStepDown : kEvStepUp;
                    pos[1] = s32(int64_t(f.height) << 16);
                } else {
                    w.events |= kEvWalkedOff;
                    w.ground = 0;
                }
            } else if (w.gravity) {
                bool land = d >= 0 || iabs(d) <= (dy < 48 ? dy + 16 : 64);
                if (land && dy >= 0) {
                    w.events |= kEvLanding;
                    w.ground = 1;
                    pos[1] = s32(int64_t(f.height) << 16);
                    w.vy = 0;
                }
            }
        }
    }
    std::copy(pos, pos + 3, w.old);
}

}  // namespace valor
