// Decoupled rendering. The game draws a frame per 30 Hz tick, with the camera of that tick; frame
// interpolation redraws those frames for in-between moments, which can't show what the game
// didn't draw. Instead, every presented frame runs the game's own drawing pass again, on a copy of
// the machine taken when the tick's drawing began, with the camera placed where it should be at
// that moment. The copy's drawing goes to the OpenGL renderer only; the game itself never sees it.
//
// MoH (LEVEL.BIN): 0x800503D4 is the drawing pass. It clears this frame's ordering table
// (ClearOTagR via 0x80050550), fills it (0x8006C714: level, objects, weapon; 0x8008019C: HUD), and
// 0x800504F8 flips: 0x80090B80 waits for the vblank, toggles the buffer index [0x8009A620+0x2204]
// and puts the next drawing environment, then DrawOTag sends the table. In the copy, the flip
// puts that drawing environment and walks the table straight away; ClearOTagR is done natively
// (the copy must not touch DMA or interrupts).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "game.h"

#include "gpu.h"
#include "gpu_gl.h"
#include "hw.h"
#include "interp.h"
#include "runtime.h"
#include "shadow.h"

namespace psx {

namespace {

constexpr uint32_t PASS = 0x800503D4;
constexpr uint32_t FRAME_DATA = 0x8009A620;  // +0x2204: buffer index; +0x10: drawing environments
constexpr uint32_t OT_BASE = 0x8009A7C8;     // 512 entries per buffer, 0x1000 apart
// The camera (0x8004913C(player) builds it, before the tick's controls are applied): it looks
// from the player's position (player+0x20C, 16.16) at a point that orbits it (obj+0x0, obj =
// player+0x388), with obj+0x6C a point above. 0x8004913C makes the view matrix (obj+0xE4, copied
// for the drawing pass by 0x80050550) and the frustum from them.
constexpr uint32_t PLAYER = 0x8009D638;
constexpr uint32_t CAMERA_SETUP = 0x8004913C;

// A copy of the machine as the CPU's code sees it.
struct Machine {
    std::vector<uint8_t> ram, scratch, gte;
    Cpu cpu;
};

Machine snapshot, saved;
bool have_snapshot;
uint64_t snapshot_frame, previous_frame;
int32_t eye[3], previous_eye[3];  // the player's position in the latest copy and the one before
bool active;  // a shadow pass is running
int shadow_x, shadow_y;  // where it drew (the drawing environment's area)

void capture(Machine &m, const Cpu &c) {
    m.ram.assign(psx_ram, psx_ram + PSX_RAM_SIZE);
    m.scratch.assign(psx_scratch, psx_scratch + sizeof psx_scratch);
    gte_save(m.gte);
    m.cpu = c;
}

void load(const Machine &m, Cpu &c) {
    memcpy(psx_ram, m.ram.data(), PSX_RAM_SIZE);
    memcpy(psx_scratch, m.scratch.data(), sizeof psx_scratch);
    gte_restore(m.gte);
    c = m.cpu;
}

uint32_t r32(uint32_t a) { return psx_r32(hw::cpu, a); }
uint16_t r16(uint32_t a) { return uint16_t(psx_r16(hw::cpu, a)); }
uint8_t r8(uint32_t a) { return uint8_t(psx_r8(hw::cpu, a)); }

// PutDrawEnv(env) as GP0 commands (PsyQ's DRAWENV: clip RECT, ofs[2], tw RECT, tpage, dtd, dfe,
// isbg, r0, g0, b0).
void put_draw_env(uint32_t env) {
    int x = int16_t(r16(env)), y = int16_t(r16(env + 2)), w = int16_t(r16(env + 4)), h = int16_t(r16(env + 6));
    int ox = int16_t(r16(env + 8)), oy = int16_t(r16(env + 10));
    uint32_t tpage = r16(env + 0x14);
    bool dtd = r8(env + 0x16), dfe = r8(env + 0x17), isbg = r8(env + 0x18);
    using hw::gpu_gp0;
    gpu_gp0(0xE1000000 | (tpage & 0x1FF) | (dtd ? 1u << 9 : 0) | (dfe ? 1u << 10 : 0));
    gpu_gp0(0xE2000000);
    gpu_gp0(0xE3000000 | (uint32_t(y & 0x3FF) << 10) | uint32_t(x & 0x3FF));
    gpu_gp0(0xE4000000 | (uint32_t((y + h - 1) & 0x3FF) << 10) | uint32_t((x + w - 1) & 0x3FF));
    gpu_gp0(0xE5000000 | uint32_t(ox & 0x7FF) | (uint32_t(oy & 0x7FF) << 11));
    if (isbg) {
        gpu_gp0(0x02000000 | uint32_t(r8(env + 0x19)) | uint32_t(r8(env + 0x1A)) << 8 | uint32_t(r8(env + 0x1B)) << 16);
        gpu_gp0(uint32_t(x & 0x3FF) | uint32_t(y & 0x1FF) << 16);
        gpu_gp0(uint32_t(w & 0x3FF) | uint32_t(h & 0x1FF) << 16);
    }
    shadow_x = x;
    shadow_y = y;
}

// DrawOTag: the packets of an ordering table, from its last entry.
void draw_ot(uint32_t addr) {
    for (int guard = 0; guard < 0x100000; guard++) {
        uint32_t header = r32(addr);
        uint32_t words = header >> 24;
        hw::gpu_packet_addr = addr & 0x1FFFFC;  // physical, as the DMA reports it
        for (uint32_t i = 1; i <= words; i++) hw::gpu_gp0(r32(addr + 4 * i));
        if ((header & 0xFFFFFF) == 0xFFFFFF) return;
        addr = 0x80000000 | (header & 0x1FFFFC);
    }
    log("shadow: ordering table doesn't end");
}

// Place the copy's camera for this moment: the eye between the last two ticks' positions, turned
// by the mouse look the game hasn't taken yet, and build its view (0x8004913C).
void place_camera(Cpu &c, double vblank_time) {
    uint32_t player = r32(PLAYER);
    if ((player & 0xFFE00003) != 0x80000000) return;
    uint32_t obj = r32(player + 0x388);
    if ((obj & 0xFFE00003) != 0x80000000) return;
    double period = std::max(1.0, double(snapshot_frame - previous_frame));
    double alpha = std::clamp((vblank_time - double(snapshot_frame)) / period, 0.0, 1.0);
    double e[3], t[3], u[3];
    for (int i = 0; i < 3; i++) {
        double now = eye[i], before = previous_eye[i];
        e[i] = before + (now - before) * alpha;
        t[i] = double(int32_t(r32(obj + 4 * i))) - now;  // relative to the latest eye
        u[i] = double(int32_t(r32(obj + 0x6C + 4 * i))) - now;
    }
    double yaw = 0, pitch = 0;
    if (!game_aiming()) kbm_pending_look(yaw, pitch);
    if (yaw != 0 || pitch != 0) {
        // Yaw about the world's vertical (y, down); pitch about the view's horizontal axis.
        double a = yaw * M_PI / 180, ca = std::cos(a), sa = std::sin(a);
        auto turn = [&](double v[3]) {
            double x = v[0] * ca - v[2] * sa, z = v[0] * sa + v[2] * ca;
            v[0] = x;
            v[2] = z;
        };
        turn(t);
        turn(u);
        double fl = std::hypot(t[0], t[2]);
        if (fl > 0) {
            // Right axis (horizontal, perpendicular to the view), then Rodrigues' rotation.
            double k[3] = {t[2] / fl, 0, -t[0] / fl};
            double b = -pitch * M_PI / 180, cb = std::cos(b), sb = std::sin(b);
            auto tilt = [&](double v[3]) {
                double kv = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
                double cx[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]};
                for (int i = 0; i < 3; i++) v[i] = v[i] * cb + cx[i] * sb + k[i] * kv * (1 - cb);
            };
            tilt(t);
            tilt(u);
        }
    }
    for (int i = 0; i < 3; i++) {
        psx_w32(&c, player + 0x20C + 4 * i, uint32_t(int32_t(std::lround(e[i]))));
        psx_w32(&c, obj + 4 * i, uint32_t(int32_t(std::lround(e[i] + t[i]))));
        psx_w32(&c, obj + 0x6C + 4 * i, uint32_t(int32_t(std::lround(e[i] + u[i]))));
    }
    Cpu regs = c;
    c.r[4] = player;
    psx_call(&c, CAMERA_SETUP);
    regs.cycles = c.cycles;
    c = regs;
}

}  // namespace

bool shadow_active() { return active; }

bool shadow_fresh() { return have_snapshot && hw::gpu.frames <= snapshot_frame + 4; }

bool shadow_render(double vblank_time, int &x, int &y) {
    if (!have_snapshot || active) return false;
    // Only a copy from the latest tick (not a level left behind).
    if (hw::gpu.frames > snapshot_frame + 4) return false;
    Cpu &c = *hw::cpu;
    capture(saved, c);
    load(snapshot, c);
    hw::gpu_shadow_begin();
    hw::gl_renderer_shadow(true);
    interp_shadow(true);
    active = true;
    c.next_event = INT64_MAX;  // no interrupts or devices: the copy only draws

    place_camera(c, vblank_time);
    uint32_t buffer = r32(FRAME_DATA + 0x2204);
    put_draw_env(FRAME_DATA + 0x10 + 0xB8 * (buffer ? 0 : 1));
    psx_call(&c, PASS);

    active = false;
    interp_shadow(false);
    hw::gl_renderer_shadow(false);
    hw::gpu_shadow_end();
    if (static bool trace = getenv("VALOR_TRACE_POOL"); trace)
        log("shadow pool: %u of %u bytes", r32(0x80098958) - r32(0x80098980), 2 * r32(r32(0x8009CC64) + 0x98));
    load(saved, c);
    x = shadow_x;
    y = shadow_y;
    return true;
}

}  // namespace psx

using namespace psx;

// 0x800503D4: the drawing pass. The tick's logic is done: keep a copy of the machine to draw again.
extern "C" void hook_render_pass(Cpu *c, RecompFn body) {
    if (!active && config.decoupled) {
        capture(snapshot, *c);
        uint32_t player = psx_r32(c, PLAYER);
        memcpy(previous_eye, eye, sizeof eye);
        for (int i = 0; i < 3; i++) eye[i] = int32_t(psx_r32(c, player + 0x20C + 4 * i));
        if (!have_snapshot) memcpy(previous_eye, eye, sizeof eye);
        previous_frame = have_snapshot ? snapshot_frame : hw::gpu.frames;
        have_snapshot = true;
        snapshot_frame = hw::gpu.frames;
    }
    body(c);
}

// 0x800504F8: flip and DrawOTag. In a shadow pass: draw this buffer's table now.
extern "C" void hook_render_flip(Cpu *c, RecompFn body) {
    if (!active) return body(c);
    uint32_t buffer = r32(FRAME_DATA + 0x2204);
    draw_ot(OT_BASE + (buffer << 12) + 0x7FC);
}

// 0x80053B8C(buffer): starts this buffer's primitive pool: [0x80098958] the next free byte,
// [0x8009895C] the last, from [0x80098980] + buffer * size ([[0x8009CC64]+0x98], 80000 bytes).
// The game drops what doesn't fit (0x80010B60 stops drawing the world), and a wider view than the
// original's fills it: the floor under the player, drawn late, and the weapon, drawn last, went
// missing. A shadow pass draws into a copy, where the other buffer's pool (the frame on the
// screen) isn't needed: it gets both.
extern "C" void hook_prim_pool(Cpu *c, RecompFn body) {
    body(c);
    if (!active) return;
    uint32_t base = r32(0x80098980), size = r32(r32(0x8009CC64) + 0x98);
    psx_w32(c, 0x80098958, base);
    psx_w32(c, 0x8009895C, base + 2 * size - 1);
}

// 0x8001722C ClearOTagR(ot, n): in a shadow pass, without the DMA.
extern "C" void hook_clear_otag_r(Cpu *c, RecompFn body) {
    if (!active) return body(c);
    uint32_t ot = c->r[4], n = c->r[5];
    for (uint32_t i = 1; i < n; i++) psx_w32(c, ot + 4 * i, (ot + 4 * (i - 1)) & 0xFFFFFF);
    if (n) psx_w32(c, ot, 0xFFFFFF);
    c->r[2] = 0;
}
