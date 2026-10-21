// A walker's move (entity_move 0x8006C83C, walker_slide 0x80078F60) as docs/re/movement.md and
// levels.md (Walkers) describe it for the player. Ported from tools/sim/move_sim.py.
#pragma once

#include <cstdint>
#include <vector>

#include "valor/fixed.h"
#include "valor/world.h"

namespace valor {

// What entity_move reads and writes of the soldier. All 16.16 unless noted.
struct Walker {
    int32_t pos[3]{};    // +0x20C feet
    int32_t old[3]{};    // +0x3BC: where the last move left it
    int32_t vy = 0;      // +0x21C vertical speed (y down)
    int32_t fwd = 0;     // +0x200 forward speed (local z)
    int32_t strafe = 0;  // +0x218 strafe speed (local x, right)
    int32_t yaw = 0;     // the view yaw inv+0x184, 16.16 with 4096 per turn
    int32_t roll = 0;    // the body's roll +0x230 (16.16): the move's matrix is (0, yaw, roll)
    int32_t push[3]{};   // +0x1F0: added to this move
    int32_t floor = 0;   // +0x208: the last floor found, 16.16
    int32_t ground = 0;  // +0x284
    int32_t radius = 100;  // +0x2EC whole units
    int32_t eye = 240;     // +0x2D6 whole units: the capsule's height
    uint32_t flags_b = 0x21000;
    bool gravity = true;  // flags A 0x20 (off on ladders and while aiming)
    bool walls = true;

    // Outputs of the last move.
    uint16_t zone = 0xFFFF;
    int32_t floor_found = kNoFloor;
    uint32_t contact = 0;
    uint32_t events = 0;  // MoveEvent bits
    int wall_pushes = 0;
};

enum MoveEvent : uint32_t {
    kEvLanding = 1,
    kEvNoFloor = 2,
    kEvStepDown = 4,
    kEvStepUp = 8,
    kEvStillInWall = 16,
    kEvWalkedOff = 32,
    kEvWallPush = 64,
};

constexpr int32_t kGravity = 0x20000;       // 2.0 per tick
constexpr int32_t kTerminalVy = 0x4B0000;   // 75.0
constexpr int32_t kJumpVy = -0x1A0000;      // -26.0

// One tick's move of a walker: gravity, the move turned by the yaw, the walls, the floor.
void entity_move(const World& world, Walker& w);

}  // namespace valor
