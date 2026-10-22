// ValorAssets: a level's presentation data as Godot resources, read from the user's extracted disc
// at run time (nothing from the disc is kept in the project). docs/re/formats.md (TAF, VB, BSD),
// audio.md (the banks, the music), hud.md (the HUD's textures and tables), animation.md (the
// player's weapon models). Nothing here feeds the rules.
#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/audio_stream_wav.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <map>
#include <string>
#include <vector>

#include "valor/controls.h"
#include "valor/level.h"
#include "valor/model.h"
#include "valor/render_data.h"

namespace godot {

class ValorAssets : public RefCounted {
    GDCLASS(ValorAssets, RefCounted)

public:
    static constexpr double kUnitsPerMetre = 128.0;  // as ValorGame

    // The level's BSD, TAF (american: the 1 file, else the German 0) and LEVEL.BIN.
    bool load(const String& data_dir, int mission, int level, bool american);
    // The user's disc image in this library too (its own copy of the core's reader, valor/disc.h):
    // "disc:/DATA" paths then read from it.
    bool mount_disc(const String& path);
    String get_error() const { return error_; }

    // The BSD header's presentation fields: sky colour (R +0x58C, G +0x58E, B +0x58D, 0..255),
    // star height (+0x58F), moon y/z (+0x594/+0x596), fog.
    Dictionary get_level_info() const;
    // Bytes of LEVEL.BIN's static data at a RAM address (its tables: glyphs, weapons, names).
    PackedByteArray level_bin(int64_t addr, int64_t len) const;
    // A NUL-terminated string in LEVEL.BIN at a RAM address.
    String level_string(int64_t addr) const;

    // A texture page as a CLUT shows it (256 x 256 RGBA8; alpha 0 for the transparent texel), or a
    // rectangle of it.
    Ref<Image> page_image(int tpage, int clut) const;
    Ref<Image> region_image(int tpage, int clut, int u, int v, int w, int h) const;

    // Sound banks (the TAF's VABs, in slot order) and their sounds (VAG index), decoded to 16-bit
    // PCM at the sample's own rate (22050 Hz: pitch 0x1000 is 44100 Hz, every base pitch is 0x800).
    int get_bank_count() const { return int(banks_.size()); }
    int get_sound_count(int bank) const;
    Ref<AudioStreamWAV> get_sound(int bank, int sound);
    // The level's music (Mm_l.VB, or the ambience-only Mm_lA.VB), mono 22050 Hz, looping.
    Ref<AudioStreamWAV> get_music(bool ambience_only) const;

    // A model's sound bank (+0xC8), -1 for an unknown model.
    int get_model_bank(int64_t hash) const;
    // The player's weapon models (animation.md, The player: first-person weapon models): the
    // frame count of an animation, and a frame's skinned mesh in model space (Godot axes, the
    // weapon's origin at the eye's; surfaces named as the world shaders expect).
    int get_anim_frames(int64_t hash, int anim) const;
    Ref<ArrayMesh> build_model_mesh(int64_t hash, int anim, int frame);
    // A point on a bone of that pose (model_bone_point: the joint plus the offset turned by the
    // bone), Godot axes: where the muzzle flash hangs (g_weapons +0x34 bone, +0x36 offset).
    Vector3 bone_point(int64_t hash, int anim, int frame, int bone, Vector3i offset);
    // A model's +0x30 face list as scope_draw (0x8007B534) draws it: untextured Gouraud triangles
    // over its vertices (+0x88, x and y) and vertex colours (+0xC4): { verts: PackedVector2Array,
    // colors: PackedColorArray (0..255 as is), tris: PackedInt32Array }. Empty for an unknown model.
    Dictionary get_model_list_faces(int64_t hash) const;

    // The PVS (formats.md, The render tree): the world's leaves in the order ValorGame's world mesh
    // takes its faces (every chunk's tree walked as RenderChunk::parse walks it), each face's leaf
    // as a per-vertex id for a surface (opaque, semi0..3: the world mesh's), and from a camera
    // point (game units) every leaf's visibility as tsp_walk decides it: the camera's leaf in each
    // chunk (pvs_leaf_find: of the boxes over the point, x and z holding it and the bottom below it, the lowest-bottomed; its B ORed over the
    // chunks, its A ORed too), then a node drawn only if its A & that B and its B & that A, and a
    // leaf only if every node above it was. pvs_load returns the leaf count (0: no TSPs).
    int pvs_load();
    PackedFloat32Array pvs_leaf_ids(int surface) const;
    PackedByteArray pvs_visibility(Vector3 camera);

    // The texture pages the meshes built so far use, one layer per page and CLUT.
    Ref<Texture2DArray> get_texture_pages();

    // A pad's sticks (bytes, 0x80 centred) through the game's own analog curve and a fresh card's
    // configuration (movement.md, Control roles: the core's role_axis): the four roles' values
    // (forward, strafe, pitch, turn; 16.16) as the core takes them from a host.
    PackedInt32Array stick_roles(int rx, int ry, int lx, int ly) const;

protected:
    static void _bind_methods();

private:
    String error_;
    std::string data_dir_;
    int mission_ = 0, level_ = 0;
    valor::LevelInfo info_;
    std::vector<uint8_t> bsd_, level_bin_;
    valor::Vram vram_;
    valor::Models models_;
    valor::Controls controls_;
    struct PvsNode {
        int16_t min[3], max[3];
        uint32_t a = 0, b = 0;
        int child[3]{-1, -1, -1};  // node indices in the chunk; leaves have none
        bool leaf = false;
        int leaf_id = -1;
    };
    struct PvsChunk {
        std::vector<PvsNode> nodes;  // [0] the root
    };
    std::vector<PvsChunk> pvs_chunks_;
    std::vector<std::vector<std::pair<int, int>>> pvs_leaf_path_;  // per leaf: (chunk, node) from the root
    std::vector<float> pvs_face_leaf_[5];                          // per surface: a leaf id per vertex
    int pvs_leaves_ = 0;
    std::vector<std::vector<uint8_t>> banks_;  // each VAB's body
    std::vector<std::vector<uint32_t>> bank_offsets_;  // per bank: VAG byte offsets, then the end
    std::map<uint64_t, Ref<AudioStreamWAV>> sounds_;
    std::map<uint32_t, int> layer_of_;
    TypedArray<Image> page_images_;
    Ref<Texture2DArray> pages_;
    bool pages_dirty_ = true;
    int layer(uint16_t tpage, uint16_t clut);
};

}  // namespace godot
