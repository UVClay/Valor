#include "valor/player.h"

#include <algorithm>

#include "valor/fixed.h"

namespace valor {

namespace {
constexpr int32_t kPitchLimit = 0x02AA0000;  // 682.0: 59.9 degrees
}

void Player::spawn(const World& world, const LevelInfo& info) {
    Controls keep = controls;
    *this = Player{};
    controls = keep;
    for (int i = 0; i < 3; i++) w.pos[i] = s32(int64_t(info.start.pos[i]) << 16);
    inv.yaw = s32(int64_t(info.start.yaw) << 16);
    body[1] = inv.yaw;
    // Settle on the floor under the start (the original falls onto it in its first ticks).
    Floor f = world.floor_find(w.pos);
    if (f.height != kNoFloor) w.pos[1] = s32(int64_t(f.height) << 16), w.ground = 1, w.floor = w.pos[1];
    std::copy(w.pos, w.pos + 3, w.old);
}

void Player::place(const int32_t pos[3], int32_t yaw, int32_t pitch, bool ground) {
    std::copy(pos, pos + 3, w.pos);
    std::copy(pos, pos + 3, w.old);
    inv.yaw = yaw, inv.pitch = pitch;
    w.vy = 0;
    w.ground = ground;
    w.floor = pos[1];
}

// The eye target and the drawing distances (crouch_target / stand_target in player_update,
// player_stand and player_crouch_done). The level 5:4 crouches lower (-150), so the drawing
// distances aren't touched there.
void Player::set_eye_target(bool crouched) {
    inv.eye_target = crouched ? -80 : -240;
}

// player_stand (0x80079718).
void Player::stand() {
    inv.flags &= ~2u & ~0x10u;
    eye = inv.eye_stand;
    sphere_y = -100;
    controls.role_axis_set_speed(kRoleForward, 0, 2);
    controls.role_axis_set_speed(kRoleStrafe, 0, 2);
    controls.role_button_set_speed(kRoleForward, 0, 2);
    controls.role_button_set_speed(kRoleStrafe, 0, 2);
    set_eye_target(inv.flags & 2);
}

// player_crouch_done (0x800798AC): the roles' speeds quartered, the capsule 80.
void Player::crouch_done() {
    inv.flags = (inv.flags | 2) & ~0x10u;
    if (!riding) eye = 80 << 16, sphere_y = 0;
    controls.role_axis_set_speed(kRoleForward, 2, 4);
    controls.role_axis_set_speed(kRoleStrafe, 2, 4);
    controls.role_button_set_speed(kRoleForward, 2, 4);
    controls.role_button_set_speed(kRoleStrafe, 2, 4);
    set_eye_target(inv.flags & 2);
}

// ladder_climb (0x8007F1E0): vertical speed half the forward role (up), or down to the floor.
bool Player::ladder_climb() {
    w.vy = 0;
    w.fwd = 0;
    flags_a &= ~0x20u;
    int32_t v = controls.role_button(kRoleForward) >> 1;
    if (v == 0 && controls.pad.analog) {
        int32_t a = controls.role_axis(kRoleForward);
        controls.calls.push_back({kRoleForward, 1, a});
        v = a >> 1;
    }
    if (v < 0) {
        w.ground = 0;
        w.vy = v;
        inv.flags |= 4;
        return true;
    }
    if (v > 0 && w.pos[1] < w.floor) {
        w.vy = std::min(v, s32(int64_t(w.floor) - w.pos[1]));
        inv.flags |= 8;
        return true;
    }
    w.vy = 0;
    return false;
}

// The look input's scaling: the scope / 8; in the aiming stances 0, 1 a slower turn (the option
// 0x80000 slower still); the aim friction / 2 (bit 31: / 4).
int32_t Player::scale_look(int32_t v) const {
    uint32_t f = inv.flags;
    if (f & 0x100) return v >> 3;
    if (inv.stance < 2) {
        v = (f & 0x80000) ? (v >> 1) - (v >> 3) : (v >> 1) + (v >> 4);
        if (f & 0x100000) v = int32_t(f) < 0 ? v >> 2 : v >> 1;
        return v;
    }
    if (f & 0x100000) return int32_t(f) < 0 ? v >> 2 : v >> 1;
    return v;
}

// center_view (0x8007F084): the crosshair back toward the centre, 3/4 per tick.
void Player::center_view() {
    int32_t a = (int64_t(inv.turn_rate) * 3) >> 2;
    int32_t s = s32((int64_t(a) << 8) / controls.role_button_max(kRoleTurn));
    inv.turn_rate = a;
    int16_t sx = int16_t(s + 0x100);
    if (inv.box_x < iabs(int16_t(s))) sx = inv.turn_rate <= 0 ? int16_t(0x100 - inv.box_x) : int16_t(inv.box_x + 0x100);
    inv.cross_sx = sx;
    int32_t b = (int64_t(inv.cross_y) * 3) >> 2;
    int32_t t = s32((int64_t(b) * 0x78) / controls.role_button_max(kRolePitch));
    inv.cross_y = b;
    int16_t sy = int16_t(t + 0x83);
    if (inv.box_y < iabs(int16_t(t))) sy = inv.cross_y <= 0 ? int16_t(0x83 - inv.box_y) : int16_t(inv.box_y + 0x83);
    inv.cross_sy = sy;
}

// player_controls (0x8007F300).
void Player::player_controls() {
    Controls& c = controls;
    uint32_t f0 = inv.flags;
    inv.flags &= ~0xCu;
    if (!(f0 & 0x20)) {
        if (inv.prev_flags & 0x20) {  // leaving aim mode: back to the pinned spot
            std::copy(inv.pin, inv.pin + 3, w.pos);
            inv.lean = 0;
            flags_a |= 0x20;
            w.flags_b &= ~0x10u;
        }
        bool climbed = inv.ladder ? ladder_climb() : false;
        if (!climbed) w.fwd = s32(-int64_t(c.role(kRoleForward)));
        w.strafe = c.role(kRoleStrafe);
    } else {
        if (!(inv.prev_flags & 0x20)) {  // aim mode: pinned where it started
            std::copy(w.pos, w.pos + 3, inv.pin);
            flags_a &= ~0x20u;
            w.flags_b |= 0x10;
        }
        w.fwd = 0;
        int32_t r = c.role(kRoleStrafe);
        w.push[0] = s32(int64_t(w.push[0]) + inv.pin[0] - w.pos[0]);
        w.push[1] = 0;
        w.pos[1] = inv.pin[1];
        w.push[2] = s32(int64_t(w.push[2]) + inv.pin[2] - w.pos[2]);
        int32_t v = s32(int64_t(s32(int64_t(r) * 2)) + int64_t(inv.lean) * 3) >> 2;
        w.strafe = v;
        inv.lean = v;
    }
    uint32_t s = inv.stance;
    if (s < 2) {
        // Aiming a gun: the look roles move the crosshair, the view turns past its box.
        if (s != inv.prev_stance) {
            inv.turn_rate = 0;
            inv.flags &= 0xFFEFFFFFu & 0x7FFFFFFFu;
            ang_vel[1] = 0;
            inv.cross_y = 0;
            ang_vel[0] = 0;
        }
        if (c.action_held(kCenter)) {
            center_view();
        } else {
            int32_t m3 = c.role_button_max(kRoleTurn);
            int32_t t = scale_look(c.role(kRoleTurn));
            int32_t r = s32(int64_t(inv.turn_rate) + (t >> 3));
            int32_t q = s32((int64_t(r) << 8) / m3);
            inv.turn_rate = r;
            if (inv.box_x >= iabs(int16_t(q))) {
                inv.cross_sx = int16_t(q + 0x100);
            } else {
                int32_t v;
                if (inv.turn_rate > 0) {
                    inv.cross_sx = int16_t(inv.box_x + 0x100);
                    v = s32((int64_t(inv.box_x) * m3) / 256);
                } else {
                    inv.cross_sx = int16_t(0x100 - inv.box_x);
                    v = s32(-(int64_t(inv.box_x) * m3) / 256);
                }
                inv.turn_rate = v;
                inv.yaw = s32(int64_t(inv.yaw) + t);
                body[1] = s32(int64_t(body[1]) + t);
            }
            int32_t m2 = c.role_button_max(kRolePitch);
            int32_t p = c.role_button(kRolePitch);
            if (p == 0 && c.pad.analog) {
                p = c.role_axis(kRolePitch);
                c.calls.push_back({kRolePitch, 1, p});
                if (p == 0) p = c.role_axis_alt(kRoleForward, kRolePitch);
            }
            p = (inv.flags & 0x80000) ? (p >> 1) - (p >> 3) : (p >> 1) + (p >> 4);
            if (inv.flags & 0x100000) p = int32_t(inv.flags) < 0 ? p >> 2 : p >> 1;
            int32_t cy = s32(int64_t(inv.cross_y) + (p >> 3));
            inv.cross_y = cy;
            int32_t q2 = s32((int64_t(cy) * 0x78) / m2);
            p >>= 1;
            if (inv.box_y >= iabs(int16_t(q2))) {
                inv.cross_sy = int16_t(q2 + 0x83);
            } else {
                int64_t v;
                if (inv.cross_y > 0) {
                    v = int64_t(inv.box_y) * m2;
                    inv.cross_sy = int16_t(inv.box_y + 0x83);
                } else {
                    v = -(int64_t(inv.box_y) * m2);
                    inv.cross_sy = int16_t(0x83 - inv.box_y);
                }
                inv.cross_y = s32(v / 120);
                inv.pitch = s32(int64_t(inv.pitch) - p);
                body[0] = s32(int64_t(body[0]) - p);
                if (inv.pitch > kPitchLimit) {
                    body[0] = s32(int64_t(body[0]) + kPitchLimit - inv.pitch);
                    inv.pitch = kPitchLimit;
                }
                if (inv.pitch < -kPitchLimit) {
                    body[0] = s32(int64_t(body[0]) - kPitchLimit - inv.pitch);
                    inv.pitch = -kPitchLimit;
                }
            }
        }
        // aim_update (combat.md) follows: not ported; its friction comes in as an event.
        inv.prev_stance = s;
        return;
    }
    if (s != inv.prev_stance) {
        inv.turn_rate = 0;
        inv.flags &= 0xFFEFFFFFu & 0x7FFFFFFFu;
        ang_vel[1] = 0;
        inv.cross_y = 0;
        ang_vel[0] = 0;
        if (s == 5) inv.cross_sx = 0x10E, inv.cross_sy = 0x73;
        else inv.cross_sx = 0x100, inv.cross_sy = 0x83;
    }
    int32_t t = scale_look(c.role(kRoleTurn));
    if (uint32_t(t + 0x10000) > 0x20000u)
        inv.turn_rate = s32(int64_t(inv.turn_rate) * 3 + t) >> 2;
    else
        inv.turn_rate = s32(int64_t(inv.turn_rate) + 1) >> 4;
    inv.yaw = s32(int64_t(inv.yaw) + t);
    // The pitch role, with the forward stick when the pitch stick is idle (role_axis_alt).
    auto pitch_role = [&](bool alt_needs_aim) {
        int32_t p = c.role_button(kRolePitch);
        if (p == 0 && c.pad.analog) {
            p = c.role_axis(kRolePitch);
            c.calls.push_back({kRolePitch, 1, p});
            if (p == 0 && (!alt_needs_aim || (inv.flags & 0x20))) p = c.role_axis_alt(kRoleForward, kRolePitch);
        }
        return p;
    };
    auto free_pitch = [&](int32_t p) {
        p = scale_look(p);
        if (p != 0) inv.pitch = std::clamp(s32(int64_t(inv.pitch) - (p >> 1)), -kPitchLimit, kPitchLimit);
    };
    auto centre_pitch = [&] {
        if (iabs(inv.pitch >> 2) <= 0x27FFFF)
            inv.pitch = s32(int64_t(inv.pitch) * 3 + 1) >> 2;
        else
            inv.pitch = s32(int64_t(inv.pitch) * 7 + 1) >> 3;
    };
    switch (c.cfg.look_mode) {
    case 0:  // the look spring; L3 freezes it
        if (c.action_pressed(kCenter)) inv.flags ^= 0x10;
        if (!(inv.flags & 0x10)) inv.pitch = s32(int64_t(inv.pitch) * 3 - pitch_role(true)) >> 2;
        break;
    case 1:  // free look; centre view while held
        if (c.action_held(kCenter))
            centre_pitch();
        else
            free_pitch(pitch_role(true));
        break;
    case 2:  // free while aiming, else back to level
        if (c.action_pressed(kCenter)) inv.flags ^= 0x10;
        if (!(inv.flags & 0x10)) {
            if (inv.flags & 0x20)
                free_pitch(pitch_role(false));
            else
                centre_pitch();
        }
        break;
    }
    if (s == 7 || s == 8) {
        // aim_update (combat.md): the body trails the view; not ported.
    } else {
        body[0] = inv.pitch;
        body[1] = inv.yaw;
    }
    inv.prev_stance = s;
}

void Player::tick(const World& world, const PlayerEvents& ev) {
    Controls& c = controls;
    c.calls.clear();
    // Events from systems not ported yet, as they stand at the start of this tick.
    if (ev.set_weapon) inv.slot = ev.slot, inv.weapon = ev.weapon;
    if (ev.set_friction) inv.flags = (inv.flags & ~0x80100000u) | (ev.friction & 0x80100000u);
    if (ev.set_flags_b) w.flags_b = ev.flags_b;
    if (ev.flinch) {
        inv.flinch = 8;
        std::copy(ev.knock, ev.knock + 3, inv.knock);
    }
    for (int i = 0; i < 3; i++) w.push[i] = s32(int64_t(w.push[i]) + ev.push[i]);
    if (ev.touch_cleared) w.fwd = 0, w.strafe = 0, local_fwd = 0;
    if (ev.moved) std::copy(ev.pos, ev.pos + 3, w.pos);
    ticks++;
    if (dead) return;  // player_think_dead: the death camera (not ported) carries him

    // player_update (0x8007A6B0).
    if (w.pos[1] > 0x13880000 || ev.dies) {  // fell out of the world (10000 damage) or killed
        // player_death_start (0x80079D10): the death camera takes over.
        dead = true;
        inv.flags &= ~0x20u & ~0x40u & ~0x100u & 0xFFDFFFFFu & ~0x4000u;
        flags_a |= 0x20;
        w.flags_b &= ~0x10u;
        return;
    }
    if (w.ground && c.action_held(kAim)) {
        inv.flags |= 0x20;
        if (inv.weapon == 5) {
            inv.flags = (inv.flags & 0xFFDFFFFFu) | 0x140;
            inv.zoom_target = 0x98F;
        }
        // (with the option 0x400000 and a weapon whose g_weapons +0x28 is set: mode 0x200000,
        // zoom 475; the weapon table isn't loaded yet)
    } else {
        inv.flags &= ~0x20u & ~0x40u & 0xFFDFFFFFu;
    }
    // player_stance (0x8007BA20).
    static const uint8_t aiming[13] = {0, 1, 5, 6, 0, 2, 1, 6, 0, 0, 6, 1, 0};
    static const uint8_t walking[13] = {7, 8, 9, 9, 7, 7, 8, 9, 7, 7, 9, 8, 7};
    inv.stance = inv.weapon < 13 ? ((inv.flags & 0x20) ? aiming[inv.weapon] : walking[inv.weapon]) : 7;
    // Use (zones: Session), reload, the trigger: weapons, not ported.
    // The recoil kick.
    int32_t k = std::min(s32(int64_t(inv.kick) + inv.climb), 0xF0000);
    inv.kick = k > 0 ? k - 0x7FFF : (k < -0x2000 ? k + 0x1FFF : 0);
    inv.climb = inv.climb >= 0x2001 ? inv.climb - 0x2000 : 0;
    inv.flags &= 0xFFFBFFFFu;
    // Jump: pressed, on the ground, not aiming, not crouched.
    if (!(inv.flags & 0x20) && w.ground && c.action_pressed(kJump) && !(inv.flags & 2)) {
        w.ground = 0;
        w.vy = (inv.flags & 0x10000000) ? -0x68000 : kJumpVy;
    }
    // Crouch: a toggle; the camera's eye offset eases 20 per tick toward the target.
    if (c.action_pressed(kCrouch)) {
        if (!(inv.flags & 2)) {
            inv.flags |= 2;
            set_eye_target(true);
        } else if (!(w.flags_b & 0x800)) {
            stand();
        }
    }
    if (eye_offset != inv.eye_target) {
        int32_t cur = eye_offset, tgt = inv.eye_target, step;
        if (tgt + 0x12 < cur) {
            step = -0x14;
        } else {
            step = 0x14;
            if (cur >= tgt - 0x12) {
                // The quirk for targets not a multiple of 20 away (movement.md, Crouching).
                if ((inv.flags & 2) && (w.fwd || w.strafe || ang_vel[1]))
                    step = cur - tgt;
                else
                    step = tgt - cur;
            }
        }
        eye_offset = cur + step;
        if (eye_offset == tgt && (inv.flags & 2)) crouch_done();
    } else if ((inv.flags & 2) && !riding) {
        eye = 80 << 16;
        sphere_y = 0;
    }
    if (inv.ladder && (inv.flags & 2)) stand();  // a ladder stands him up
    player_controls();
    if (inv.ladder == 0) flags_a |= 0x20;
    // The hit flinch: the knockback, less a quarter of the velocity's local components taken as
    // world ones; crouching cancels it.
    if (inv.flinch) {
        if (inv.flags & 2) {
            inv.flinch = 0;
        } else {
            int32_t vel[3] = {w.strafe, w.vy, local_fwd};
            for (int i = 0; i < 3; i++) w.push[i] = s32(int64_t(w.push[i]) - (vel[i] >> 2) + inv.knock[i]);
            inv.flinch--;
        }
    }
    // Zoom.
    if (inv.flags & 0x200040) {
        if (inv.zoom < inv.zoom_target) {
            if (inv.zoom_step < 0x40) inv.zoom_step += 2;
            inv.zoom += inv.zoom_step;
        }
    } else {
        inv.zoom = 400;
        inv.flags &= ~0x100u;
    }
    if (inv.zoom == 400) inv.zoom_step = 1;
    // The soldier's scripts run here: weapon_draw swaps the arms model, whose +0xA0 is the
    // capsule's height (188).
    if (ev.eye_swap) eye = ev.eye_swap_value;
    // The ladder latch: leaving a ladder zeroes the vertical speed.
    inv.prev_flags = inv.flags;
    if (inv.ladder) {
        inv.ladder_latch = inv.ladder;
        inv.ladder = 0;
    } else if (inv.ladder_latch) {
        w.vy = 0;
        inv.ladder_latch = 0;
    }
    inv.was_on_ground = w.ground;
    inv.flags &= ~0xE00u;

    // entity_move, then player_world_contact.
    w.yaw = inv.yaw;
    w.eye = eye >> 16;
    w.gravity = flags_a & 0x20;
    local_fwd = w.fwd;
    entity_move(world, w);
    std::fill(w.push, w.push + 3, 0);
    if (w.contact & 2) w.strafe = 0, local_fwd = 0;  // walls gathered: the sideways speed cleared
    if (w.events & kEvLanding) w.fwd = w.strafe = w.vy = local_fwd = 0;
}

}  // namespace valor
