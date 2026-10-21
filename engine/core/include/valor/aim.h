// aim_update (0x8007E034; docs/re/combat.md, Aiming): the crosshair's ray from the camera, swept
// against the world in three pieces and shortened to the nearest entity it passes (with aim
// friction past 1200 and 2750 units on characters); the aim point inv+0x1A4, where shots start
// (inv+0x198), and the soldier's angular velocity steering the gun toward the crosshair. Ported
// from tools/sim/engine_aim.py.
#pragma once

#include <cstdint>
#include <vector>

#include "valor/camera.h"
#include "valor/gte.h"
#include "valor/model.h"
#include "valor/world.h"

namespace valor {

struct AimPart {
    int32_t c[3];  // the hit part's world centre (+0x20)
    int32_t r2;    // +0x38: radius squared
    int32_t kind;  // +0x8 (7: never hit)
};

// An entity as the aim sees it: its collision sphere and, for characters, its hit parts.
struct AimTarget {
    uint32_t handle = 0, flags_a = 0, flags_b = 0;
    int32_t pos[3]{};      // +0x20C
    int16_t centre[3]{};   // +0x2E0
    uint16_t radius = 0;   // +0x2E8
    uint16_t box = 0;      // +0x2EE (an object's box: not ported)
    std::vector<AimPart> parts;
};

// The first-person model's gun bones as the last draw left them (model +0xD4, +0xD6).
struct AimArms {
    const Model* model = nullptr;
    int16_t joint[2][3]{};
    Quat quat[2]{};
};

class Player;

// The ray's segment tests (combat_sim.py): segment_sphere (0x8004CBC8), the line within the radius,
// and segment_parts (0x8004C76C), the lowest-numbered part the path crosses.
bool segment_sphere(const int32_t start[3], const int32_t u[3], const int16_t centre[3], int32_t r);
int segment_parts(const int32_t start[3], const int32_t u[3], int32_t len2, const std::vector<AimPart>& parts);
int32_t dist_cheap(int64_t a, int64_t b, int64_t c);  // max + (the other two) / 4

}  // namespace valor
