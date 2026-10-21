// A level's models (the BSD's render objects) and their skeletal data (docs/re/formats.md: Models,
// the shared skeletal tables; animation.md: the skeleton, animation table and frames, the pose,
// skinning and joints, hit spheres). Parsed from the BSD file; the pose code follows
// tools/sim/combat_sim.py, which matches the original's RAM exactly.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "valor/gte.h"

namespace valor {

using Quat = std::array<int16_t, 4>;  // x, y, z, w; 4096 = 1

struct AnimFrame {
    uint32_t move = 0;   // +0x0: root movement, 10:11:10 bits
    uint32_t root = 0;   // +0x4: root offset, packed the same way
    int16_t turn[3]{};   // +0x8: root turn (pitch, yaw, roll)
    uint8_t keys = 0;    // +0xE: frames to the next key (high nibble), back to the previous (low)
    uint8_t nquats = 0;  // +0xF
    int32_t quats = -1;  // +0x10: offset of the packed quaternions (key frames), -1 none
};

struct Anim {
    uint8_t bones = 0;
    std::vector<AnimFrame> frames;  // empty: an animation this level doesn't load
};

struct Bone {
    uint16_t index = 0;  // the quaternion, vertex table and joint it uses
    int16_t offset[3]{};  // from the parent's joint, in the parent's frame
    int first_child = -1, next_sibling = -1;  // indices into Model::bones
};

// A skinned face (28 bytes): per corner a colour, a UV and (bone, vertex in the bone's table).
struct SkinFace {
    uint8_t rgb[3][3];
    uint8_t uv[3][2];
    uint16_t clut, tpage;
    uint8_t bone[3], vertex[3];
};

// A static face (16 bytes, model +0x2C): UVs, CLUT, tpage and three vertex indices.
struct StaticFace {
    uint8_t uv[3][2];
    uint16_t clut, tpage;
    uint16_t v[3];
};

struct HitPartTemplate {
    int32_t kind = 0;          // +0x8
    uint16_t a = 0, b = 0;     // +0x1C, +0x1E: bones (0xFFFF a fixed point, 0xFFFE plus the eye)
    int16_t radius = 0;        // +0x34
    int32_t radius2 = 0;       // +0x38
    int16_t off[3]{};          // +0x40
};

struct Model {
    uint32_t hash = 0, base = 0xFFFFFFFF;  // +0x0, +0xE8 (inherits from that model)
    int32_t type = 0;                      // +0xFC
    int32_t radius = 0;                    // +0xA0 (16.16)
    int32_t scale[3]{};                    // +0xA4
    std::vector<Anim> anims;               // +0x08
    std::vector<Bone> bones;               // from +0x9C, the root first
    std::vector<SkinFace> faces;           // +0x38
    std::vector<std::vector<std::array<int16_t, 3>>> bone_vertices;  // +0x84, per bone index
    std::vector<StaticFace> static_faces;  // +0x2C
    std::vector<std::array<int16_t, 3>> static_vertices;  // +0x88
    std::vector<std::array<uint8_t, 3>> static_colours;   // +0xC4
    std::vector<HitPartTemplate> parts;    // +0xBE count, +0xC0
    int16_t gun_bone[2]{};                 // +0xD4, +0xD6: the gun's near and far bones
    int16_t gun_off[2][3]{};               // +0xD8, +0xE0: their offsets
    bool skinned() const { return !bones.empty(); }
};

class Models {
public:
    bool load(const std::string& data_dir, int mission, int level, std::string* err);
    std::vector<Model> models;  // in the BSD's order
    const Model* by_hash(uint32_t hash) const;

    // anim_frame_pose (0x8008492C): a frame's quaternions, decoded or interpolated between keys.
    std::vector<Quat> frame_pose(const Model& m, int anim, int frame) const;
    // The packed root offset of a frame, unpacked (10:11:10 bits, signed).
    static void unpack3(uint32_t w, int16_t out[3]);

    Gte gte;

private:
    std::vector<uint8_t> body_;          // the BSD body (file offset 0x800 on)
    uint32_t quat_base_ = 0;             // +0x554
    std::vector<Quat> decode(int32_t off, int n) const;
};

// One bone of the pose interpolation (anim_frame_pose and the cross-fade): a toward b at k
// quarters (1, 2, 3).
Quat quat_interp(const Gte& g, const Quat& a, const Quat& b, int k);

// skeleton_pose (0x80084718) / skeleton_walk (0x80013AE4): the joints in model space (3 x s16 per
// bone index), from the pose, the root offset and the frame's bone count.
std::vector<std::array<int16_t, 3>> skeleton_joints(const Model& m, const std::vector<Quat>& quats,
                                                    const int16_t root[3], int nbones);

// The skinned vertices in model space, per bone index (the draw's skinning, for a host to render):
// each bone's vertices turned by its own rotation and moved to its joint.
std::vector<std::vector<std::array<int32_t, 3>>> skin_vertices(const Model& m, const std::vector<Quat>& pose,
                                                               const std::vector<std::array<int16_t, 3>>& joints);

// entity_parts_update (0x8004CEE4): a hit part's centre in model space.
void part_centre(const HitPartTemplate& p, const std::vector<std::array<int16_t, 3>>& joints,
                 const std::vector<Quat>* quats, int32_t eye, int16_t out[3]);

}  // namespace valor
