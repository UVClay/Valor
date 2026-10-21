// The player's soldier for play in a host engine: the movement half of player_update and
// player_controls (docs/re/movement.md) on top of entity_move. What it doesn't do yet: weapons,
// aim mode, ladders, the flinch, Use, touches, death, streaming chunk slots (all chunks are loaded).
#pragma once

#include <cstdint>

#include "valor/level.h"
#include "valor/movement.h"
#include "valor/world.h"

namespace valor {

// One tick's control roles (movement.md, Control roles), already in the game's units.
struct PlayerInput {
    int32_t forward = 0;  // 16.16 forward speed request: +34.863 (0x22DD00) full forward
    int32_t strafe = 0;   // 16.16, positive right
    int32_t turn = 0;     // 16.16 yaw units added this tick (4096 per turn)
    int32_t pitch = 0;    // 16.16 pitch role: negative looks up (pitch -= role / 2)
    bool jump = false;    // pressed this tick
    bool crouch = false;  // pressed this tick (a toggle)
};

constexpr int32_t kRunSpeed = 0x22DD00;  // 34.863: a full stick or the keyboard

class Player {
public:
    // Places the soldier on the level's player start (on its floor).
    void spawn(const World& world, const LevelInfo& info);

    // Puts the soldier at a 16.16 position and view (a warp; no floor search).
    void place(const int32_t pos[3], int32_t yaw, int32_t pitch, bool ground);

    // One 30 Hz tick.
    void tick(const World& world, const PlayerInput& in);

    const Walker& walker() const { return w_; }
    int32_t yaw() const { return w_.yaw; }       // 16.16, 4096 per turn
    int32_t pitch() const { return pitch_; }     // 16.16, clamped to +-682
    int32_t eye_offset() const { return eye_offset_; }  // camera_obj+0x94: -240 standing, -80 crouched
    bool crouched() const { return crouched_; }
    uint32_t ticks() const { return ticks_; }

    // The camera: the soldier's state at the start of this tick (the camera entity runs first, so
    // it shows the end of the tick before). Whole-unit feet position and eye offset.
    const int32_t* camera_feet() const { return cam_feet_; }
    int32_t camera_eye_offset() const { return cam_eye_; }
    int32_t camera_yaw() const { return cam_yaw_; }
    int32_t camera_pitch() const { return cam_pitch_; }

private:
    Walker w_;
    int32_t pitch_ = 0;
    int32_t eye_offset_ = -240, eye_target_ = -240;
    bool crouched_ = false, crouch_done_ = false;
    int32_t spawn_pos_[3]{}, spawn_yaw_ = 0;
    uint32_t ticks_ = 0;
    int32_t cam_feet_[3]{}, cam_eye_ = -240, cam_yaw_ = 0, cam_pitch_ = 0;
};

}  // namespace valor
