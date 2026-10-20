/* Types and globals for m2c (`fn.py c ADDR`), from the documents beside this file.
 * unk_XXX fields are padding; rename them as they are understood. */

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;

typedef struct { s32 x, y, z; } Vec3;
typedef struct { s16 x, y, z; } SVec3;

struct Entity;
typedef s32 (*EntityFn)();

/* Hit part, 0x48 bytes (entities.md). */
typedef struct HitPart {
    /* 0x00 */ s32 hit;
    /* 0x04 */ s32 unk04;
    /* 0x08 */ s32 kind;
    /* 0x0C */ u8 unk0C[0x14];
    /* 0x20 */ Vec3 centre;
    /* 0x2C */ u8 unk2C[0xC];
    /* 0x38 */ s32 radius2;
    /* 0x3C */ u8 unk3C[0xC];
} HitPart;

/* The entity, 0x3D0 bytes (entities.md). */
typedef struct Entity {
    /* 0x000 */ s32 unk000;
    /* 0x004 */ struct Entity *next;
    /* 0x008 */ s32 type;
    /* 0x00C */ u32 flags_a;
    /* 0x010 */ u32 flags_b;
    /* 0x014 */ u32 collide_mask;
    /* 0x018 */ u32 attach_state;
    /* 0x01C */ void *record;          /* the spawner / level record */
    /* 0x020 */ u8 unk020[0x8];
    /* 0x028 */ u32 stamp;
    /* 0x02C */ u32 parent;
    /* 0x030 */ u32 attach;
    /* 0x034 */ s8 attach_bone;
    /* 0x035 */ u8 unk035;
    /* 0x036 */ u16 part_count;
    /* 0x038 */ u32 contact;
    /* 0x03C */ u32 unk03C;
    /* 0x040 */ u8 unk040[0xC];
    /* 0x04C */ s32 vars[0x28];        /* script variables 0x00..0x27 */
    /* 0x0EC */ u32 var_bits;          /* script variables 0x100.. */
    /* 0x0F0 */ s32 health_max;        /* 16.16 */
    /* 0x0F4 */ s32 health;            /* 16.16 */
    /* 0x0F8 */ u32 unk0F8;
    /* 0x0FC */ s32 spawn_type;
    /* 0x100 */ void *anim;
    /* 0x104 */ u8 script_ctx[2][0x74];
    /* 0x1EC */ u32 unk1EC;
    /* 0x1F0 */ Vec3 push;
    /* 0x1FC */ s32 grav_accel;
    /* 0x200 */ s32 fall_speed;
    /* 0x204 */ s32 fall_max;
    /* 0x208 */ s32 floor;
    /* 0x20C */ Vec3 pos;
    /* 0x218 */ Vec3 vel;
    /* 0x224 */ s32 speed;
    /* 0x228 */ Vec3 angles;
    /* 0x234 */ Vec3 ang_vel;
    /* 0x240 */ Vec3 root_angles;
    /* 0x24C */ u8 unk24C[0xC];
    /* 0x258 */ Vec3 prev_pos;
    /* 0x264 */ Vec3 prev_angles;
    /* 0x270 */ u8 unk270[0x14];
    /* 0x284 */ s32 on_ground;
    /* 0x288 */ u8 unk288[0x4C];
    /* 0x2D4 */ s32 eye_height;
    /* 0x2D8 */ SVec3 sphere_local;
    /* 0x2DE */ u16 unk2DE;
    /* 0x2E0 */ SVec3 sphere;
    /* 0x2E6 */ u16 unk2E6;
    /* 0x2E8 */ u16 radius;
    /* 0x2EA */ u8 unk2EA[0x1A];
    /* 0x304 */ HitPart *parts;
    /* 0x308 */ s16 bbox[6];
    /* 0x314 */ s32 part_dist;
    /* 0x318 */ s32 surface;
    /* 0x31C */ u8 unk31C[0x24];
    /* 0x340 */ s16 rot[3][3];
    /* 0x352 */ u16 unk352;
    /* 0x354 */ SVec3 ipos;
    /* 0x35A */ u8 unk35A[0xA];
    /* 0x364 */ SVec3 contact_normal;
    /* 0x36A */ u16 unk36A;
    /* 0x36C */ EntityFn think;
    /* 0x370 */ EntityFn draw;
    /* 0x374 */ EntityFn hit_part;
    /* 0x378 */ EntityFn world_contact;
    /* 0x37C */ EntityFn touch;
    /* 0x380 */ EntityFn unk380;
    /* 0x384 */ EntityFn unk384;
    /* 0x388 */ void *object;
    /* 0x38C */ struct Brain *brain;
    /* 0x390 */ u8 unk390[0x2C];
    /* 0x3BC */ Vec3 move_from;
    /* 0x3C8 */ s16 unk3C8;
    /* 0x3CA */ s16 weapon;
    /* 0x3CC */ u8 inv[4];
} Entity;

/* Weapon table entry, 0x44 bytes (player.md). */
typedef struct Weapon {
    /* 0x00 */ u32 hash;
    /* 0x04 */ s16 refire;
    /* 0x06 */ s16 carried;
    /* 0x08 */ s16 max_ammo;
    /* 0x0A */ s16 clip;
    /* 0x0C */ u8 unk0C[0x8];
    /* 0x14 */ s16 sound;
    /* 0x16 */ s16 sound_variant;
    /* 0x18 */ u8 unk18[0x12];
    /* 0x2A */ u16 flags;
    /* 0x2C */ s32 damage;
    /* 0x30 */ u8 unk30[0xC];
    /* 0x3C */ char *ammo_name;
    /* 0x40 */ u32 unk40;
} Weapon;

/* Entity script context, 0x74 bytes (enemies.md). */
typedef struct ScriptCtx {
    /* 0x00 */ u8 unk00[0x10];
    /* 0x10 */ void *script;
    /* 0x14 */ s32 pc;
    /* 0x18 */ s32 wait_frame;
    /* 0x1C */ s16 wait_ticks;
    /* 0x1E */ s16 depth;
    /* 0x20 */ u32 unk20;
    /* 0x24 */ u8 frames[5][16];
} ScriptCtx;

extern Entity *g_entities;
extern Entity *g_soldier;
extern Entity *g_player;
extern Entity *g_world;
extern Weapon g_weapons[13];
extern u32 g_tick;
extern void *g_level;

/* Enemy brain, 0x124 bytes at entity+0x38C (enemies.md, The brain). */
typedef struct Brain {
    /* 0x00 */ s32 unk00;
    /* 0x04 */ u32 goals;              /* 1 seek, 2 path, 4 walk, 0x800 node, 0x1000 point, 0x4000 flee */
    /* 0x08 */ u32 state;              /* 1/2 turning right/left, 4/8/0x10 wall ahead/left/right, 0x20 */
    /* 0x0C */ u32 unk0C;
    /* 0x10 */ struct Entity *seek_target;
    /* 0x14 */ u32 unk14;
    /* 0x18 */ s32 walk_speed;         /* 16.16 */
    /* 0x1C */ u8 unk1C[0x20];
    /* 0x3C */ s32 node;               /* goal 0x800 */
    /* 0x40 */ SVec3 *point;           /* goal 0x1000 */
    /* 0x44 */ u8 unk44[0xC];
    /* 0x50 */ s32 seek_arg;
    /* 0x54 */ u8 unk54[0x28];
    /* 0x7C */ s32 node_var;           /* variable receiving the distance */
    /* 0x80 */ s32 point_var;
    /* 0x84 */ u8 unk84[0x8];
    /* 0x8C */ s32 via_side;           /* heading for the side waypoint first */
    /* 0x90 */ u32 unk90;
    /* 0x94 */ Vec3 pos;
    /* 0xA0 */ SVec3 node_point;
    /* 0xA6 */ u16 unkA6;
    /* 0xA8 */ s16 arrive_radius;
    /* 0xAA */ s16 unkAA;
    /* 0xAC */ s16 side_x, side_z;
    /* 0xB0 */ s16 range_sight;
    /* 0xB2 */ s16 range_hear;
    /* 0xB4 */ s16 unkB4;
    /* 0xB6 */ s16 claimed_node;
    /* 0xB8 */ s16 path;
    /* 0xBA */ s16 path_mode;          /* 0 once, 1 backwards, 2 back and forth */
    /* 0xBC */ s16 path_point;
    /* 0xBE */ s16 path_step;
    /* 0xC0 */ s16 gait;
    /* 0xC2 */ s16 slow_radius;
    /* 0xC4 */ s16 probe_radius;       /* -> entity+0x2EC */
    /* 0xC6 */ s16 radius;             /* -> entity+0x2E8 */
    /* 0xC8 */ s16 aim_error;
    /* 0xCA */ s16 unkCA;              /* -> entity+0x3C8 */
    /* 0xCC */ s16 gait_speed[2];
    /* 0xD0 */ s16 gait_base[2];
    /* 0xD4 */ s16 turn_rate;
    /* 0xD6 */ s16 unkD6;
    /* 0xD8 */ s32 backwards;
    /* 0xDC */ s32 walk_speed_base;
    /* 0xE0 */ u8 unkE0[0xC];
    /* 0xEC */ u8 walls[0x38];         /* nearest wall ahead / left / right (walker_slide) */
    /* 0x124 */
} Brain;

void apply_damage(Entity *victim, s32 damage, Entity *attacker);
void brain_update(Entity *e);
void brain_seek(Entity *e, SVec3 *forward, SVec3 *delta, s32 turn_rate, s32 dist);
void brain_move_to(Entity *e, Entity *target, s32 arg, s32 flee);
s32 brain_steer(Entity *e);
void brain_follow_path(Entity *e);
void brain_goto_node(Entity *e, s32 node, s32 var, s32 radius);
void brain_goto_point(Entity *e, SVec3 *point, s32 var, s32 radius);
s32 brain_node_target(Entity *e, Brain *b, s32 node);
Entity *spawn_by_type(s32 type);
s32 get_var(Entity *e, s32 id);
void set_var(Entity *e, s32 id, s32 value);
