// The player's soldier: player_update (0x8007A6B0) and player_controls (0x8007F300) as they run, on
// top of entity_move (docs/re/movement.md; the order of a tick there). Input is the pad, through the
// control roles (controls.h). What other systems decide is taken as events until they are ported:
// the weapon in hand (weapons, scripts), the aim friction aim_update sets (combat), a hit's flinch
// and death (combat), touches by soldiers and objects, the low-ceiling zones (level triggers).
#pragma once

#include <cstdint>

#include "valor/aim.h"
#include "valor/camera.h"
#include "valor/controls.h"
#include "valor/level.h"
#include "valor/movement.h"
#include "valor/world.h"

namespace valor {

// The look/weapon block (soldier +0x388, "inv"), the fields movement and the view use.
struct Inv {
    uint32_t flags = 0;       // +0x0 (movement.md: 2 crouched, 0x20 aiming, 0x100 scope, ...)
    uint32_t prev_flags = 0;  // +0x4: last tick's
    int32_t eye_stand = 240 << 16;  // +0x34: the standing eye height
    int32_t eye_target = -240;      // +0x3C: the camera's eye offset target (whole units)
    int32_t kick = 0, climb = 0;    // +0x4C, +0x50: the recoil kick
    int8_t slot = 0;                // +0x54
    uint8_t weapon = 0;             // +0x55
    int32_t ladder = 0;             // +0x70: a ladder's top, set by its think this tick
    int32_t ladder_latch = 0;       // +0x74
    int32_t knock[3]{};             // +0x80: the flinch's push per tick
    int32_t was_on_ground = 0;      // +0x8C
    int32_t pin[3]{};               // +0x90: aim mode's spot
    int32_t lean = 0;               // +0x9C
    uint32_t zoom = 400, zoom_target = 400, zoom_step = 1;  // +0xA4, +0xA8, +0xAC
    int16_t box_x = 64, box_y = 30;  // +0xB4, +0xB6: the crosshair's box before the view turns
    uint16_t flinch = 0;             // +0x154: ticks left
    int32_t pitch = 0;               // +0x178 (16.16, 4096 per turn)
    int32_t cross_y = 0;             // +0x17C
    int32_t aim_pitch = 0;           // +0x180: the body's pitch past the view's
    int32_t yaw = 0;                 // +0x184: the view yaw movement uses
    int32_t turn_rate = 0;           // +0x188
    int32_t aim_yaw = 0;             // +0x18C
    int16_t cross_sx = 0x100, cross_sy = 0x83;  // +0x190, +0x192: the crosshair on screen
    int32_t shot_start[3]{};         // +0x198: where shots start (16.16)
    int32_t aim_point[3]{};          // +0x1A4: what the crosshair is on (16.16)
    uint32_t stance = 7, prev_stance = 7;       // +0x1C8, +0x1CC
};

struct PlayerEvents {
    // Taken from outside each tick (see the header comment).
    bool set_weapon = false;
    int8_t slot = 0;
    uint8_t weapon = 0;
    bool set_friction = false;  // aim_update's friction bits (inv 0x100000, bit 31), last tick's
    uint32_t friction = 0;
    bool set_roll = false;  // the body's roll (+0x230) as the move finds it
    int32_t roll = 0;
    bool set_anim_state = false;  // the scripts' animation state (+0x11E) and the weapon's timer
    int16_t anim_state = 0;       // (inv+0x48): player_update's inv 0x800000
    int32_t weapon_timer = 0;
    bool set_recoil = false;  // the kick (inv+0x4C) and its climb (+0x50) as firing left them
    int32_t kick = 0, climb = 0;
    bool flinch = false;  // player_hit_part started a flinch: 8 ticks of this knockback
    int32_t knock[3]{};
    // weapon_draw (0x8007DD4C) this tick, when the soldier's scripts ask for it (variable 0x11):
    // the slot's weapon in hand, aim mode and zoom dropped, the arms model swapped (the capsule's
    // height after it: 188 from the model's +0xA0, 80 if weapon_draw found him crouched).
    bool weapon_draw = false;
    int8_t draw_slot = 0;
    uint8_t draw_weapon = 0;
    int32_t draw_eye = 188 << 16;
    bool dies = false;  // contact 0x4000: health reached 0
    bool set_flags_b = false;  // the trigger boxes' 0x800 (low ceiling) and the rest
    uint32_t flags_b = 0;
    int32_t push[3]{};  // a touch handler's push left for the next move (+0x1F0)
    bool touch_cleared = false;  // ... which also cleared the speeds
    bool moved = false;  // an object's touch handler moved him after the move (objects.md, Blocking)
    int32_t pos[3]{};
    bool teleport = false;  // the level logic put him somewhere (position and +0x3BC both)
    int32_t teleport_pos[3]{};
};

// What aim_update reads besides the player: the view, the world, the entities, the arms.
struct AimContext {
    const Gte* gte = nullptr;
    const World* world = nullptr;
    Camera* camera = nullptr;
    const std::vector<AimTarget>* targets = nullptr;  // in the entity list's order
    AimArms arms;
    uint32_t self = 0;  // the soldier's handle
};

class Player {
public:
    AimContext aim;  // with aim.gte set, the player runs aim_update and drives the camera's points
    Controls controls;
    Inv inv;
    Walker w;
    int32_t eye = 240 << 16;   // +0x2D4: the capsule's height (16.16; w.eye is its whole part)
    int16_t sphere_y = -100;   // +0x2DA
    uint32_t flags_a = 0x20;   // +0xC (0x20 gravity)
    int32_t local_fwd = 0;     // +0x220
    int32_t ang_vel[3]{};      // +0x234
    int32_t body[3]{};         // +0x228: the body's angles (aim_update's in stances 7, 8)
    int16_t matrix[3][3]{};    // +0x340: RotMatrixYXZ of the body's angles, built at the end of a tick
    int32_t ipos[3]{};         // +0x354: the position's whole part, as the last move left it
    int32_t eye_offset = -240; // the camera's +0x94
    int mission = 0, level = 0;  // g_params (0x800939F4): 5:4 crouches lower
    bool riding = false;       // +0x2C: a parent (not ported)
    bool dead = false;
    uint32_t ticks = 0;

    // Places the soldier on the level's player start (on its floor), standing, default view.
    void spawn(const World& world, const LevelInfo& info);
    // Puts the soldier at a 16.16 position and view (a warp; no floor search).
    void place(const int32_t pos[3], int32_t yaw, int32_t pitch, bool ground);

    // player_update, then entity_move and the landing's world contact. The pad must be set in
    // `controls.pad` (and any host axis values) before.
    void tick(const World& world, const PlayerEvents& ev = {});
    void update(const PlayerEvents& ev);  // the think: player_update (0x8007A6B0)
    void move(const World& world);        // entity_move, player_world_contact

    int32_t yaw() const { return inv.yaw; }
    int32_t pitch() const { return inv.pitch; }
    bool crouched() const { return inv.flags & 2; }

private:
    void stand();
    void crouch_done();
    void set_eye_target(bool crouched);
    void player_controls();
    bool ladder_climb();
    int32_t scale_look(int32_t v) const;
    void aim_update();
    void bone_point(int which, int32_t out[3]) const;
    void center_view();
};

}  // namespace valor
