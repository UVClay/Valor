// A model instance's animation state and pose (docs/re/animation.md: The instance, Playback, The
// pose): the current animation and frame the scripts set, the cross-fade, pose_build's cache, the
// root offset and the joints. The scripts (enemies' VM) are not ported: a host or a test plays the
// animations through start() and advance().
#pragma once

#include <cstdint>
#include <vector>

#include "valor/model.h"

namespace valor {

struct AnimInstance {
    const Model* model = nullptr;
    int16_t anim = -1, frame = 0;     // +0x5C, +0x5E
    int16_t target = -1;              // the channel's frame target (B; -1 shows A one tick)
    int16_t blend = 0;                // +0x3E: 4 at a cross-fade's start, down to 0
    int16_t blend_frame = -1;         // +0x40
    int16_t first = 0;                // +0x60: the range's first frame, at least 3
    int16_t cache_anim = -1, cache_frame = -1;  // +0x62, +0x64
    bool posed = false;               // +0x88 set
    std::vector<Quat> pose, saved;    // +0x88's quaternions, +0x8C
    int16_t root[3]{}, saved_root[3]{};  // +0x98, +0xA0
    std::vector<std::array<int16_t, 3>> joints;  // +0x94
    int16_t nbones = 0;               // the posed frame's bone count

    // Op 00 (script_op_anim, 0x80057D9C): animation n, frames a..b (b -1: the last), mode 0
    // continue, 1 cross-fade from the pose on screen over 4 ticks, 2 cut.
    void start(int n, int a, int b, int mode);
    // script_run_a's frame advance, once per tick: false when the range is done.
    bool advance();
    // pose_build (0x80085648) and skeleton_pose: the pose and joints as a draw would build them.
    void build(const Models& models);
};

}  // namespace valor
