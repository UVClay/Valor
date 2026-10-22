// The original's function pointers (an entity's think +0x36C, hit_part +0x374, world contact +0x378,
// touch +0x37C, the destructor by type) dispatched to the engine's systems (engine.py's think and
// handlers tables).
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"

namespace valor::sim {

bool run_think(Engine& eng, U fn, U e) {
    switch (fn) {
        case 0x8007A6B0: player_update(eng, e); return true;
        case 0x80079A50: player_think_dead(eng, e); return true;
        case 0x800473DC: camera_think(eng, e); return true;
        case 0x80053C00: return true;  // the world's colour cycles: drawing only
        case 0x8004F488: fade_think(eng, e); return true;
        case 0x80055628: enemy_think(eng, e); return true;
        case 0x80054AB8: dog_think(eng, e); return true;
        case PROJECTILE_THINK: projectile_think(eng, e); return true;
        case CASING_THINK: casing_think(eng, e); return true;
        case IMPACT_THINK: impact_effect_think(eng, e); return true;
        case PARTICLES_THINK: particles_think(eng, e); return true;
        case 0x80082E88: return true;  // the pickup's think is empty
        case LADDER_THINK: ladder_think(eng, e); return true;
        case SCRIPTED_THINK:
        case SWITCH_THINK: scripted_think(eng, e); return true;
        case SMOKE_THINK: return true;  // the emitter's particles: drawing only
        case FLASH_THINK: flash_think(eng, e); return true;
        case EXPLOSION_THINK: explosion_think(eng, e); return true;
        case BURST_THINK: burst_think(eng, e); return true;
        case ALARM_THINK: alarm_think(eng, e); return true;
        case MOUNTED_GUN_THINK: mounted_gun_think(eng, e); return true;
        default: return false;
    }
}

bool run_touch(Engine& eng, U fn, U e, U o, I& r) {
    switch (fn) {
        case 0x80082E90: r = health_touch(eng, e, o); return true;
        case 0x800830BC: r = ammo_touch(eng, e, o); return true;
        case 0x800832C0: r = item_touch(eng, e, o); return true;
        case 0x8005291C: r = enemy_touch(eng, e, o); return true;
        case 0x8007C030: r = player_touch(eng, e, o); return true;
        case BULLET_TOUCH: r = bullet_touch(eng, e, o); return true;
        case 0x8005D32C: r = destructible_touch(eng, e, o); return true;
        case MOUNTED_GUN_TOUCH: r = mounted_gun_touch(eng, e, o); return true;
        case 0x800476C4: r = sphere_vs_object_planes(eng, e, o) != 0; return true;  // the death camera's
        default: return false;
    }
}

bool run_hit_part(Engine& eng, U fn, U e, I p, U o, I op, I& r) {
    switch (fn) {
        case 0x80054C24: r = dog_hit_part(eng, e, p, o, op); return true;
        case 0x80055864: r = enemy_hit_part(eng, e, p, o, op); return true;
        case 0x8007C1D0: r = player_hit_part(eng, e, p, o, op); return true;
        case PROJECTILE_HIT_PART: r = projectile_hit_part(eng, e, p, o, op); return true;
        case GRENADE_HIT_PART: r = grenade_hit_part(eng, e, p, o, op); return true;
        case 0x8004C1B4: r = damage_from_hit(eng, e, p, o, op); return true;
        default: return false;
    }
}

bool run_world_contact(Engine& eng, U fn, U e, I res) {
    switch (fn) {
        case PROJECTILE_WORLD_CONTACT: projectile_world_contact(eng, e, res); return true;
        case 0x800476E8:  // the death camera's: landing stops it
            if (res & 1) {
                eng.m.w32(e + 0x200, 0);
                eng.m.w32(e + 0x38, eng.m.u32(e + 0x38) | 2);
            }
            return true;
        case 0x80056250:
        case 0x80054E14: walker_world_contact(eng, e, res); return true;
        default: return false;
    }
}

void run_destroy(Engine& eng, I type, U e) {
    if (type == SOLDIER)
        enemy_destroy(eng, e);
    else if (type == DOG)
        dog_destroy(eng, e);
    else if (type == 0x20)
        alarm_destroy(eng, e);
}

}  // namespace valor::sim
