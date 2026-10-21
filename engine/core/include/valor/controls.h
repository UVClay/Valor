// The pad and the control roles (docs/re/movement.md, Control roles): a pad's buttons and sticks
// become the four roles' values (forward, strafe, pitch, turn) through a configuration of digital
// ramps and analog curves, and actions are button masks. One port. The functions keep the
// original's names and state; they are called in the original's order, since role_button keeps
// a ramp between calls.
#pragma once

#include <cstdint>
#include <vector>

namespace valor {

enum Role { kRoleForward = 0, kRoleStrafe = 1, kRolePitch = 2, kRoleTurn = 3 };
enum Action { kFire = 0, kJump, kSwitch, kCrouch, kReload, kUse, kAction6, kCenter, kAim };

// A role's digital configuration (g_role_buttons 0x800B75F0 + port 0xD0 + role 0x34).
struct RoleButton {
    uint32_t neg = 0, pos = 0;          // +0x0, +0x4 button masks (all bits down)
    uint32_t mod = 0, mod_down = 0;     // +0x8 modifier mask, +0xC: non-zero, it must be down (else up)
    int32_t lo = 0, hi = 0;             // +0x10, +0x14 (set_speed)
    int32_t base = 0, max = 0;          // +0x18 the speed set_speed restores, +0x1C the maximum
    int32_t invert = 0;                 // +0x20
    int32_t out = 0;                    // +0x24 the output, kept between calls
    int32_t accel = 0, ramp = 0;        // +0x28, +0x2C
    int32_t flags = 0;                  // +0x30: &6 == 2 output to 0 on release; &0x18 the ramp's release
};

// A role's analog configuration (g_role_axes 0x800B74B0 + port 0xA0 + role 0x28).
struct RoleAxis {
    uint32_t axis = 0;                  // +0x0: the stick byte (rx, ry, lx, ly)
    uint32_t mod = 0, mod_down = 0;     // +0x4, +0x8: as RoleButton's
    uint8_t lo = 0, hi = 0, centre = 0; // +0xC, +0xD, +0xE
    int32_t neg_scale = 0, pos_scale = 0;  // +0x14 = 0x7FFF / (centre - lo), +0x18 = 0x7FFF / (hi - centre)
    int32_t base = 0, speed = 0;        // +0x1C the speed set_speed restores, +0x20 the speed
    int32_t invert = 0;                 // +0x24
};

struct PadConfig {
    RoleButton buttons[4];
    RoleAxis axes[4];
    uint32_t action[9]{};       // 0x800B7790 + port 9 × 4: the action's mask (all bits down)
    uint32_t action_not[9]{};   // 0x800B77E0: and not all of these (0: no exclusion)
    uint8_t curve[128]{};       // g_axis_curve 0x8009E3AC
    uint8_t look_mode = 1;      // 0x8009E3A0[port] (movement.md, Turning and looking)
    uint32_t aim_pitch_from_move = 0;  // 0x8009E398[port]

    // From a RAM dump (2 MB, the PS1's main RAM), port 0.
    bool from_ram(const std::vector<uint8_t>& ram);
    // The configuration a fresh memory card gives (read from a new game's RAM), without the
    // analog curve, which lives in the game's data: a host that reads no pad sticks needs none.
    static PadConfig fresh_card();
};

// The pad as the game reads it each tick.
struct PadState {
    uint32_t now = 0, prev = 0;  // 0x80093B7C, 0x80093B84 (port 0)
    uint8_t sticks[4]{};         // 0x80093B70: rx, ry, lx, ly
    uint8_t analog = 0;          // 0x80093B78: the pad in analog mode
};

class Controls {
public:
    PadConfig cfg;
    PadState pad;

    // A host that turns the view or moves through the analog roles (mouse, keyboard, a modern
    // stick: the runtime's hook on role_axis) sets the role's value here; role_axis returns it.
    bool axis_override[4]{};
    int32_t axis_value[4]{};

    bool action_held(int a) const;     // 0x8007570C
    bool action_pressed(int a) const;  // 0x80075778
    // func_8004D590: the mask's buttons all down (or, `up`, not all down); `edge`: and newly so.
    bool buttons(uint32_t mask, bool edge, bool up) const;

    int32_t role_button(int role, bool use_mod = true);  // 0x80075A30
    int32_t role_axis(int role, bool use_mod = true) const;  // 0x80075FB4 -> role_axis_curve
    int32_t role_axis_alt(int from_role, int cfg_role) const;  // 0x80076008: pitch from another axis
    int32_t role_button_max(int role) const { return cfg.buttons[role].max; }  // 0x800759EC
    // The role as player_controls reads it: the buttons', else (analog mode) the stick's.
    int32_t role(int role);

    void role_button_set_speed(int role, int32_t n, int mode);  // 0x800758B4
    void role_axis_set_speed(int role, int32_t n, int mode);    // 0x80075D1C

    // Role calls of the last tick, for checking against the original: (role, src 0/1, value).
    struct Call {
        int role, src;
        int32_t value;
    };
    std::vector<Call> calls;

private:
    int32_t axis_curve(const RoleAxis& a, int axis, bool use_mod) const;
};

}  // namespace valor
