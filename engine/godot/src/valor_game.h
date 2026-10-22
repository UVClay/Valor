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

#include <memory>

namespace valor::sim {
class Engine;
class Feed;
}  // namespace valor::sim

namespace godot {

class ValorGame : public RefCounted {
    GDCLASS(ValorGame, RefCounted)

public:
    ValorGame();
    ~ValorGame() override;
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

    // The whole engine (engine/sim) live: the level built from the disc files (no RAM dump), its
    // soldiers and dogs on their own logic, the player's input per tick. After load_level. ammo:
    // the rounds every weapon of the level starts with (-1: a fresh game's none).
    bool start_engine(int ammo);
    bool is_engine_running() const { return bool(engine_); }
    // One 30 Hz tick, the input as tick() takes it (the buttons in the game's layout 1: Cross
    // 0x40 fires, Circle 0x20 switches weapons, Square 0x80 reloads / uses).
    void engine_tick(int64_t buttons, int64_t forward, int64_t strafe, int64_t turn, int64_t pitch);
    // The entities to draw, by handle: { handle: { model, skinned, type, dead, transform } }
    // (through the accessor layer, sim/views.h).
    Dictionary get_engine_actors() const;
    // The level is over and the engine has built the next (the campaign plays through): true once
    // after each, when the world's meshes and textures are to be rebuilt (get_level_info is the
    // new level's).
    bool take_level_change();
    bool is_campaign_done() const;
    void debug_end_level();  // as the level script's level_set(3, 1): for testing the transitions
    // The engine's effect voices 1..23 (audio.md, Voices) through the accessor layer: { voice, status
    // (0 off, 1 on, 3 releasing), flags, handle, bank, sound, loop, cls, volume, pitch, age, fade_out,
    // pos (game units) }, for the presentation layer to play.
    Array get_engine_voices() const;
    // Drive the engine with a recording's input (build/engine/orig.log) instead of the host's:
    // only the pad and the roles; everything else stays live. An empty path returns to the host's.
    bool set_engine_replay(const String& log_path);
    // An entity's mesh as its pose shows it now (skinned models re-skinned from the core's pose).
    Ref<ArrayMesh> build_engine_actor_mesh(int64_t handle);

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
    std::string data_dir_;
    int mission_ = 0, level_ = 0;
    std::unique_ptr<valor::sim::Feed> feed_, replay_;
    std::unique_ptr<valor::sim::Engine> engine_;
    uint32_t prev_buttons_ = 0;
    int64_t level_changes_seen_ = 0;
    bool level_changed_ = false;
    Ref<ArrayMesh> mesh_of(const valor::Model& m, const std::vector<valor::Quat>* pose,
                           const std::vector<std::array<int16_t, 3>>* joints);
};

}  // namespace godot
