#include "valor/controls.h"

#include <algorithm>
#include <cstring>

#include "valor/fixed.h"

namespace valor {

namespace {

uint32_t ram32(const std::vector<uint8_t>& m, uint32_t a) {
    a &= 0x1FFFFF;
    return uint32_t(m[a]) | (uint32_t(m[a + 1]) << 8) | (uint32_t(m[a + 2]) << 16) | (uint32_t(m[a + 3]) << 24);
}

}  // namespace

bool PadConfig::from_ram(const std::vector<uint8_t>& m) {
    if (m.size() < 0x200000) return false;
    for (int r = 0; r < 4; r++) {
        uint32_t b = 0x800B75F0 + 0x34 * r;
        RoleButton& rb = buttons[r];
        rb.neg = ram32(m, b), rb.pos = ram32(m, b + 4), rb.mod = ram32(m, b + 8), rb.mod_down = ram32(m, b + 0xC);
        rb.lo = int32_t(ram32(m, b + 0x10)), rb.hi = int32_t(ram32(m, b + 0x14));
        rb.base = int32_t(ram32(m, b + 0x18)), rb.max = int32_t(ram32(m, b + 0x1C));
        rb.invert = int32_t(ram32(m, b + 0x20)), rb.out = int32_t(ram32(m, b + 0x24));
        rb.accel = int32_t(ram32(m, b + 0x28)), rb.ramp = int32_t(ram32(m, b + 0x2C));
        rb.flags = int32_t(ram32(m, b + 0x30));
        uint32_t a = 0x800B74B0 + 0x28 * r;
        RoleAxis& ra = axes[r];
        ra.axis = ram32(m, a), ra.mod = ram32(m, a + 4), ra.mod_down = ram32(m, a + 8);
        ra.lo = m[(a + 0xC) & 0x1FFFFF], ra.hi = m[(a + 0xD) & 0x1FFFFF], ra.centre = m[(a + 0xE) & 0x1FFFFF];
        ra.neg_scale = int32_t(ram32(m, a + 0x14)), ra.pos_scale = int32_t(ram32(m, a + 0x18));
        ra.base = int32_t(ram32(m, a + 0x1C)), ra.speed = int32_t(ram32(m, a + 0x20));
        ra.invert = int32_t(ram32(m, a + 0x24));
    }
    for (int i = 0; i < 9; i++) action[i] = ram32(m, 0x800B7790 + 4 * i), action_not[i] = ram32(m, 0x800B77E0 + 4 * i);
    std::memcpy(curve, &m[0x8009E3AC & 0x1FFFFF], sizeof curve);
    look_mode = m[0x8009E3A0 & 0x1FFFFF];
    aim_pitch_from_move = ram32(m, 0x8009E398);
    return true;
}

PadConfig PadConfig::fresh_card() {
    PadConfig c;
    // Forward d-pad up/down (Triangle up), strafe L1/R1, pitch d-pad up/down with Triangle,
    // turn d-pad left/right: 35.0 and 60.0 at most, ramps of 32.0 and 2.0 (movement.md).
    const uint32_t neg[4] = {0x1000, 0x4, 0x1000, 0x8000}, pos[4] = {0x4000, 0x8, 0x4000, 0x2000};
    const uint32_t mod[4] = {0x10, 0, 0x10, 0}, mod_down[4] = {0, 0, 1, 0};
    for (int r = 0; r < 4; r++) {
        RoleButton& b = c.buttons[r];
        int32_t v = r < 2 ? 0x230000 : 0x3C0000;
        b.neg = neg[r], b.pos = pos[r], b.mod = mod[r], b.mod_down = mod_down[r];
        b.lo = -v, b.hi = v, b.base = v, b.max = v;
        b.accel = r < 2 ? 0x200000 : 0x20000;
        b.flags = 2;
        // Sticks: ly forward, rx strafe, ry pitch, lx turn; centre 0x5B (a wide deadzone).
        RoleAxis& a = c.axes[r];
        const uint32_t axis[4] = {3, 0, 1, 2};
        a.axis = axis[r], a.lo = 0, a.hi = 0xFF, a.centre = 0x5B;
        a.neg_scale = 0x7FFF / 0x5B, a.pos_scale = 0x7FFF / (0xFF - 0x5B);
        a.base = a.speed = v;
    }
    // Fire Cross, jump Triangle, switch Circle, crouch L2, reload and Use Square, centre L3, aim R2.
    const uint32_t action[9] = {0x40, 0x10, 0x20, 0x1, 0x80, 0x80, 0x800, 0x200, 0x2};
    std::memcpy(c.action, action, sizeof action);
    c.look_mode = 1;
    c.aim_pitch_from_move = 1;
    return c;
}

bool Controls::action_held(int a) const {
    uint32_t mask = cfg.action[a], no = cfg.action_not[a];
    return (pad.now & mask) == mask && (no == 0 || (pad.now & no) != no);
}

bool Controls::action_pressed(int a) const {
    uint32_t mask = cfg.action[a], no = cfg.action_not[a];
    uint32_t down = pad.now & mask;
    bool edge = down == mask && ((pad.prev & down) ^ down) != 0;
    return edge && (no == 0 || (pad.now & no) != no);
}

bool Controls::buttons(uint32_t mask, bool edge, bool up) const {
    if (up) {
        if (edge) return !(pad.now & mask) && (pad.prev & mask) != 0;
        return (pad.now & mask) == 0;
    }
    if (edge) return (pad.now & mask) == mask && ((pad.prev & mask) ^ mask) != 0;
    return (pad.now & mask) == mask;
}

int32_t Controls::role_button(int role, bool use_mod) {
    RoleButton& b = cfg.buttons[role];
    int dir = 0;
    if (!use_mod || b.mod == 0 || buttons(b.mod, false, b.mod_down == 0)) {
        if ((pad.now & b.neg) == b.neg)
            dir = b.invert ? 1 : -1;
        else if ((pad.now & b.pos) == b.pos)
            dir = b.invert ? -1 : 1;
    }
    if (dir == 0) {
        if ((b.flags & 6) == 2) b.out = 0;
        switch (b.flags & 0x18) {
        case 0:
            b.ramp = 0;
            break;
        case 8: {
            int32_t r = s32(int64_t(b.ramp) - int64_t(b.accel) * 4);
            b.ramp = r <= 0 ? 0 : r;
            break;
        }
        }
    } else {
        int32_t r = s32(int64_t(b.ramp) + b.accel);
        if (b.max < r) r = b.max;
        b.ramp = r;
        if (dir < 0) {
            int32_t o = s32(int64_t(b.out) - r);
            b.out = -b.max >= o ? -b.max : o;
        } else {
            int32_t o = s32(int64_t(b.out) + r);
            b.out = b.max < o ? b.max : o;
        }
    }
    calls.push_back({role, 0, b.out});
    return b.out;
}

int32_t Controls::axis_curve(const RoleAxis& a, int axis, bool use_mod) const {
    if (use_mod && a.mod != 0 && !buttons(a.mod, false, a.mod_down == 0)) return 0;
    uint32_t v = pad.sticks[axis & 3];
    int64_t r;
    if (v < a.centre) {
        int64_t i = std::min<int64_t>((int64_t(a.centre - v) * a.neg_scale) >> 8, 127);
        r = -int64_t(cfg.curve[i]) * (a.speed >> 8);
    } else if (v >= 0xA6) {
        int64_t i = std::min<int64_t>((int64_t(v - a.centre) * a.pos_scale) >> 8, 127);
        r = int64_t(cfg.curve[i]) * (a.speed >> 8);
    } else {
        r = 0;
    }
    return s32(a.invert ? -r : r);
}

int32_t Controls::role_axis(int role, bool use_mod) const {
    if (axis_override[role]) return axis_value[role];
    const RoleAxis& a = cfg.axes[role];
    return axis_curve(a, int(a.axis), use_mod);
}

int32_t Controls::role_axis_alt(int from_role, int cfg_role) const {
    if (!cfg.aim_pitch_from_move) return 0;
    return axis_curve(cfg.axes[cfg_role], int(cfg.axes[from_role].axis), true);
}

int32_t Controls::role(int r) {
    int32_t v = role_button(r);
    if (v == 0 && pad.analog) {
        v = role_axis(r);
        calls.push_back({r, 1, v});
    }
    return v;
}

void Controls::role_button_set_speed(int role, int32_t n, int mode) {
    RoleButton& b = cfg.buttons[role];
    int32_t v = n;
    switch (mode) {
    case 2: v = b.base; break;
    case 3: v = s32(int64_t(n) * b.base); break;
    case 5: v = s32(int64_t(b.base) << n); break;
    case 4: v = b.base >> n; break;
    }
    if (b.invert) b.lo = v, b.hi = -v;
    else b.lo = -v, b.hi = v;
    if (mode == 0) b.base = v;
    b.max = v;
}

void Controls::role_axis_set_speed(int role, int32_t n, int mode) {
    RoleAxis& a = cfg.axes[role];
    int32_t v = n;
    switch (mode) {
    case 2: v = a.base; break;
    case 3: v = s32(int64_t(n) * a.base); break;
    case 5: v = s32(int64_t(a.base) << n); break;
    case 4: v = a.base >> n; break;
    }
    uint8_t lo = uint8_t(a.centre - a.lo), hi = uint8_t(a.hi - a.centre);
    a.neg_scale = 0x7FFF / (lo ? lo : 1);
    a.pos_scale = 0x7FFF / (hi ? hi : 1);
    if (mode == 0) a.base = v;
    a.speed = v;
}

}  // namespace valor
