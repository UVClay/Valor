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
    ClassDB::bind_method(D_METHOD("spawn_actors"), &ValorGame::spawn_actors);
    ClassDB::bind_method(D_METHOD("get_actor_count"), &ValorGame::get_actor_count);
    ClassDB::bind_method(D_METHOD("get_actor_transform", "i"), &ValorGame::get_actor_transform);
    ClassDB::bind_method(D_METHOD("is_actor_skinned", "i"), &ValorGame::is_actor_skinned);
    ClassDB::bind_method(D_METHOD("build_actor_mesh", "i"), &ValorGame::build_actor_mesh);
    ClassDB::bind_method(D_METHOD("get_level_info"), &ValorGame::get_level_info);
    ClassDB::bind_method(D_METHOD("place", "x", "y", "z", "yaw", "pitch"), &ValorGame::place);
    ClassDB::bind_method(D_METHOD("tick", "buttons", "forward", "strafe", "turn", "pitch"), &ValorGame::tick);
    ClassDB::bind_method(D_METHOD("get_eye_position"), &ValorGame::get_eye_position);
    ClassDB::bind_method(D_METHOD("get_yaw"), &ValorGame::get_yaw);
    ClassDB::bind_method(D_METHOD("get_pitch"), &ValorGame::get_pitch);
    ClassDB::bind_method(D_METHOD("get_state"), &ValorGame::get_state);
    ClassDB::bind_method(D_METHOD("get_light_colours"), &ValorGame::get_light_colours);
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
    if (!session_.load(dir, mission, level, &err) || !render_.load(dir, session_.info(), &err) ||
        !models_.load(dir, mission, level, &err)) {
        error_ = String(err.c_str());
        return false;
    }
    pages_.unref();
    layer_of_.clear();
    page_images_.clear();
    actors_.clear();
    loaded_ = true;
    return true;
}

int ValorGame::layer(uint16_t tpage, uint16_t clut) {
    uint32_t key = uint32_t(tpage & 0x19F) | uint32_t(clut) << 16;
    auto it = layer_of_.find(key);
    if (it != layer_of_.end()) return it->second;
    std::vector<uint8_t> rgba;
    render_.vram.decode_page(tpage, clut, rgba);
    PackedByteArray bytes;
    bytes.resize(int64_t(rgba.size()));
    memcpy(bytes.ptrw(), rgba.data(), rgba.size());
    page_images_.push_back(Image::create_from_data(256, 256, false, Image::FORMAT_RGBA8, bytes));
    int n = int(layer_of_.size());
    layer_of_[key] = n;
    pages_dirty_ = true;
    return n;
}

Ref<Texture2DArray> ValorGame::get_texture_pages() {
    if (pages_dirty_ || pages_.is_null()) {
        pages_.instantiate();
        if (!page_images_.is_empty()) pages_->create_from_images(page_images_);
        pages_dirty_ = false;
    }
    return pages_;
}

namespace {

// Surfaces: opaque faces, then semi-transparent ones by blend mode.
struct Surface {
    PackedVector3Array v;
    PackedColorArray c;
    PackedVector2Array uv, uv2;
};

void add_surfaces(Ref<ArrayMesh>& mesh, Surface (&surf)[5]) {
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
}

bool semi(uint16_t tpage) { return tpage & 0x4000; }
int blend_mode(uint16_t tpage) { return (tpage >> 5) & 3; }

}  // namespace

Ref<ArrayMesh> ValorGame::build_world_mesh() {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    if (!loaded_) return mesh;
    Surface surf[5];
    for (const valor::RenderChunk& ch : render_.chunks) {
        for (const valor::RenderFace& f : ch.faces) {
            Surface& s = surf[f.semi() ? 1 + f.blend() : 0];
            float l = float(layer(f.tpage, f.clut));
            for (int k = 0; k < 3; k++) {
                const valor::RenderVertex& rv = ch.verts[f.v[k]];
                uint8_t col[3];
                render_.lit_colour(session_.info(), rv, col);
                s.v.push_back(to_godot(rv.x, rv.y, rv.z));
                s.c.push_back(Color(col[0] / 255.0f, col[1] / 255.0f, col[2] / 255.0f));
                s.uv.push_back(Vector2(f.uv[k][0] / 256.0f, f.uv[k][1] / 256.0f));
                // UV2.y: 1 + the animated light the colour names (0: none), coloured per tick.
                s.uv2.push_back(Vector2(l, float(valor::LevelRender::light_index(session_.info(), rv) + 1)));
            }
        }
    }
    add_surfaces(mesh, surf);
    return mesh;
}

int ValorGame::spawn_actors() {
    actors_.clear();
    if (!loaded_) return 0;
    const valor::Records& recs = session_.records();
    for (size_t i = 0; i < recs.records.size(); i++) {
        const valor::Record& r = recs.records[i];
        if (!r.model || r.cls == 0x8877B646) continue;  // the player start names the player's model
        const valor::Model* m = models_.by_hash(r.model);
        if (!m || (!m->skinned() && m->static_faces.empty())) continue;
        Actor a{int(i), m, {}, -1};
        a.inst.model = m;
        if (m->skinned()) {
            // A walk if the model has one (the soldier's animation 7), else its first animation.
            for (int k : {7, 0, 1, 2, 3}) {
                if (size_t(k) < m->anims.size() && m->anims[k].frames.size() > 1) {
                    a.anim = k;
                    break;
                }
            }
            if (a.anim < 0)
                for (size_t k = 0; k < m->anims.size() && a.anim < 0; k++)
                    if (!m->anims[k].frames.empty()) a.anim = int(k);
            if (a.anim < 0) continue;
            // Each actor starts at its own frame, so a level's soldiers don't march in step.
            int n = int(m->anims[a.anim].frames.size());
            a.inst.start(a.anim, int(i * 7) % n, -1, 2);
            a.inst.build(models_);
        }
        actors_.push_back(std::move(a));
    }
    return int(actors_.size());
}

bool ValorGame::is_actor_skinned(int i) const {
    return i >= 0 && size_t(i) < actors_.size() && actors_[i].model->skinned();
}

Transform3D ValorGame::get_actor_transform(int i) const {
    if (i < 0 || size_t(i) >= actors_.size()) return Transform3D();
    const valor::Record& r = session_.records().records[actors_[i].record];
    // The model turned by RotMatrixYXZ of the record's angles and placed at its position; in
    // Godot's axes (y and z flipped) the basis is C M C.
    int16_t m[3][3];
    valor::rot_yxz(r.ang[0], r.ang[1], r.ang[2], m);
    const float c[3] = {1, -1, -1};
    Basis b;
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) b[row][col] = real_t(c[row] * c[col] * m[row][col] / 4096.0);
    return Transform3D(b, to_godot(r.pos[0], r.pos[1], r.pos[2]));
}

Ref<ArrayMesh> ValorGame::build_actor_mesh(int i) {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    if (i < 0 || size_t(i) >= actors_.size()) return mesh;
    Actor& a = actors_[i];
    const valor::Model& m = *a.model;
    Surface surf[5];
    // Model colours follow the animated lights as the world's do (formats.md: red below 40 names
    // a light, black past the level's).
    const valor::LevelInfo& info = session_.info();
    auto corner = [&](Surface& s, int32_t x, int32_t y, int32_t z, const uint8_t* rgb, const uint8_t* uv, float l) {
        valor::RenderVertex rv{0, 0, 0, rgb[0], rgb[1], rgb[2]};
        uint8_t col[3];
        render_.lit_colour(info, rv, col);
        s.v.push_back(to_godot(x, y, z));
        s.c.push_back(Color(col[0] / 255.0f, col[1] / 255.0f, col[2] / 255.0f));
        s.uv.push_back(Vector2(uv[0] / 256.0f, uv[1] / 256.0f));
        s.uv2.push_back(Vector2(l, float(valor::LevelRender::light_index(info, rv) + 1)));
    };
    if (m.skinned()) {
        auto verts = valor::skin_vertices(m, a.inst.pose, a.inst.joints);
        for (const valor::SkinFace& f : m.faces) {
            Surface& s = surf[semi(f.tpage) ? 1 + blend_mode(f.tpage) : 0];
            float l = float(layer(f.tpage, f.clut));
            bool ok = true;
            for (int k = 0; k < 3; k++) ok &= f.bone[k] < verts.size() && f.vertex[k] < verts[f.bone[k]].size();
            if (!ok) continue;
            for (int k = 0; k < 3; k++) {
                const auto& v = verts[f.bone[k]][f.vertex[k]];
                corner(s, v[0], v[1], v[2], f.rgb[k], f.uv[k], l);
            }
        }
    } else {
        static const uint8_t grey[3] = {128, 128, 128};
        for (const valor::StaticFace& f : m.static_faces) {
            Surface& s = surf[semi(f.tpage) ? 1 + blend_mode(f.tpage) : 0];
            float l = float(layer(f.tpage, f.clut));
            bool ok = true;
            for (int k = 0; k < 3; k++) ok &= f.v[k] < m.static_vertices.size();
            if (!ok) continue;
            for (int k = 0; k < 3; k++) {
                const auto& v = m.static_vertices[f.v[k]];
                const uint8_t* rgb = f.v[k] < m.static_colours.size() ? m.static_colours[f.v[k]].data() : grey;
                corner(s, v[0], v[1], v[2], rgb, f.uv[k], l);
            }
        }
    }
    add_surfaces(mesh, surf);
    return mesh;
}

PackedVector3Array ValorGame::get_light_colours() const {
    PackedVector3Array out;
    for (const valor::LightState& l : session_.lights())
        out.push_back(Vector3((l.colour & 0xFF) / 255.0f, ((l.colour >> 8) & 0xFF) / 255.0f, ((l.colour >> 16) & 0xFF) / 255.0f));
    return out;
}

Dictionary ValorGame::get_level_info() const {
    const valor::LevelInfo& info = session_.info();
    Dictionary d;
    d["mission"] = info.mission;
    d["level"] = info.level;
    d["chunks"] = int(session_.world().chunks.size());
    d["fog_near"] = info.fog_near;
    d["fog_color"] = Color(info.fog_rgb[0] / 255.0f, info.fog_rgb[1] / 255.0f, info.fog_rgb[2] / 255.0f);
    d["sky_color"] = Color(info.sky_rgb[0] / 255.0f, info.sky_rgb[1] / 255.0f, info.sky_rgb[2] / 255.0f);
    d["view_distance"] = int64_t(info.view_distance);
    d["lights"] = int64_t(info.lights.size());
    return d;
}

void ValorGame::tick(int64_t buttons, int64_t forward, int64_t strafe, int64_t turn, int64_t pitch) {
    if (!loaded_) return;
    valor::Controls& c = session_.player().controls;
    c.pad.prev = c.pad.now;
    c.pad.now = 0xFFFF0000u | (uint32_t(buttons) & 0xFFFF);
    const int64_t axes[4] = {forward, strafe, pitch, turn};
    for (int r = 0; r < 4; r++) c.axis_override[r] = true, c.axis_value[r] = valor::s32(axes[r]);
    session_.vblanks(2);  // a 30 Hz tick is two vblanks
    session_.tick();
    session_.end_frame();
    // The actors' animations, a frame per tick, looping.
    for (Actor& a : actors_) {
        if (!a.model->skinned()) continue;
        if (!a.inst.advance()) a.inst.start(a.anim, 0, -1, 2);
        a.inst.build(models_);
    }
}

void ValorGame::place(double x, double y, double z, double yaw, double pitch) {
    // Game units (y down) and game angles (4096 per turn).
    int32_t pos[3] = {valor::s32(int64_t(x * 65536.0)), valor::s32(int64_t(y * 65536.0)),
                      valor::s32(int64_t(z * 65536.0))};
    session_.player().place(pos, valor::s32(int64_t(yaw * 65536.0)), valor::s32(int64_t(pitch * 65536.0)), true);
}

Vector3 ValorGame::get_eye_position() const {
    const valor::Player& player = session_.player();
    const valor::Walker& w = player.w;
    return to_godot(w.pos[0] / 65536.0, w.pos[1] / 65536.0 + player.eye_offset, w.pos[2] / 65536.0);
}

double ValorGame::get_yaw() const { return -(session_.player().yaw() / 65536.0) * kAngle; }
double ValorGame::get_pitch() const { return (session_.player().pitch() / 65536.0) * kAngle; }

Dictionary ValorGame::get_state() const {
    const valor::Player& player = session_.player();
    const valor::Walker& w = player.w;
    Dictionary d;
    d["tick"] = int64_t(player.ticks);
    d["x"] = w.pos[0] / 65536.0;
    d["y"] = w.pos[1] / 65536.0;
    d["z"] = w.pos[2] / 65536.0;
    d["vy"] = w.vy / 65536.0;
    d["ground"] = bool(w.ground);
    d["zone"] = int64_t(w.zone);
    d["crouched"] = player.crouched();
    d["yaw"] = player.yaw() / 65536.0;
    Array slots;
    for (int c : session_.streaming().slot_chunk) slots.push_back(c);
    d["chunk_slots"] = slots;
    return d;
}

}  // namespace godot
