#include "valor_game.h"

#include <cmath>
#include <map>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace godot {

namespace {
constexpr double kAngle = 2.0 * 3.14159265358979323846 / 4096.0;  // radians per game angle unit
}

void ValorGame::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_level", "data_dir", "mission", "level"), &ValorGame::load_level);
    ClassDB::bind_method(D_METHOD("get_error"), &ValorGame::get_error);
    ClassDB::bind_method(D_METHOD("build_world_mesh"), &ValorGame::build_world_mesh);
    ClassDB::bind_method(D_METHOD("get_texture_pages"), &ValorGame::get_texture_pages);
    ClassDB::bind_method(D_METHOD("get_level_info"), &ValorGame::get_level_info);
    ClassDB::bind_method(D_METHOD("place", "x", "y", "z", "yaw", "pitch"), &ValorGame::place);
    ClassDB::bind_method(D_METHOD("tick", "forward", "strafe", "turn", "pitch", "jump", "crouch"), &ValorGame::tick);
    ClassDB::bind_method(D_METHOD("get_eye_position"), &ValorGame::get_eye_position);
    ClassDB::bind_method(D_METHOD("get_yaw"), &ValorGame::get_yaw);
    ClassDB::bind_method(D_METHOD("get_pitch"), &ValorGame::get_pitch);
    ClassDB::bind_method(D_METHOD("get_state"), &ValorGame::get_state);
    ClassDB::bind_static_method("ValorGame", D_METHOD("to_godot", "x", "y", "z"), &ValorGame::to_godot);
    BIND_CONSTANT(kUnitsPerMetre);
}

Vector3 ValorGame::to_godot(double x, double y, double z) {
    // y down, z forward at yaw 0 -> Godot's y up, -z forward: a half turn about x.
    return Vector3(real_t(x / kUnitsPerMetre), real_t(-y / kUnitsPerMetre), real_t(-z / kUnitsPerMetre));
}

bool ValorGame::load_level(const String& data_dir, int mission, int level) {
    loaded_ = false;
    std::string dir = data_dir.utf8().get_data(), err;
    if (!info_.load(dir, mission, level, &err) || !world_.load(dir, mission, level, &err) ||
        !render_.load(dir, info_, &err)) {
        error_ = String(err.c_str());
        return false;
    }
    world_.set_all_chunks();  // streaming isn't ported: every chunk stays loaded
    player_.spawn(world_, info_);
    pages_.unref();
    loaded_ = true;
    return true;
}

Ref<ArrayMesh> ValorGame::build_world_mesh() {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    if (!loaded_) return mesh;
    // One texture layer per (page, depth, CLUT) the faces use.
    std::map<uint32_t, int> layer_of;
    TypedArray<Image> images;
    std::vector<uint8_t> rgba;
    auto layer = [&](const valor::RenderFace& f) {
        uint32_t key = uint32_t(f.tpage & 0x19F) | uint32_t(f.clut) << 16;
        auto it = layer_of.find(key);
        if (it != layer_of.end()) return it->second;
        render_.vram.decode_page(f.tpage, f.clut, rgba);
        PackedByteArray bytes;
        bytes.resize(int64_t(rgba.size()));
        memcpy(bytes.ptrw(), rgba.data(), rgba.size());
        images.push_back(Image::create_from_data(256, 256, false, Image::FORMAT_RGBA8, bytes));
        int n = int(layer_of.size());
        layer_of[key] = n;
        return n;
    };
    // Surfaces: opaque faces, then semi-transparent ones by blend mode.
    struct Surface {
        PackedVector3Array v;
        PackedColorArray c;
        PackedVector2Array uv, uv2;
    };
    Surface surf[5];
    for (const valor::RenderChunk& ch : render_.chunks) {
        for (const valor::RenderFace& f : ch.faces) {
            Surface& s = surf[f.semi() ? 1 + f.blend() : 0];
            float l = float(layer(f));
            for (int k = 0; k < 3; k++) {
                const valor::RenderVertex& rv = ch.verts[f.v[k]];
                uint8_t col[3];
                render_.lit_colour(info_, rv, col);
                s.v.push_back(to_godot(rv.x, rv.y, rv.z));
                s.c.push_back(Color(col[0] / 255.0f, col[1] / 255.0f, col[2] / 255.0f));
                s.uv.push_back(Vector2(f.uv[k][0] / 256.0f, f.uv[k][1] / 256.0f));
                s.uv2.push_back(Vector2(l, 0));
            }
        }
    }
    static const char* names[5] = {"opaque", "semi0", "semi1", "semi2", "semi3"};
    for (int i = 0; i < 5; i++) {
        if (surf[i].v.is_empty()) continue;
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = surf[i].v;
        arrays[Mesh::ARRAY_COLOR] = surf[i].c;
        arrays[Mesh::ARRAY_TEX_UV] = surf[i].uv;
        arrays[Mesh::ARRAY_TEX_UV2] = surf[i].uv2;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        mesh->surface_set_name(mesh->get_surface_count() - 1, names[i]);
    }
    pages_.instantiate();
    if (!images.is_empty()) pages_->create_from_images(images);
    return mesh;
}

Dictionary ValorGame::get_level_info() const {
    Dictionary d;
    d["mission"] = info_.mission;
    d["level"] = info_.level;
    d["chunks"] = int(world_.chunks.size());
    d["fog_near"] = info_.fog_near;
    d["fog_color"] = Color(info_.fog_rgb[0] / 255.0f, info_.fog_rgb[1] / 255.0f, info_.fog_rgb[2] / 255.0f);
    d["sky_color"] = Color(info_.sky_rgb[0] / 255.0f, info_.sky_rgb[1] / 255.0f, info_.sky_rgb[2] / 255.0f);
    d["view_distance"] = int64_t(info_.view_distance);
    d["lights"] = int64_t(info_.lights.size());
    return d;
}

void ValorGame::tick(int64_t forward, int64_t strafe, int64_t turn, int64_t pitch, bool jump, bool crouch) {
    if (!loaded_) return;
    valor::PlayerInput in;
    in.forward = valor::s32(forward), in.strafe = valor::s32(strafe), in.turn = valor::s32(turn);
    in.pitch = valor::s32(pitch), in.jump = jump, in.crouch = crouch;
    player_.tick(world_, in);
}

void ValorGame::place(double x, double y, double z, double yaw, double pitch) {
    // Game units (y down) and game angles (4096 per turn).
    int32_t pos[3] = {valor::s32(int64_t(x * 65536.0)), valor::s32(int64_t(y * 65536.0)),
                      valor::s32(int64_t(z * 65536.0))};
    player_.place(pos, valor::s32(int64_t(yaw * 65536.0)), valor::s32(int64_t(pitch * 65536.0)), true);
}

Vector3 ValorGame::get_eye_position() const {
    const valor::Walker& w = player_.walker();
    return to_godot(w.pos[0] / 65536.0, w.pos[1] / 65536.0 + player_.eye_offset(), w.pos[2] / 65536.0);
}

double ValorGame::get_yaw() const { return -(player_.yaw() / 65536.0) * kAngle; }
double ValorGame::get_pitch() const { return (player_.pitch() / 65536.0) * kAngle; }

Dictionary ValorGame::get_state() const {
    const valor::Walker& w = player_.walker();
    Dictionary d;
    d["tick"] = int64_t(player_.ticks());
    d["x"] = w.pos[0] / 65536.0;
    d["y"] = w.pos[1] / 65536.0;
    d["z"] = w.pos[2] / 65536.0;
    d["vy"] = w.vy / 65536.0;
    d["ground"] = bool(w.ground);
    d["zone"] = int64_t(w.zone);
    d["crouched"] = player_.crouched();
    d["yaw"] = player_.yaw() / 65536.0;
    return d;
}

}  // namespace godot
