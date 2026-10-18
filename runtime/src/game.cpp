// Medal of Honor state the runtime reads (pause, aim mode, controller configuration) and the
// native hooks that feed mouse and keyboard input into its controls. Addresses: docs/RECON.md.
#include "game.h"

#include <algorithm>
#include <cmath>

#include "hw.h"
#include "input.h"

namespace psx {

namespace {

constexpr uint32_t OVERLAY_BASE = 0x80039D90;  // first word: the resident overlay's id
constexpr uint32_t LEVEL_ID = 5;
constexpr uint32_t PAUSED = 0x8009A5E0;        // set by the start-button handler (0x800625BC)
constexpr uint32_t PLAYER_STATE = 0x8009D654;  // -> struct; +0x10 bit 0x10: aim mode
constexpr uint32_t ACTION_BUTTONS = 0x800B7790;  // (port * 9 + action) * 4: button mask
constexpr uint32_t ROLE_AXES = 0x800B74B0;       // port * 160 + role * 40: analog role config

uint32_t r32(uint32_t addr) { return psx_r32(hw::cpu, addr); }

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
    double add = 0;
    if (role <= 1) {
        double forward, strafe;
        kbm_movement(forward, strafe);
        add = role == 0 ? -forward * full : strafe * full;
    } else {
        constexpr double UNITS_PER_DEGREE = 4096.0 * 65536.0 / 360.0;
        double degrees = role == 3 ? kbm_take_turn() : -kbm_take_pitch();
        add = degrees * UNITS_PER_DEGREE * (role == 2 ? 2 : 1);
    }
    if (add == 0) return;
    double v = double(int32_t(c->r[2])) + add;
    if (role <= 1) v = std::clamp(v, -full, full);
    c->r[2] = uint32_t(int32_t(std::clamp(v, -2.0e9, 2.0e9)));
}
