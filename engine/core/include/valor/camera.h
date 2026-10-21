// The camera entity (type 2) in its normal mode (docs/re/movement.md, The camera): camera_think
// (0x800473DC: ease, look points, place, shake) and camera_setup (0x8004913C) with view_lookat
// (0x80048EEC), which builds the view matrix aim_update reads. Ported from tools/sim/engine_camera.py.
//
// The camera object (the entity's +0x388) is kept as the original's words, because the player's
// update writes into it (the crouch's eye easing, the pitch's look-point height) and reads its
// halves (+0x26, +0x2E, +0xBE).
#pragma once

#include <cstdint>
#include <cstring>

#include "valor/gte.h"

namespace valor {

struct CameraObj {
    int32_t w[0x50]{};  // 0x140 bytes
    int32_t& at(uint32_t off) { return w[off >> 2]; }
    int32_t get(uint32_t off) const { return w[off >> 2]; }
    int16_t s16(uint32_t off) const {
        uint32_t v = uint32_t(w[off >> 2]);
        return int16_t((off & 2) ? (v >> 16) : (v & 0xFFFF));
    }
    void set16(uint32_t off, int16_t x) {
        uint32_t& v = reinterpret_cast<uint32_t&>(w[off >> 2]);
        v = (off & 2) ? ((v & 0xFFFF) | (uint32_t(uint16_t(x)) << 16)) : ((v & 0xFFFF0000u) | uint16_t(x));
    }
    int32_t eye_offset() const { return get(0x94); }  // -240 standing, -80 crouched
};

struct Camera {
    CameraObj obj;
    int32_t pos[3]{};  // the entity's +0x20C: the eye (16.16)

    // camera_think for the soldier at sol_pos with the view yaw (inv+0x184), as the soldier was at
    // the end of the last tick (the camera runs first).
    void think(const int32_t sol_pos[3], int32_t view_yaw);
    // camera_setup: the view matrix (obj +0x100) and translation (+0x114).
    void setup(const Gte& g);
    void view(int16_t m[3][3], int32_t t[3]) const;
    // view_pitch_target (0x8007BADC), from player_controls: the look point's height target.
    void pitch_target(const Gte& g, int32_t pitch);
};

}  // namespace valor
