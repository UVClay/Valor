// Medal of Honor state the runtime reads (pause, aim mode, controller configuration) and the
// native hooks that feed mouse and keyboard input into its controls. Addresses: docs/RECON.md.
#include "game.h"

#include <algorithm>
#include <cmath>

#include "gpu.h"
#include "hw.h"
#include "input.h"
#include "interp.h"

namespace psx {

namespace {

constexpr uint32_t OVERLAY_BASE = 0x80039D90;  // first word: the resident overlay's id
constexpr uint32_t LEVEL_ID = 5;
constexpr uint32_t PAUSED = 0x8009A5E0;        // set by the start-button handler (0x800625BC)
constexpr uint32_t PLAYER_STATE = 0x8009D654;  // -> struct; +0x10 bit 0x10: aim mode
constexpr uint32_t ACTION_BUTTONS = 0x800B7790;  // (port * 9 + action) * 4: button mask
constexpr uint32_t ROLE_AXES = 0x800B74B0;       // port * 160 + role * 40: analog role config
constexpr uint32_t STICKS = 0x80093B70;          // port * 4: rx, ry, lx, ly; +8 + port: analog mode

uint32_t r32(uint32_t addr) { return psx_r32(hw::cpu, addr); }
uint8_t r8(uint32_t addr) { return psx_r8(hw::cpu, addr); }

bool in_ram(uint32_t p) { return (p & 0xFFE00003) == 0x80000000; }

}  // namespace

bool game_in_level() { return r32(OVERLAY_BASE) == LEVEL_ID; }

bool game_paused() { return game_in_level() && r32(PAUSED) != 0; }

bool game_aiming() {
    if (!game_in_level()) return false;
    uint32_t p = r32(PLAYER_STATE);
    return in_ram(p) && (r32(p + 0x10) & 0x10);
}

uint16_t game_action_buttons(GameAction a) {
    if (!game_in_level()) return 0;
    // The game keeps button masks with the pad's two bytes swapped.
    uint32_t m = r32(ACTION_BUTTONS + uint32_t(a) * 4) & 0xFFFF;
    return uint16_t((m & 0xFF) << 8 | m >> 8);
}

}  // namespace psx

using namespace psx;

// 0x80075FB4(port, role, ?): the value of a control role (0 forward, 1 strafe, 2 pitch, 3 turn)
// from its stick axis, as 0x80075E7C shapes it: up to 255 * (speed >> 8), negative for forward,
// left, up and left. Look roles end up added to 16.16 angles (4096 per turn). Measured: turn
// applies the value as is, pitch half of it.
extern "C" void hook_role_axis(Cpu *c, RecompFn body) {
    uint32_t port = c->r[4], role = c->r[5];
    body(c);
    if (port != 0 || role > 3) return;
    double full = 255.0 * double(r32(ROLE_AXES + role * 40 + 0x20) >> 8);
    constexpr double UNITS_PER_DEGREE = 4096.0 * 65536.0 / 360.0;
    if (role >= 2 && config.stick_aim && r8(STICKS + 8 + port) != 0) {
        // Modern stick response, from the same stick axis (the stick copy holds rx, ry, lx, ly):
        // a small deadzone and a smooth curve up to a top speed, instead of the game's.
        uint32_t cfg = ROLE_AXES + role * 40;
        double d = (double(r8(STICKS + (r8(cfg) & 3))) - 128.0) / 127.0;
        d = std::clamp(d, -1.0, 1.0) * (r32(cfg + 0x24) ? -1 : 1);
        constexpr double DEADZONE = 0.12;
        double a = std::abs(d) < DEADZONE ? 0 : std::pow((std::abs(d) - DEADZONE) / (1 - DEADZONE), 1.6);
        double degrees = std::copysign(a, d) * (role == 3 ? config.stick_yaw_speed : config.stick_pitch_speed) / 30.0;
        c->r[2] = uint32_t(int32_t(degrees * UNITS_PER_DEGREE * (role == 2 ? 2 : 1)));
    }
    double add = 0;
    if (role <= 1) {
        double forward, strafe;
        kbm_movement(forward, strafe);
        add = role == 0 ? -forward * full : strafe * full;
    } else {
        double degrees = role == 3 ? kbm_take_turn() : -kbm_take_pitch();
        add = degrees * UNITS_PER_DEGREE * (role == 2 ? 2 : 1);
    }
    if (add == 0) return;
    double v = double(int32_t(c->r[2])) + add;
    if (role <= 1) v = std::clamp(v, -full, full);
    c->r[2] = uint32_t(int32_t(std::clamp(v, -2.0e9, 2.0e9)));
}

// 0x8008D0CC(camera): the view frustum's side planes, through the camera and the corners of a
// rectangle `camera+0x3C` (the projection distance, 400) ahead: `camera+0x30` / 2 to the sides
// and `camera+0x34` / 2 up and down (16.16; 512 and 384 for the 512x240 screen with some margin
// vertically). Rooms and objects outside are culled, so with a wider view they'd vanish near
// the edges: widen the rectangle to the projection while the planes are built. A little more
// covers in-between frames, which turn the camera toward the next tick's view.
extern "C" void hook_view_frustum(Cpu *c, RecompFn body) {
    uint32_t cam = c->r[4];
    double kx, ky;
    gte_world_scale(kx, ky);
    if ((kx == 1 && ky == 1) || !in_ram(cam)) return body(c);
    constexpr double MARGIN = 1.1;
    uint32_t w = r32(cam + 0x30), h = r32(cam + 0x34);
    double sw = MARGIN / kx, sh = MARGIN / ky;
    // Mouse look turns the view ahead of the game (gpu_gl.cpp): draw what that can show at the
    // sides too. (Not up and down: a taller rectangle loses nearby walls, and pitch turns slowly.)
    double reach = kbm_look_reach();
    int32_t dist = int32_t(r32(cam + 0x3C));
    if (reach > 0 && dist > 0 && int32_t(w) > 0) {
        double t = double(int32_t(w)) * sw / (2 * 65536.0 * dist);  // tan of the half angle
        double angle = std::min(std::atan(t) + reach * M_PI / 180, 70 * M_PI / 180);
        sw *= std::tan(angle) / t;
    }
    psx_w32(c, cam + 0x30, uint32_t(int32_t(std::min(double(int32_t(w)) * sw, 2.0e9))));
    psx_w32(c, cam + 0x34, uint32_t(int32_t(std::min(double(int32_t(h)) * sh, 2.0e9))));
    body(c);
    psx_w32(c, cam + 0x30, w);
    psx_w32(c, cam + 0x34, h);
}

// 0x80011214: whether to draw a world polygon, after its vertices are projected (RTPT). Registers:
// r17 the vertex table (x, y | z, 8 bytes each), r19 the scratchpad, r20..r22 the polygon's
// vertex indices (r23 already points at the next polygon, whose vertices it loads). Returns 0 to
// skip, 1 to draw, more to subdivide it first.
// It skips polygons reaching behind the camera (their projection is garbage, so the backface test
// fails). With the OpenGL renderer they're clipped at a near plane (gpu_gl.cpp, near_triangle):
// let the ones facing the camera through, storing what the test would have for drawing them.
extern "C" void hook_world_polygon(Cpu *c, RecompFn body) {
    body(c);
    if (c->r[2] != 0 || !interp_enabled()) return;
    uint32_t sz[3] = {gte_mfc2(c, 17), gte_mfc2(c, 18), gte_mfc2(c, 19)};
    if (sz[0] && sz[1] && sz[2]) return;  // in front: really skipped
    // Camera-space vertices from the GTE's rotation and translation.
    int16_t rt[9];
    for (int i = 0; i < 5; i++) {
        uint32_t w = gte_cfc2(c, i);
        rt[i * 2] = int16_t(w);
        if (i < 4) rt[i * 2 + 1] = int16_t(w >> 16);
    }
    int32_t tr[3] = {int32_t(gte_cfc2(c, 5)), int32_t(gte_cfc2(c, 6)), int32_t(gte_cfc2(c, 7))};
    uint32_t index[3] = {c->r[20] & 0xFFFF, c->r[21] & 0xFFFF, c->r[22] & 0xFFFF};
    double v[3][3];
    bool front_any = false;
    for (int k = 0; k < 3; k++) {
        uint32_t a = c->r[17] + index[k] * 8;
        uint32_t xy = r32(a);
        double m[3] = {double(int16_t(xy)), double(int16_t(xy >> 16)), double(int16_t(r32(a + 4)))};
        for (int r = 0; r < 3; r++)
            v[k][r] = (rt[r * 3] * m[0] + rt[r * 3 + 1] * m[1] + rt[r * 3 + 2] * m[2]) / 4096.0 + tr[r];
        front_any |= v[k][2] > 0;
    }
    if (!front_any) return;
    // Facing the camera: the eye (the origin) is on the front side of the plane, as NCLIP > 0
    // means for projected points.
    double det = v[0][0] * (v[1][1] * v[2][2] - v[1][2] * v[2][1]) - v[0][1] * (v[1][0] * v[2][2] - v[1][2] * v[2][0]) +
                 v[0][2] * (v[1][0] * v[2][1] - v[1][1] * v[2][0]);
    if (det <= 0) return;
    uint32_t hi = std::max({sz[0], sz[1], sz[2]}), lo = std::min({sz[0], sz[1], sz[2]});
    uint32_t otz = (hi * 3 + lo) >> 4;
    psx_w16(c, c->r[19] + 0x2A, sz[2]);
    psx_w32(c, c->r[19] + 0x34, gte_mfc2(c, 14));
    psx_w32(c, c->r[19] + 0x3A8, otz);
    // Subdivided like the test does for near polygons (a mask of the vertices closer than
    // [0x3A4] * 4, doubled), so edges shared with subdivided neighbours split at the same points
    // (a whole edge against split ones cracks open at high resolution).
    uint32_t limit = r32(c->r[19] + 0x3A4) << 2, mask = 0;
    if (limit) {
        psx_w16(c, c->r[19] + 0x42, otz);
        for (int k = 0; k < 3; k++)
            if (sz[k] < limit) mask |= 1u << k;
    }
    c->r[2] = mask ? mask << 1 : 1;
}
