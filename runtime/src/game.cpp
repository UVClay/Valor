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

int game_crosshair_line() {
    if (!game_in_level()) return 120;
    uint32_t p = r32(PLAYER_STATE);
    if (!in_ram(p)) return 120;
    // The crosshair's screen line, inv+0x192 (inv = soldier+0x3CC): the aim ray goes through it.
    int y = int16_t(r32(p + 0x3CC + 0x190) >> 16);
    return y > 0 && y < 240 ? y : 120;
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

// Drawn beyond the view this tick (degrees, sideways and up or down): the frustum and the world's
// polygon test reach that much farther while mouse look turns the view ahead of the game
// (gpu_gl.cpp). `drawn_*` is what in-between frames can show of it: less where the frustum or the
// screen margin can't reach that far.
static double reach_yaw, reach_pitch;
static double frustum_yaw, frustum_pitch, drawn_yaw, drawn_pitch;
// A frustum much wider than this loses the world at some angles (the room in front of the camera
// is culled), so the reach stops there (degrees from the view's center).
constexpr double FRUSTUM_LIMIT_H = 75, FRUSTUM_LIMIT_V = 70;

// 0x8008D0CC(camera): the view frustum's side planes, through the camera and the corners of a
// rectangle `camera+0x3C` (the projection distance, 400) ahead: `camera+0x30` / 2 to the sides
// and `camera+0x34` / 2 up and down (all 16.16; 512 and 384 for the 512x240 screen with some
// margin vertically). Rooms and objects outside are culled, so with a wider view they'd vanish
// near the edges: widen the rectangle to the projection while the planes are built. A little more
// covers in-between frames, which turn the camera toward the next tick's view, and the reach
// covers mouse look.
extern "C" void hook_view_frustum(Cpu *c, RecompFn body) {
    uint32_t cam = c->r[4];
    double kx, ky, tan_h, tan_v;
    gte_world_scale(kx, ky);
    gte_world_view(tan_h, tan_v);
    reach_yaw = reach_pitch = 0;
    // (Decoupled rendering draws every frame for its own camera: nothing to reach for.)
    if (interp_presenting() && !config.decoupled && tan_h > 0) {
        kbm_look_reach(reach_yaw, reach_pitch);
        // Some always, for turning with a stick.
        reach_yaw = std::clamp(reach_yaw, 6.0, 30.0);
        reach_pitch = std::clamp(reach_pitch, 4.0, 20.0);
    }
    frustum_yaw = frustum_pitch = drawn_yaw = drawn_pitch = 0;
    if (!in_ram(cam) || (kx == 1 && ky == 1 && reach_yaw == 0)) return body(c);
    constexpr double MARGIN = 1.1;
    uint32_t w = r32(cam + 0x30), h = r32(cam + 0x34);
    int32_t dist = int32_t(r32(cam + 0x3C));
    double sw = MARGIN / kx, sh = MARGIN / ky;
    // The rectangle's edge out to `reach` degrees past the view's (`view`: its half-FOV tangent),
    // within `limit`; how far past the view it then is.
    auto widen = [&](uint32_t extent, double &scale, double view, double reach, double limit) {
        if (dist <= 0 || int32_t(extent) <= 0 || view <= 0) return 0.0;
        double t = double(int32_t(extent)) * scale / (2.0 * dist);  // tan of the half angle
        double edge = std::max(std::atan(t), std::min(std::atan(view) + reach * M_PI / 180, limit * M_PI / 180));
        scale *= std::tan(edge) / t;
        return std::max((edge - std::atan(view)) * 180 / M_PI, 0.0);
    };
    drawn_yaw = frustum_yaw = std::min(reach_yaw, widen(w, sw, tan_h, reach_yaw, FRUSTUM_LIMIT_H));
    drawn_pitch = frustum_pitch = std::min(reach_pitch, widen(h, sh, tan_v, reach_pitch, FRUSTUM_LIMIT_V));
    psx_w32(c, cam + 0x30, uint32_t(int32_t(std::min(double(int32_t(w)) * sw, 2.0e9))));
    psx_w32(c, cam + 0x34, uint32_t(int32_t(std::min(double(int32_t(h)) * sh, 2.0e9))));
    body(c);
    psx_w32(c, cam + 0x30, w);
    psx_w32(c, cam + 0x34, h);
}

// For hook_world_polygon: a polygon in front of the camera that the test skipped. Off the screen
// sideways or up or down within the reach, it is drawn anyway: in-between frames turn the view
// toward it. Otherwise as the test decided: facing away, too near or too far, or bad projection.
static void offscreen_polygon(Cpu *c, const uint32_t sz[3]) {
    if (frustum_yaw == 0 && frustum_pitch == 0) return;
    uint32_t flag = gte_mfc2(c, 31);
    if ((flag & 0x80000000) && (flag & 0x7FF98000)) return;
    // A vertex too far off screen for the GTE (clamped): where it was drawn isn't where it is.
    if (flag & 0x6000) return;
    if (int32_t(gte_mfc2(c, 24)) <= 0) return;  // NCLIP: facing away
    uint32_t hi = std::max({sz[0], sz[1], sz[2]}), lo = std::min({sz[0], sz[1], sz[2]});
    uint32_t otz = (hi * 3 + lo) >> 4;
    if ((lo < 10 && otz < 16) || (otz >> 2) >= 0x200) return;
    // The screen's bounds for what was drawn: where a point drawn_* degrees past the view's edge
    // projects, within where the GTE can put one (it clamps to -1024..1023).
    int32_t width = int16_t(psx_r16(c, c->r[19] + 0x3CC)), height = int16_t(psx_r16(c, c->r[19] + 0x3CE));
    double ofx = double(int32_t(gte_cfc2(c, 24))) / 65536, ofy = double(int32_t(gte_cfc2(c, 25))) / 65536;
    double tan_h, tan_v;
    gte_world_view(tan_h, tan_v);
    if (width <= 0 || height <= 0 || ofx <= 0 || ofy <= 0 || tan_h <= 0) return;
    static struct { int32_t width, height; double ofx, ofy, th, tv, yaw, pitch, x0, x1, y0, y1, drawn_yaw, drawn_pitch; } m;
    if (m.width != width || m.height != height || m.ofx != ofx || m.ofy != ofy || m.th != tan_h || m.tv != tan_v ||
        m.yaw != frustum_yaw || m.pitch != frustum_pitch) {
        // The screen's half extent `half` around `center` shows a half-FOV tangent `view`.
        auto bounds = [](double center, double half, double view, double reach, double size, double &lo, double &hi) {
            double ratio = std::tan(std::atan(view) + reach * M_PI / 180) / view;
            ratio = std::min({ratio, (center + 1024) / half, (1023 - center) / half});
            lo = std::min(0.0, center - half * ratio);
            hi = std::max(size, center + half * ratio);
            return (std::atan(view * std::max(ratio, 1.0)) - std::atan(view)) * 180 / M_PI;
        };
        m = {width, height, ofx, ofy, tan_h, tan_v, frustum_yaw, frustum_pitch, 0, 0, 0, 0, 0, 0};
        m.drawn_yaw = bounds(ofx, ofx, tan_h, frustum_yaw, width, m.x0, m.x1);
        m.drawn_pitch = bounds(ofy, ofy, tan_v, frustum_pitch, height, m.y0, m.y1);
    }
    drawn_yaw = m.drawn_yaw;
    drawn_pitch = m.drawn_pitch;
    // The test's outcodes (left, right, up, down; skipped when all vertices share one), with the
    // bounds. A vertex inside draws it.
    uint32_t all = 0xF;
    for (int k = 0; k < 3; k++) {
        uint32_t xy = gte_mfc2(c, 12 + k);
        double x = int16_t(xy), y = int16_t(xy >> 16);
        all &= (x < m.x0 ? 1 : x >= m.x1 ? 2 : 0) | (y < m.y0 ? 4 : y >= m.y1 ? 8 : 0);
    }
    if (all) return;
    // What the test stores for drawing it is already there; subdivided as it would be.
    uint32_t limit = r32(c->r[19] + 0x3A4) << 2, mask = 0;
    if (limit)
        for (int k = 0; k < 3; k++)
            if (sz[k] < limit) mask |= 1u << k;
    c->r[2] = mask ? mask << 1 : 1;
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
    // Closer than H/2 the GTE's divide saturates: the screen positions are wrong, so the test's
    // backface and nearness checks are too. Such polygons are checked in camera space below.
    uint32_t near = (gte_cfc2(c, 26) & 0xFFFF) / 2;
    if (sz[0] >= near && sz[1] >= near && sz[2] >= near) return offscreen_polygon(c, sz);
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
    // Its place in the ordering table (512 entries, otz / 4): a polygon from the camera to far
    // away would land past the table's end, and the game would write over whatever follows it.
    uint32_t otz = std::min((hi * 3 + lo) >> 4, 0x7FFu);
    psx_w16(c, c->r[19] + 0x2A, sz[2]);
    psx_w32(c, c->r[19] + 0x34, gte_mfc2(c, 14));
    psx_w32(c, c->r[19] + 0x3A8, otz);
    // Drawn whole, not subdivided as the test does near polygons: the subdivision can't place the
    // pieces behind the camera (the floor under the player went missing), and floods the
    // primitive buffer with them (the weapon, drawn last, went missing too).
    c->r[2] = 1;
    uint32_t sxy[3] = {gte_mfc2(c, 12), gte_mfc2(c, 13), gte_mfc2(c, 14)};
    interp_note_near_polygon(sxy);
}

// 0x8008019C: draws the HUD (messages, ammo, compass) with packets from the primitive pool, whose
// next free byte is [0x80098958]: those packets are the HUD whatever their coordinates.
extern "C" void hook_draw_hud(Cpu *c, RecompFn body) {
    uint32_t before = r32(0x80098958);
    body(c);
    hw::game_hud_packets[0] = before & 0x1FFFFC;
    hw::game_hud_packets[1] = r32(0x80098958) & 0x1FFFFC;
}

void psx::game_drawn_reach(double &yaw, double &pitch) {
    yaw = drawn_yaw;
    pitch = drawn_pitch;
}
