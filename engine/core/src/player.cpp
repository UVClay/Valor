#include "valor/player.h"

#include <algorithm>

#include "valor/fixed.h"

namespace valor {

void Player::spawn(const World& world, const LevelInfo& info) {
    w_ = Walker{};
    for (int i = 0; i < 3; i++) w_.pos[i] = s32(int64_t(info.start.pos[i]) << 16);
    w_.yaw = s32(int64_t(info.start.yaw) << 16);
    // Settle on the floor under the start (the original falls onto it in its first ticks).
    Floor f = world.floor_find(w_.pos);
    if (f.height != kNoFloor) w_.pos[1] = s32(int64_t(f.height) << 16), w_.ground = 1, w_.floor = w_.pos[1];
    std::copy(w_.pos, w_.pos + 3, w_.old);
    std::copy(w_.pos, w_.pos + 3, spawn_pos_);
    spawn_yaw_ = w_.yaw;
    pitch_ = 0;
    eye_offset_ = eye_target_ = -240;
    crouched_ = crouch_done_ = false;
    ticks_ = 0;
    for (int i = 0; i < 3; i++) cam_feet_[i] = w_.pos[i];
    cam_eye_ = eye_offset_, cam_yaw_ = w_.yaw, cam_pitch_ = pitch_;
}

void Player::place(const int32_t pos[3], int32_t yaw, int32_t pitch, bool ground) {
    std::copy(pos, pos + 3, w_.pos);
    std::copy(pos, pos + 3, w_.old);
    w_.yaw = yaw;
    w_.vy = 0;
    w_.ground = ground;
    w_.floor = pos[1];
    pitch_ = pitch;
    std::copy(pos, pos + 3, cam_feet_);
    cam_yaw_ = yaw, cam_pitch_ = pitch;
}

void Player::tick(const World& world, const PlayerInput& in) {
    // The camera entity runs first: it shows the soldier as the last tick left him.
    std::copy(w_.pos, w_.pos + 3, cam_feet_);
    cam_eye_ = eye_offset_, cam_yaw_ = w_.yaw, cam_pitch_ = pitch_;

    // 2. Below y = +5000: "Player has fallen too far" (the original kills him; back to the start).
    if ((w_.pos[1] >> 16) > 5000) {
        std::copy(spawn_pos_, spawn_pos_ + 3, w_.pos);
        std::copy(spawn_pos_, spawn_pos_ + 3, w_.old);
        w_.vy = 0, w_.ground = 1;
    }
    // 9. Jump: pressed, on the ground, not crouched.
    if (in.jump && w_.ground && !crouched_) w_.ground = 0, w_.vy = kJumpVy;
    // 10. Crouch toggle and the eye easing (20 per tick toward the target).
    if (in.crouch) {
        if (!crouched_) {
            crouched_ = true;
            eye_target_ = -80;
        } else if (!(w_.flags_b & 0x800)) {  // a low-ceiling zone keeps him down
            crouched_ = crouch_done_ = false;
            eye_target_ = -240;
            w_.eye = 240;
        }
    }
    if (eye_offset_ != eye_target_) {
        eye_offset_ += eye_offset_ < eye_target_ ? std::min(20, eye_target_ - eye_offset_)
                                                 : -std::min(20, eye_offset_ - eye_target_);
        if (crouched_ && eye_offset_ == eye_target_) {
            crouch_done_ = true;  // player_crouch_done: speeds quartered, capsule 80
            w_.eye = 80;
        }
    }
    // 12. player_controls: velocity from the roles, the yaw and pitch.
    int32_t fwd = in.forward, strafe = in.strafe;
    if (crouch_done_) fwd >>= 2, strafe >>= 2;
    w_.fwd = fwd;
    w_.strafe = strafe;
    w_.yaw = s32(int64_t(w_.yaw) + in.turn);
    pitch_ = std::clamp<int32_t>(s32(int64_t(pitch_) - in.pitch / 2), -682 * kOne, 682 * kOne);
    // 13. Gravity on (no ladders yet).
    w_.gravity = true;
    // 21. entity_move.
    entity_move(world, w_);
    ticks_++;
}

}  // namespace valor
