// ValorGame: the core hosted in Godot. Loads a level from the extracted disc, builds its render
// geometry and textures as Godot resources, and steps the player at 30 Hz from the host's input.
#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <map>
#include <vector>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "valor/anim.h"
#include "valor/model.h"
#include "valor/render_data.h"
#include "valor/session.h"

namespace godot {

class ValorGame : public RefCounted {
    GDCLASS(ValorGame, RefCounted)

public:
    // World units per metre in Godot space (the eye is 240 units up: 1.9 m).
    static constexpr double kUnitsPerMetre = 128.0;

    bool load_level(const String& data_dir, int mission, int level);
    String get_error() const { return error_; }

    Ref<ArrayMesh> build_world_mesh();
    // The texture pages the meshes built so far use (one layer per page and CLUT): call after
    // building them.
    Ref<Texture2DArray> get_texture_pages();

    // Actors: the models the level's records name (soldier spawns, objects), placed at their
    // records; skinned ones play an animation in a loop (the scripts that choose animations are
    // not ported). Their meshes are posed by the core every tick.
    int spawn_actors();
    int get_actor_count() const { return int(actors_.size()); }
    Transform3D get_actor_transform(int i) const;
    bool is_actor_skinned(int i) const;
    Ref<ArrayMesh> build_actor_mesh(int i);
    // A character as a RAM dump has it (engine/tools/scene_from_dump.py): a model, a position (game
    // units), angles (4096 per turn) and the animation and frame it shows, held there.
    int add_actor(int64_t model, Vector3 pos, Vector3i angles, int anim, int frame);
    void remove_skinned_actors();
    Dictionary get_level_info() const;

    void place(double x, double y, double z, double yaw, double pitch);
    // One tick from the host's input: the pad's buttons (PS1 bits: 0x10 Triangle, 0x1 L2, ...)
    // and the analog roles' values in the game's units (16.16): forward (negative is forward),
    // strafe, turn (yaw units added), pitch (pitch -= value / 2), as the runtime's mouse and
    // keyboard give them.
    void tick(int64_t buttons, int64_t forward, int64_t strafe, int64_t turn, int64_t pitch);
    Vector3 get_eye_position() const;
    double get_yaw() const;    // Godot rotation.y, radians
    double get_pitch() const;  // Godot rotation.x, radians
    Dictionary get_state() const;
    PackedVector3Array get_light_colours() const;  // the animated lights' colours this tick, 0..1

    static Vector3 to_godot(double x, double y, double z);

protected:
    static void _bind_methods();

private:
    struct Actor {
        int record;
        const valor::Model* model;
        valor::AnimInstance inst;
        int anim;
        int32_t pos[3];    // whole units
        int16_t ang[3];
        bool frozen = false;  // holds its frame (a scene from a dump)
    };
    valor::Session session_;
    valor::LevelRender render_;
    valor::Models models_;
    std::vector<Actor> actors_;
    Ref<Texture2DArray> pages_;
    std::map<uint32_t, int> layer_of_;
    TypedArray<Image> page_images_;
    bool pages_dirty_ = true;
    int layer(uint16_t tpage, uint16_t clut);
    String error_;
    bool loaded_ = false;
};

}  // namespace godot
