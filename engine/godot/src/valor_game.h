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
    Ref<Texture2DArray> get_texture_pages() const { return pages_; }
    Dictionary get_level_info() const;

    void place(double x, double y, double z, double yaw, double pitch);
    void tick(int64_t forward, int64_t strafe, int64_t turn, int64_t pitch, bool jump, bool crouch);
    Vector3 get_eye_position() const;
    double get_yaw() const;    // Godot rotation.y, radians
    double get_pitch() const;  // Godot rotation.x, radians
    Dictionary get_state() const;
    PackedVector3Array get_light_colours() const;  // the animated lights' colours this tick, 0..1

    static Vector3 to_godot(double x, double y, double z);

protected:
    static void _bind_methods();

private:
    valor::Session session_;
    valor::LevelRender render_;
    Ref<Texture2DArray> pages_;
    String error_;
    bool loaded_ = false;
};

}  // namespace godot
