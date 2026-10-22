// The accessor layer: typed, read-only views of the engine's state for a host (the Godot
// extension, tools), so nothing outside sim/ reads offsets. The engine keeps the original's memory
// layout (README, "Why it keeps the original's memory layout"); these views name its fields.
//
// One table per structure lists the fields (name, type, offset, what it is). The accessors are
// generated from it, so the names have one source: docs/re/entities.md (the entity), soldier.md
// (the soldier's object), movement.md (the player's object), animation.md (the instance).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "valor/sim/common.h"

namespace valor::sim::view {

// Field reads by type.
template <typename T>
T get(const Mem& m, U a);
template <>
inline int32_t get<int32_t>(const Mem& m, U a) { return int32_t(m.s32(a)); }
template <>
inline uint32_t get<uint32_t>(const Mem& m, U a) { return uint32_t(m.u32(a)); }
template <>
inline int16_t get<int16_t>(const Mem& m, U a) { return int16_t(m.s16(a)); }
template <>
inline uint16_t get<uint16_t>(const Mem& m, U a) { return uint16_t(m.u16(a)); }
template <>
inline int8_t get<int8_t>(const Mem& m, U a) { return int8_t(m.s8(a)); }
template <>
inline uint8_t get<uint8_t>(const Mem& m, U a) { return uint8_t(m.u8(a)); }

#define VALOR_VIEW_FIELD(name, type, off, doc) \
    type name() const { return get<type>(*m, a + (off)); }

// The entity (entities.md, The entity): every actor, object, projectile and effect.
#define VALOR_ENTITY_FIELDS(X)                                                            \
    X(handle, uint32_t, 0x000, "serial << 16 | slot")                                     \
    X(next_addr, uint32_t, 0x004, "the next entity in the list")                          \
    X(type, int32_t, 0x008, "1 player, 2 camera, 3 world, 0x1402 soldier, 0x1405 dog...") \
    X(flags_a, uint32_t, 0x00C, "0x80000 removed, 0x20000000 dead, 1 player...")          \
    X(flags_b, uint32_t, 0x010, "")                                                       \
    X(collide_mask, uint32_t, 0x014, "the flags A it collides with")                      \
    X(record_pair, uint32_t, 0x01C, "its record's table pair (0: none)")                  \
    X(group, int8_t, 0x035, "its group (soldiers, objects)")                              \
    X(parts_n, uint16_t, 0x036, "hit parts")                                              \
    X(max_health, int32_t, 0x0F0, "16.16")                                                \
    X(health, int32_t, 0x0F4, "16.16; <= 0 dead")                                         \
    X(instance_addr, uint32_t, 0x100, "the model instance (0: none)")                     \
    X(speed, int32_t, 0x200, "forward, 16.16 per tick")                                   \
    X(x, int32_t, 0x20C, "position, 16.16 units, y down")                                 \
    X(y, int32_t, 0x210, "")                                                              \
    X(z, int32_t, 0x214, "")                                                              \
    X(pitch, int32_t, 0x228, "angles, 4096 << 16 per turn")                               \
    X(yaw, int32_t, 0x22C, "")                                                            \
    X(roll, int32_t, 0x230, "")                                                           \
    X(on_ground, int32_t, 0x284, "")                                                      \
    X(eye_height, int32_t, 0x2D4, "16.16, up from the feet")                              \
    X(radius, uint16_t, 0x2E8, "the collision sphere's radius")                           \
    X(object_addr, uint32_t, 0x388, "the type's object (the soldier's, the player's...)") \
    X(brain_addr, uint32_t, 0x38C, "a character's brain")

// The model instance (animation.md, The instance).
#define VALOR_INSTANCE_FIELDS(X)                                     \
    X(pose_buffer, uint32_t, 0x000, "")                              \
    X(flags, uint32_t, 0x004, "")                                    \
    X(model_addr, uint32_t, 0x008, "the BSD model (its +0 the hash)") \
    X(anim, int16_t, 0x05C, "the animation shown")                   \
    X(frame, int16_t, 0x05E, "its frame")                            \
    X(bones, uint16_t, 0x072, "the posed skeleton's bones")          \
    X(posed_addr, uint32_t, 0x088, "this tick's quaternions")        \
    X(saved_addr, uint32_t, 0x08C, "the last pose's quaternions")    \
    X(joints_addr, uint32_t, 0x094, "the joints, 8 bytes each")

// The player's object (movement.md, soldier.md: the inventory and the view).
#define VALOR_PLAYER_FIELDS(X)                               \
    X(state, uint32_t, 0x000, "2 crouched, 0x20 aiming...") \
    X(slot, int8_t, 0x054, "the weapon slot in hand")         \
    X(weapon, uint8_t, 0x055, "its weapon (g_weapons index)") \
    X(look_pitch, int32_t, 0x178, "4096 << 16 per turn")     \
    X(look_yaw, int32_t, 0x184, "")

// A view of one entity.
struct Entity {
    const Mem* m = nullptr;
    U a = 0;
    VALOR_ENTITY_FIELDS(VALOR_VIEW_FIELD)

    bool removed() const { return flags_a() & 0x80000; }
    bool dead() const { return (flags_a() & 0x20000000) || health() <= 0; }
    bool character() const { return type() == 0x1402 || type() == 0x1405 || type() == 1; }
    // Whole units and turns' 4096ths, as the renderer wants them.
    std::array<double, 3> position() const { return {x() / 65536.0, y() / 65536.0, z() / 65536.0}; }
    std::array<int16_t, 3> angles() const { return {int16_t(pitch() >> 16), int16_t(yaw() >> 16), int16_t(roll() >> 16)}; }
    // The model's hash (0: none).
    uint32_t model_hash() const;
};

struct Instance {
    const Mem* m = nullptr;
    U a = 0;
    VALOR_INSTANCE_FIELDS(VALOR_VIEW_FIELD)

    // The pose a draw shows: the bones' quaternions (x, y, z, w; 4096 = 1) and joints (model
    // space), for skinning (valor::skin_vertices). Empty when the instance has no skeleton.
    std::vector<std::array<int16_t, 4>> quaternions() const;
    std::vector<std::array<int16_t, 3>> joints() const;
};

struct Player {
    const Mem* m = nullptr;
    U a = 0;  // the player's object (the soldier's +0x388)
    VALOR_PLAYER_FIELDS(VALOR_VIEW_FIELD)

    int16_t clip() const { return int16_t(m->s16(a + 0x66 + 2 * slot())); }
    int16_t reserve() const { return int16_t(m->s16(a + 0x56 + 2 * slot())); }
};

// The camera's object (movement.md, The camera): the view's points.
#define VALOR_CAMERA_FIELDS(X)                                                    \
    X(look_x, int32_t, 0x000, "the look point, 16.16")                          \
    X(look_y, int32_t, 0x004, "")                                                \
    X(look_z, int32_t, 0x008, "")                                                \
    X(eye_x, int32_t, 0x03C, "the eye in the death modes (camera_place), 16.16") \
    X(eye_y, int32_t, 0x040, "")                                                 \
    X(eye_z, int32_t, 0x044, "")                                                 \
    X(mode, uint32_t, 0x09C, "0x80 follows the soldier, 0x800 the death camera")

struct Camera {
    const Mem* m = nullptr;
    U e = 0, a = 0;  // the camera entity, its object
    VALOR_CAMERA_FIELDS(VALOR_VIEW_FIELD)

    bool death() const { return mode() & 0x1800; }
    // The eye (whole units): the camera entity's position, or in the death modes camera_place's.
    std::array<double, 3> eye() const;
    std::array<double, 3> look() const { return {look_x() / 65536.0, look_y() / 65536.0, look_z() / 65536.0}; }
};

#undef VALOR_VIEW_FIELD

// The entity list from g_entities, in the game's order.
std::vector<Entity> entities(const Mem& m);
Instance instance(const Entity& e);
// The player's soldier, the camera (the view: its position and angles) and the latest tick.
Entity soldier(const Mem& m);
Entity camera(const Mem& m);
Camera camera_view(const Mem& m);
Player player(const Mem& m);
uint32_t tick(const Mem& m);

}  // namespace valor::sim::view
