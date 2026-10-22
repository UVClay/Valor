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
#include <string>
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
#define VALOR_PLAYER_FIELDS(X)                                                             \
    X(state, uint32_t, 0x000, "2 crouched, 0x20 aiming, 0x100 scoped...")                \
    X(slot, int8_t, 0x054, "the weapon slot in hand")                                       \
    X(weapon, uint8_t, 0x055, "its weapon (g_weapons index)")                               \
    X(zoom, uint32_t, 0x0A4, "the projection distance: 400, more while zoomed (hud.md)")    \
    X(look_pitch, int32_t, 0x178, "4096 << 16 per turn")                                   \
    X(look_yaw, int32_t, 0x184, "")                                                        \
    X(flash, int16_t, 0x1D6, "the muzzle flash: 1 the frame after a shot (combat.md)")

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

// A sound voice (audio.md, Voices: g_voices 0x8009115C, 0x34 bytes; voices 1..23 effects).
#define VALOR_VOICE_FIELDS(X)                                                              \
    X(flags, uint32_t, 0x00, "1 not positional, 2 not heard by enemies")                 \
    X(handle, uint32_t, 0x04, "the entity it follows (0: none)")                          \
    X(sound, int32_t, 0x08, "the sound id (VAG index in its bank)")                       \
    X(bank, uint16_t, 0x0C, "")                                                           \
    X(loop, uint8_t, 0x0E, "restarted when it ends")                                      \
    X(fade_out, uint8_t, 0x12, "fade-out frames left")                                    \
    X(cls, uint8_t, 0x13, "the distance class 0..3 (falloff)")                            \
    X(volume, int32_t, 0x14, "argument << 7, its high half 0..255")                       \
    X(pitch, int32_t, 0x20, "16.16, 0x10000 the sample's rate")                          \
    X(age, uint16_t, 0x28, "frames since it started")                                     \
    X(x, int16_t, 0x2C, "the source, whole units (copied from the entity)")              \
    X(y, int16_t, 0x2E, "")                                                               \
    X(z, int16_t, 0x30, "")

struct Voice {
    const Mem* m = nullptr;
    U a = 0;
    int index = 0;
    VALOR_VOICE_FIELDS(VALOR_VIEW_FIELD)

    // g_voice_enabled (0x800910D0): 0 off, 1 on, 3 keyed off and still sounding.
    int status() const { return m->u8(0x800910D0 + index); }
};

// One sprite of a particle effect (combat.md, Particles; objects.md, Explosions): the sparks and
// smoke of particles_spawn (type 0x31, think 0x80073B08: 0x34-byte particles) and the fireballs
// and smoke puffs of explosions (type 0x31, think 0x800715D4: 0x3C-byte records), each system
// with its texture in its header (+0x18: VRAM x, y, CLUT x, y, width, height; +0x26 2: additive).
struct Sprite {
    std::array<double, 3> pos{};  // whole units
    // The quad's half extents in world units. Particles (0x800732F8): corners (+-w, +-h) scaled by
    // the system's +0x8 (16.16), turned by RotMatrixYXZ of the particle's three angles (a tumbling
    // quad; a triangle when its +0x3 flag is set). Fireballs and puffs (0x80070F24 ->
    // func_8006E058): camera-facing, corners (+-(w >> 1), +-(h >> 1)) of the record's +0xC/+0xE
    // turned by its +0xA in the view plane, scaled by the header's +0xC >> 4 (4096 = 1), then
    // laid along the view matrix's right and up rows (lengths 2.0 and 1.25, the up one x 1.6), so 2x.
    double half_w = 0, half_h = 0;
    uint8_t rgb[3]{128, 128, 128};  // the prim's colour: 128 leaves the texel as it is
    uint16_t rot[3]{};              // particles: pitch, yaw, roll; billboards: rot[0] in the view plane
    uint16_t tex_x = 0, tex_y = 0, clut_x = 0, clut_y = 0, w = 0, h = 0;
    bool additive = false;
    bool billboard = false;
    bool triangle = false;
};

// A streak of the impact effects (impact_effect_spawn: type 0x23, think 0x800701DC, drawn by
// 0x8006F7BC) and the explosions' first-tick bursts (0x800708F0, drawn by 0x8006FBF0): from p0 to
// p1, coloured c0 at p0 and c1 at p1, with its system's texture and semi-transparency mode.
struct Streak {
    std::array<double, 3> p0{}, p1{};  // whole units
    uint8_t c0[3]{}, c1[3]{};
    uint16_t tex_x = 0, tex_y = 0, clut_x = 0, clut_y = 0, w = 0, h = 0;
    int mode = 0;     // the template's semi value: 0 opaque, else ABR (mode - 1) & 3
    bool quad = false;  // the bursts' four-cornered streaks (POLY_GT4); impacts are triangles
};

// A hit reaction on a soldier or dog (attach.cpp hit_reaction): the puffs (object +0x3E8, 5 of
// 0x24 bytes, drawn by 0x80072310) and the drops (+0x49C, 15 of 0x74, drawn by 0x800729C0).
struct Puff {
    std::array<double, 3> pos{};
    double scale = 0;    // +0x1C >> 4, 4096 = 1
    uint16_t angle = 0;  // +0x8
    uint8_t rgb[3]{};    // +0x20, fading by 1/8 a tick
    bool drop = false;   // a drop (blood): pos +0x34, colour +0x4C, on the floor with flag 8
    bool on_floor = false;
};

// The explosion light (objects.md, Explosions: The light), applied by the world's face drawer
// (func_800114B0) to the vertex colours of faces near it: colour + light * (1 - d^2 / r^2).
struct ExplosionLight {
    bool on = false;
    std::array<double, 3> pos{};  // the latest explosion's (or rocket's) position, whole units
    uint8_t rgb[3]{};
    int32_t radius = 0, radius2 = 0;
    bool scorch = false;  // 0x800AA160: this frame also darkens the faces' colours for good
};

// The objectives (levels.md, Objectives: the list the pause menu shows).
struct Objective {
    uint32_t id = 0;
    bool done = false;
    std::string text;
};

// The on-screen texts (hud.md, Messages: the text list at 0x80099F4C, count 0x80099ED0): the
// ammo digits, the level's messages, the pickups' messages; position in the 512 x 240 screen.
struct Text {
    std::string text;
    int x = 0, y = 0;
    uint8_t rgb[3]{};
    bool additive = false;  // +0xB semi-transparency
    int32_t life = -1;      // ticks left, -1 forever
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
// The fades on screen (hud.md, Fades): the subtractive colour the latest type 0x32 entity shows
// now (nothing during its delay, its start colour for its duration, then its end colour), as
// 0..255 per channel; {0, 0, 0} for none.
std::array<int, 3> fade(const Mem& m);
// The effect voices 1..23, as the sound driver keeps them.
std::vector<Voice> voices(const Mem& m);
std::vector<Objective> objectives(const Mem& m);
std::vector<Streak> streaks(const Mem& m);
std::vector<Puff> puffs(const Mem& m);
// The enemy bullets (type 0x13F4) alive now: handle, the point they left from (their previous
// tick's position, +0x258: the muzzle on the tick they were fired), for the muzzle flash.
struct Shot {
    uint32_t handle = 0;
    std::array<double, 3> from{};
};
std::vector<Shot> enemy_shots(const Mem& m);
ExplosionLight explosion_light(const Mem& m);
std::vector<Text> texts(const Mem& m);
// The hit wedges' timers (0x8009A590: right, left, front, behind; 40 when hit, counting down).
std::array<int32_t, 4> hit_timers(const Mem& m);
// Every live particle and fireball of the effect entities, for drawing.
std::vector<Sprite> sprites(const Mem& m);
// The level script started the music (level_set(1, 1): music_start's 0x8009CC74).
bool music_started(const Mem& m);
// A NUL-terminated string in the engine's memory.
std::string string_at(const Mem& m, U a);

}  // namespace valor::sim::view
