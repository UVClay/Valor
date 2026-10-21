// The engine's systems, one file each as tools/sim/engine_*.py has them, and the dispatch of the
// original's function pointers (thinks, touch, hit-part, world-contact handlers, destructors) to
// them.
#pragma once

#include <string>
#include <vector>

#include "valor/sim/common.h"
#include "valor/sim/level.h"
#include "valor/sim/simgte.h"

namespace valor::sim {

class Engine;

// The function pointers' dispatch (engine.py's think and handlers tables). False: not implemented.
bool run_think(Engine& eng, U fn, U e);
bool run_touch(Engine& eng, U fn, U e, U other, I& result);
bool run_hit_part(Engine& eng, U fn, U e, I part, U other, I opart, I& result);
bool run_world_contact(Engine& eng, U fn, U e, I res);
void run_destroy(Engine& eng, I type, U e);
bool has_handler(const char* kind, U fn);

// engine_objects.py: spawning from records and the world objects.
void place(Engine& eng, U e, Record& rec);
U model_by_hash(Engine& eng, U h);
// weapon_draw (0x8007DD4C): the soldier's current slot's weapon in hand.
void player_weapon_draw(Engine& eng, U e);
// player_pad_config (0x8007A050): the controller roles by the layout option; the stance restored.
void player_pad_config(Engine& eng, U e, U inv, I port);
void player_stand_port(Engine& eng, U e, I port);
U record_spawn_entity(Engine& eng, Record& rec);  // 0 when refused (queued)
constexpr U G_LADDER_HELD = 0x8009A5B8;
constexpr U LADDER_THINK = 0x800611D0;
U ladder_spawn(Engine& eng, U pair, U r);
void ladder_think(Engine& eng, U e);
void ladder_release(Engine& eng);
void place_record(Mem& m, U e, U r);
U spawn_by_type(Engine& eng, I t, U model, Record& rec);
void cull(Engine& eng, U e, Record& rec);
U pickup_spawn_record(Engine& eng, Record& rec);
void level_explosion(Engine& eng, Ent* sub, I sub_obj, I kind);
void set_home(Engine& eng, const SVal& tgt, const SVal& src);

// engine_attach.py: a soldier's attachments, the hit reaction's puffs and drops, pickups.
struct PickupRec {
    I pos[3]{}, ang[3]{};
    I flags58 = 0, cull = 0, count = 0, flags30 = 0, w = -1;
    U addr = 0;
};
U attach_template(I k);
U attach_entry(Mem& m, U e, I k);
I instance_set(Engine& eng, U inst, U h, I flags);
void instance_release(Engine& eng, U inst);
I m_hash(Engine& eng, U B);
void knock_off(Engine& eng, U B, U player, I speed, I vy, I ticks, I drag, I gravity, I bounces, I spin, I action);
I fall_step(Engine& eng, U B, I feet);
void attachments_init(Engine& eng, U e, const std::vector<U>& hashes);
void attachment_draw(Engine& eng, U e, U B, I bone);
void attachments_update(Engine& eng, U e);
void attachments_op(Engine& eng, U e, I ops, I arg);
U drop_pickup(Engine& eng, U e, U pos, U ang, U model_hash, I w);
U pickup_spawn(Engine& eng, PickupRec& rec, U kind_hash, U model_hash, U owner);
void ranges_update(Engine& eng, U e);
void hit_reaction(Engine& eng, U e, I var, I part);
I puffs_update(Mem& m, U base);
I drops_update(Engine& eng, U e, U base);
I health_touch(Engine& eng, U p, U e);
I ammo_touch(Engine& eng, U p, U e);
I item_touch(Engine& eng, U p, U e);

// engine_planes.py: objects' plane boxes, the segment-box test, destructibles.
void box_sphere(Mem& m, U e, const I mn[3], const I mx[3]);
void object_box(Engine& eng, U e, const I mn[3], const I mx[3]);
// 0x8004B0F0: the segment (integer points) against the object's planes; point, normal, flags.
bool segment_box(Engine& eng, const I a[3], const I b[3], U o, I point[3], I normal[3], I& flags);
bool projectile_planes(Engine& eng, U e, U o, I point[3], I normal[3], I& flags);
I destructible_touch(Engine& eng, U e, U o);
U object_spawn(Engine& eng, Record& rec, U model);

// engine_move.py: entity_move by kind; returns the world-contact bits (1 landing, 2 slide).
I entity_move(Engine& eng, U e);
I walker_move(Engine& eng, U e);

// engine_player.py
void player_update(Engine& eng, U e);
void player_move(Engine& eng, U e);
void player_weapon_update(Engine& eng, U e);
void fade_think(Engine& eng, U e);

// engine_camera.py
void camera_think(Engine& eng, U e);
void camera_setup(Engine& eng, U e);
void view_pitch_target(Engine& eng, U inv, U co);

// engine_aim.py
void aim_update(Engine& eng, U e, U inv);

// engine_anim.py
void root_motion(Engine& eng, U e);
void entity_bounds(Engine& eng, U e);

// engine_draw.py
void draw_pass(Engine& eng);

// engine_enemy.py
void witness_check(Engine& eng);
void char_pool_update(Engine& eng);
void brain_update_entity(Engine& eng, U e);
void start_scripts(Engine& eng, U e);
U enemy_spawn(Engine& eng, Record& rec);
void enemy_think(Engine& eng, U e);
void dog_think(Engine& eng, U e);
void enemy_destroy(Engine& eng, U e);
void dog_destroy(Engine& eng, U e);

// engine_combat.py (the contact phase)
void entities_collide(Engine& eng);

// engine_projectile.py
constexpr U PROJECTILE_THINK = 0x80044470;
constexpr U PROJECTILE_HIT_PART = 0x80043E38;
constexpr U PROJECTILE_WORLD_CONTACT = 0x80043284;
constexpr U BULLET_TOUCH = 0x80043AD4;
constexpr U CASING_THINK = 0x80087FA0;
constexpr U IMPACT_THINK = 0x800701DC;
constexpr U PARTICLES_THINK = 0x80073B08;
void handle_store(Mem& m, U dst, U ent);
bool handle_same(Engine& eng, U ptr, U ent);
void fire_gun(Engine& eng, U e, U inv, I fire);
U bullet_spawn(Engine& eng, U owner, I bone_a, I bone_b, const V3* target, U attach, U off_a, U off_b, I dmg, I spread, I aimed,
               I etype, I fa, I fb, I mask);
void projectile_think(Engine& eng, U e);
I generic_move(Engine& eng, U e);
I projectile_world_contact(Engine& eng, U e, I res);
void impact_effect_think(Engine& eng, U e);
U particles_spawn(Engine& eng, const V3& pos, I spread, I n, I vel_xz, I vel_y, I col, I col_rand, I a7, I a8, I a9, I a10, I a11,
                  I a12, I smoke_on, I smoke_n);
void particles_think(Engine& eng, U e);
U casing_spawn(Engine& eng, U owner, const V3& pos, I pitch);
void casing_think(Engine& eng, U e);
I bullet_touch(Engine& eng, U e, U other);
I projectile_hit_part(Engine& eng, U e, I part, U other, I opart);
void rumble_set(Mem& m, I port, I a, I b, I c, I d);

// engine_sense.py
// model_bone_point (0x8008DABC): a bone's joint (+ its rotation times off) into the world.
V3 model_bone_point(Engine& eng, U e, I bone, const V3* off);
I angle_to(Engine& eng, U e, U target);

// engine_combat.py
I call_touch(Engine& eng, U fn, U e, U other);
I damage_from_hit(Engine& eng, U victim, I vpart, U attacker, I apart);
void apply_damage(Engine& eng, U victim, I dmg, U attacker);
I dog_hit_part(Engine& eng, U e, I p, U o, I op);
I enemy_hit_part(Engine& eng, U e, I p, U o, I op);
I player_hit_part(Engine& eng, U e, I p, U o, I op);
I enemy_touch(Engine& eng, U e, U o);
I walker_world_contact(Engine& eng, U e, I res);
I player_touch(Engine& eng, U e, U o);
void enemy_weapon(Engine& eng, U e);

}  // namespace valor::sim
