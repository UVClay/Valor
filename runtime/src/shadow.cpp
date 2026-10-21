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
#include <string>
#include <vector>

#include "game.h"

#include "gpu.h"
#include "gpu_gl.h"
#include "hw.h"
#include "interp.h"
#include "runtime.h"
#include "shadow.h"
#include "tape.h"

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
uint64_t pass_flips;  // gpu.flips after the latest copy's drawing pass
int32_t eye[3];  // the player's position in the latest copy
// The view's direction. The camera looks at a point about 200 units away in whole units, built
// a tick behind the soldier's look: shown as is, its direction steps by up to a few tenths of a
// degree each tick. The copy looks along the soldier's own yaw and pitch instead
// ([[0x8009D654]+0x388]+0x184 and +0x178, 16.16 with 4096 per turn), as they are when the copy was
// taken, turned further by the mouse look the game took or will take after it. What the camera
// adds to them (its own motion, the target's rounding) is blended from one tick to the next.
constexpr uint32_t SOLDIER = 0x8009D654;
constexpr double ANGLE_DEGREES = 360.0 / 4096 / 65536;
constexpr double PITCH_LIMIT = 0x2AA0000 * ANGLE_DEGREES;  // the game's clamp, about 60 degrees
bool look_known, built_known;  // the soldier's look was found at the copy, at the camera's build
double look[2], built_look[2];
double copy_taken[2];  // kbm_look_taken when the copy was taken
double offset[2];  // the camera's direction minus the look it was built from
// The soldier's look is the mouse look the game applied (`applied`: what it took between copies,
// outside aim mode) and the rest (`own`: its own turns, the stick); the rest is blended.
double applied[2], own[2];

double wrap(double degrees) { return std::remainder(degrees, 360.0); }

// The eye and the camera's offset shown move to the latest copy's over a tick, from what was shown
// when it came: steady even when the game's ticks vary in length (a copy's first frame is shown a
// vblank or more after it was taken).
struct Blend {
    uint64_t snapshot = ~0ull;  // the copy it moves to
    // eye x, y, z (16.16); offset yaw, pitch; own yaw, pitch (degrees)
    double from[7], to[7], shown[7];
    double start = 0, period = 2, last = 0;  // vblanks
    bool started = false;
} blend;

void blend_to(double vblank_time, double out[7]) {
    if (blend.snapshot != snapshot_frame) {
        double to[7] = {double(eye[0]), double(eye[1]), double(eye[2]), offset[0], offset[1], own[0], own[1]};
        if (!blend.started) memcpy(blend.shown, to, sizeof to);
        memcpy(blend.from, blend.shown, sizeof to);
        bool cut = false;
        for (int i = 3; i < 7; i++) {
            to[i] = blend.from[i] + wrap(to[i] - blend.from[i]);
            cut |= std::abs(to[i] - blend.from[i]) > 20;
        }
        memcpy(blend.to, to, sizeof to);
        // A camera cut or a teleport isn't blended.
        cut |= std::hypot(to[0] - blend.from[0], to[1] - blend.from[1], to[2] - blend.from[2]) / 65536 > 512;
        if (cut) memcpy(blend.from, to, sizeof to);
        blend.start = blend.started ? blend.last : vblank_time;
        blend.period = std::clamp(double(snapshot_frame - previous_frame), 1.0, 4.0);
        blend.snapshot = snapshot_frame;
        blend.started = true;
    }
    double alpha = std::clamp((vblank_time - blend.start) / blend.period, 0.0, 1.0);
    for (int i = 0; i < 7; i++) out[i] = blend.shown[i] = blend.from[i] + (blend.to[i] - blend.from[i]) * alpha;
    blend.last = vblank_time;
}
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

// The soldier's yaw and pitch (degrees; atan2(x, z) and up, as the view's direction).
bool soldier_look(Cpu &c, double out[2]) {
    uint32_t soldier = psx_r32(&c, SOLDIER);
    if ((soldier & 0xFFE00003) != 0x80000000) return false;
    uint32_t controls = psx_r32(&c, soldier + 0x388);
    if ((controls & 0xFFE00003) != 0x80000000) return false;
    out[0] = int32_t(psx_r32(&c, controls + 0x184)) * ANGLE_DEGREES;
    out[1] = int32_t(psx_r32(&c, controls + 0x178)) * ANGLE_DEGREES;
    return true;
}

// Where the camera of the latest copy looks (degrees), as soldier_look.
void camera_look(Cpu &c, uint32_t obj, const int32_t from[3], double out[2]) {
    double t[3];
    for (int i = 0; i < 3; i++) t[i] = double(int32_t(psx_r32(&c, obj + 4 * i))) - from[i];
    out[0] = std::atan2(t[0], t[2]) * 180 / M_PI;
    out[1] = std::atan2(-t[1], std::hypot(t[0], t[2])) * 180 / M_PI;
}

bool invert(const double m[3][3], double out[3][3]) {
    double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                 m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (std::abs(det) < 1e-9) return false;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            int a = (j + 1) % 3, b = (j + 2) % 3, c = (i + 1) % 3, d = (i + 2) % 3;
            out[i][j] = (m[a][c] * m[b][d] - m[a][d] * m[b][c]) / det;
        }
    return true;
}

// The weapon is drawn with one transform (0x80080FB0), made from the camera's view and its own
// place: drawn again for a moved camera it would slide across the screen, and its sway and bob
// change with the game's ticks. The copy draws it where the game drew it (its transform in the
// tick's own drawing pass), blended from the place shown to the latest tick's, as the eye.
struct WeaponBlend {
    bool known = false;  // the latest tick drew it (latest: its transform)
    int16_t rt[3][3];
    int32_t tr[3];
    uint32_t pc;
    uint64_t snapshot = ~0ull;
    double from[12], to[12], shown[12];  // rotation (row-major, 1 = 4096), translation
    double start = 0, period = 2, last = 0;
    bool started = false;
} weapon;
double present_time;  // the moment being drawn (vblanks)

void weapon_blend(const int16_t rt[3][3], const int32_t tr[3], uint32_t pc, double a[3][3], double b[3]) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) a[i][j] = i == j;
        b[i] = 0;
    }
    if (!weapon.known || pc != weapon.pc) return;
    double now[12], to[12];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            now[3 * i + j] = rt[i][j] / 4096.0;
            to[3 * i + j] = weapon.rt[i][j] / 4096.0;
        }
        now[9 + i] = tr[i];
        to[9 + i] = weapon.tr[i];
    }
    if (weapon.snapshot != snapshot_frame) {
        if (!weapon.started) memcpy(weapon.shown, to, sizeof to);
        memcpy(weapon.from, weapon.shown, sizeof to);
        memcpy(weapon.to, to, sizeof to);
        // A jump isn't blended (another weapon is: it swings in).
        if (std::hypot(to[9] - weapon.from[9], to[10] - weapon.from[10], to[11] - weapon.from[11]) > 200)
            memcpy(weapon.from, to, sizeof to);
        weapon.start = weapon.started ? weapon.last : present_time;
        weapon.period = std::clamp(double(snapshot_frame - previous_frame), 1.0, 4.0);
        weapon.snapshot = snapshot_frame;
        weapon.started = true;
    }
    double alpha = std::clamp((present_time - weapon.start) / weapon.period, 0.0, 1.0);
    for (int i = 0; i < 12; i++) weapon.shown[i] = weapon.from[i] + (weapon.to[i] - weapon.from[i]) * alpha;
    weapon.last = present_time;
    // The move from where the copy draws it to the place shown: shown * now^-1.
    double m[3][3], inv[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m[i][j] = now[3 * i + j];
    if (!invert(m, inv)) return;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            const double *s = weapon.shown + 3 * i;
            a[i][j] = s[0] * inv[0][j] + s[1] * inv[1][j] + s[2] * inv[2][j];
        }
        b[i] = weapon.shown[9 + i] - (a[i][0] * now[9] + a[i][1] * now[10] + a[i][2] * now[11]);
    }
    // A move that isn't a small sway (the copy drew something else first): none.
    bool small = true;
    for (int i = 0; i < 3; i++) {
        small &= std::abs(b[i]) < 2000;
        for (int j = 0; j < 3; j++) small &= std::abs(a[i][j] - (i == j)) < 1;
    }
    if (!small)
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) a[i][j] = i == j;
            b[i] = 0;
        }
}

// Place the copy's camera for this moment: the eye blended toward the latest tick's, looking along
// the soldier's look turned by the mouse look the game hasn't applied yet, and build its view
// (0x8004913C).
void place_camera(Cpu &c, double vblank_time) {
    uint32_t player = r32(PLAYER);
    if ((player & 0xFFE00003) != 0x80000000) return;
    uint32_t obj = r32(player + 0x388);
    if ((obj & 0xFFE00003) != 0x80000000) return;
    double shown[7];
    blend_to(vblank_time, shown);
    double e[3], t[3], u[3];
    for (int i = 0; i < 3; i++) {
        e[i] = shown[i];
        t[i] = double(int32_t(r32(obj + 4 * i))) - eye[i];  // relative to the latest eye
        u[i] = double(int32_t(r32(obj + 0x6C + 4 * i))) - eye[i];
    }
    // The look shown: the soldier's, the mouse look taken since the copy and the motion not taken
    // yet (in aim mode the mouse moves the game's reticle instead), and the camera's offset. Turned
    // from where the copy's camera looks.
    double base[2];
    camera_look(c, obj, eye, base);
    double yaw = 0, pitch = 0;
    if (!game_aiming()) {
        double taken[2];
        kbm_look_taken(taken[0], taken[1]);
        kbm_pending_look(yaw, pitch);
        yaw += taken[0] - copy_taken[0];
        pitch += taken[1] - copy_taken[1];
    }
    if (look_known) {
        double p = std::clamp(shown[6] + applied[1] + pitch, -PITCH_LIMIT, PITCH_LIMIT);
        yaw = wrap(shown[5] + applied[0] + yaw + shown[3] - base[0]);
        pitch = p + shown[4] - base[1];
    }
    if (yaw != 0 || pitch != 0) {
        // Yaw about the world's vertical (y, down); pitch about the view's horizontal axis.
        double a = -yaw * M_PI / 180, ca = std::cos(a), sa = std::sin(a);
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
            double b = pitch * M_PI / 180, cb = std::cos(b), sb = std::sin(b);
            auto tilt = [&](double v[3]) {
                double kv = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
                double cx[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]};
                for (int i = 0; i < 3; i++) v[i] = v[i] * cb + cx[i] * sb + k[i] * kv * (1 - cb);
            };
            tilt(t);
            tilt(u);
        }
    }
    // 0x8004913C makes the view's rotation from the target's whole units (16 bits), about 200
    // away: its direction would step by up to 0.3 degrees. Farther away on the same direction,
    // as far as 16 bits allow, it is exact to a few thousandths.
    double far = 64;
    for (int i = 0; i < 3; i++) {
        double room = (t[i] > 0 ? 32000.0 * 65536 - e[i] : 32000.0 * 65536 + e[i]) / std::abs(t[i]);
        if (t[i] != 0) far = std::min(far, room);
    }
    far = std::max(far, 1.0);
    for (int i = 0; i < 3; i++) {
        psx_w32(&c, player + 0x20C + 4 * i, uint32_t(int32_t(std::lround(e[i]))));
        psx_w32(&c, obj + 4 * i, uint32_t(int32_t(std::lround(e[i] + t[i] * far))));
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

// The latest copy is what the game shows: it hasn't shown anything else since (a 2D screen) but
// the frame its own pass drew (shown at a vblank after it), and its ticks keep coming (slowly, as
// when a mission ends).
// (Replaying a recording made before, as then: a copy at most 4 vblanks old, see tape.h.)
bool shadow_fresh() {
    if (tape_copies_touch_devices()) return have_snapshot && hw::gpu.frames <= snapshot_frame + 4;
    return have_snapshot && hw::gpu.flips <= pass_flips + 1 && hw::gpu.frames <= snapshot_frame + 60;
}

bool shadow_render(double vblank_time, int &x, int &y) {
    if (!have_snapshot || active) return false;
    if (!shadow_fresh()) return false;
    Cpu &c = *hw::cpu;
    capture(saved, c);
    load(snapshot, c);
    // Only a copy taken mid-mission, with a buffer to draw into.
    uint32_t buffer = r32(FRAME_DATA + 0x2204);
    if (!game_in_level() || buffer > 1) {
        load(saved, c);
        have_snapshot = false;
        return false;
    }
    hw::gpu_shadow_begin();
    hw::gl_renderer_shadow(true);
    interp_shadow(true);
    active = true;
    c.next_event = INT64_MAX;  // no interrupts or devices: the copy only draws

    // The copy runs the CPU's code: whatever it does to the coprocessor's state and the exception
    // stack is the copy's too.
    Cop0 cop0_saved = cop0;
    int isolated = psx_cache_isolated;
    size_t depth = exception_depth();
    bool drawn = true;
    try {
        present_time = vblank_time;
        place_camera(c, vblank_time);
        gte_other_transform(weapon_blend);
        put_draw_env(FRAME_DATA + 0x10 + 0xB8 * (buffer ? 0 : 1));
        psx_call(&c, PASS);
    } catch (const Halt &h) {
        // The copy went wrong: the game itself is untouched. Its own frame is shown instead, and
        // the copy isn't used again.
        std::string recent;
        for (uint32_t i = 8; i > 0; i--) {
            char buf[12];
            snprintf(buf, sizeof buf, " %08X", psx_trace_ring[(psx_trace_pos - i) & (PSX_TRACE_SIZE - 1)]);
            recent += buf;
        }
        log("shadow: %s at vblank %llu (drawing the tick's frame instead); ra=%08X, recent functions:%s", h.what(),
            (unsigned long long)hw::gpu.frames, c.r[31], recent.c_str());
        drawn = have_snapshot = false;
    }

    if (static bool trace = getenv("VALOR_TRACE_SHADOW"); trace &&
        (memcmp(&cop0_saved, &cop0, sizeof cop0) || isolated != psx_cache_isolated || depth != exception_depth()))
        log("shadow: the copy changed sr %08X -> %08X, exceptions %zu -> %zu", cop0_saved.sr, cop0.sr, depth, exception_depth());
    cop0 = cop0_saved;
    psx_cache_isolated = isolated;
    exception_unwind(depth);
    gte_other_transform(nullptr);
    active = false;
    interp_shadow(false);
    hw::gl_renderer_shadow(false);
    hw::gpu_shadow_end();
    if (static bool trace = getenv("VALOR_TRACE_POOL"); trace && drawn)
        log("shadow pool: %u of %u bytes", r32(0x80098958) - r32(0x80098980), 2 * r32(r32(0x8009CC64) + 0x98));
    load(saved, c);
    if (!drawn) return false;
    x = shadow_x;
    y = shadow_y;
    return true;
}

}  // namespace psx

using namespace psx;

// 0x800503D4: the drawing pass. The tick's logic is done: keep a copy of the machine to draw again.
extern "C" void hook_render_pass(Cpu *c, RecompFn body) {
    if (!active) watch_tick(c);
    if (!active && config.decoupled) {
        capture(snapshot, *c);
        uint32_t player = psx_r32(c, PLAYER);
        for (int i = 0; i < 3; i++) eye[i] = int32_t(psx_r32(c, player + 0x20C + 4 * i));
        double taken[2];
        kbm_look_taken(taken[0], taken[1]);
        bool known = look_known;
        look_known = built_known && soldier_look(*c, look);
        if (look_known) {
            double base[2];
            camera_look(*c, psx_r32(c, player + 0x388), eye, base);
            for (int i = 0; i < 2; i++) {
                offset[i] = wrap(base[i] - built_look[i]);
                if (known && !game_aiming()) applied[i] += taken[i] - copy_taken[i];
                own[i] = wrap(look[i] - applied[i]);
            }
        }
        memcpy(copy_taken, taken, sizeof taken);
        previous_frame = have_snapshot ? snapshot_frame : hw::gpu.frames;
        have_snapshot = true;
        snapshot_frame = hw::gpu.frames;
        gte_record_other();
        body(c);
        weapon.known = gte_recorded_other(weapon.rt, weapon.tr, weapon.pc);
        pass_flips = hw::gpu.flips;
        return;
    }
    body(c);
}

// 0x8004913C(player): the game builds its camera, from the soldier's look as it is now.
extern "C" void hook_camera_setup(Cpu *c, RecompFn body) {
    if (!active) built_known = soldier_look(*c, built_look);
    body(c);
}

// 0x800504F8: flip and DrawOTag. In a shadow pass: draw this buffer's table now.
extern "C" void hook_render_flip(Cpu *c, RecompFn body) {
    if (!active) return body(c);
    uint32_t buffer = r32(FRAME_DATA + 0x2204) & 1;
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
