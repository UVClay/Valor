#include "valor_game.h"

#include "valor/disc.h"
#include "valor/sim/engine.h"
#include "valor/sim/feed.h"
#include "valor/sim/load.h"
#include "valor/sim/views.h"

#include <algorithm>
#include <cmath>
#include <map>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
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
    ClassDB::bind_method(D_METHOD("mount_disc", "path"), &ValorGame::mount_disc);
    ClassDB::bind_method(D_METHOD("get_disc_image"), &ValorGame::get_disc_image);
    ClassDB::bind_method(D_METHOD("get_disc_data_dir"), &ValorGame::get_disc_data_dir);
    ClassDB::bind_method(D_METHOD("get_error"), &ValorGame::get_error);
    ClassDB::bind_method(D_METHOD("build_world_mesh"), &ValorGame::build_world_mesh);
    ClassDB::bind_method(D_METHOD("get_texture_pages"), &ValorGame::get_texture_pages);
    ClassDB::bind_method(D_METHOD("spawn_actors"), &ValorGame::spawn_actors);
    ClassDB::bind_method(D_METHOD("get_actor_count"), &ValorGame::get_actor_count);
    ClassDB::bind_method(D_METHOD("get_actor_transform", "i"), &ValorGame::get_actor_transform);
    ClassDB::bind_method(D_METHOD("is_actor_skinned", "i"), &ValorGame::is_actor_skinned);
    ClassDB::bind_method(D_METHOD("build_actor_mesh", "i"), &ValorGame::build_actor_mesh);
    ClassDB::bind_method(D_METHOD("add_actor", "model", "pos", "angles", "anim", "frame"), &ValorGame::add_actor);
    ClassDB::bind_method(D_METHOD("remove_skinned_actors"), &ValorGame::remove_skinned_actors);
    ClassDB::bind_method(D_METHOD("get_level_info"), &ValorGame::get_level_info);
    ClassDB::bind_method(D_METHOD("place", "x", "y", "z", "yaw", "pitch"), &ValorGame::place);
    ClassDB::bind_method(D_METHOD("tick", "buttons", "forward", "strafe", "turn", "pitch"), &ValorGame::tick);
    ClassDB::bind_method(D_METHOD("get_eye_position"), &ValorGame::get_eye_position);
    ClassDB::bind_method(D_METHOD("get_yaw"), &ValorGame::get_yaw);
    ClassDB::bind_method(D_METHOD("get_pitch"), &ValorGame::get_pitch);
    ClassDB::bind_method(D_METHOD("get_state"), &ValorGame::get_state);
    ClassDB::bind_method(D_METHOD("get_light_colours"), &ValorGame::get_light_colours);
    ClassDB::bind_method(D_METHOD("start_engine", "ammo"), &ValorGame::start_engine);
    ClassDB::bind_method(D_METHOD("is_engine_running"), &ValorGame::is_engine_running);
    ClassDB::bind_method(D_METHOD("engine_tick", "buttons", "forward", "strafe", "turn", "pitch"), &ValorGame::engine_tick);
    ClassDB::bind_method(D_METHOD("engine_pause_frame"), &ValorGame::engine_pause_frame);
    ClassDB::bind_method(D_METHOD("get_engine_actors"), &ValorGame::get_engine_actors);
    ClassDB::bind_method(D_METHOD("take_level_change"), &ValorGame::take_level_change);
    ClassDB::bind_method(D_METHOD("start_engine_game", "game"), &ValorGame::start_engine_game);
    ClassDB::bind_method(D_METHOD("is_level_over"), &ValorGame::is_level_over);
    ClassDB::bind_method(D_METHOD("get_level_results"), &ValorGame::get_level_results);
    ClassDB::bind_method(D_METHOD("get_engine_game"), &ValorGame::get_engine_game);
    ClassDB::bind_method(D_METHOD("continue_engine", "game"), &ValorGame::continue_engine);
    ClassDB::bind_method(D_METHOD("get_fade"), &ValorGame::get_fade);
    ClassDB::bind_method(D_METHOD("is_campaign_done"), &ValorGame::is_campaign_done);
    ClassDB::bind_method(D_METHOD("debug_end_level"), &ValorGame::debug_end_level);
    ClassDB::bind_method(D_METHOD("get_engine_voices"), &ValorGame::get_engine_voices);
    ClassDB::bind_method(D_METHOD("get_engine_sprites"), &ValorGame::get_engine_sprites);
    ClassDB::bind_method(D_METHOD("get_engine_effects"), &ValorGame::get_engine_effects);
    ClassDB::bind_method(D_METHOD("set_engine_replay", "log_path"), &ValorGame::set_engine_replay);
    ClassDB::bind_method(D_METHOD("build_engine_actor_mesh", "handle"), &ValorGame::build_engine_actor_mesh);
    ClassDB::bind_method(D_METHOD("build_skin_mesh", "model_hash"), &ValorGame::build_skin_mesh);
    ClassDB::bind_method(D_METHOD("setup_skeleton", "model_hash", "skeleton"), &ValorGame::setup_skeleton);
    ClassDB::bind_method(D_METHOD("pose_skeleton", "handle", "skeleton"), &ValorGame::pose_skeleton);
    ClassDB::bind_static_method("ValorGame", D_METHOD("to_godot", "x", "y", "z"), &ValorGame::to_godot);
    BIND_CONSTANT(kUnitsPerMetre);
}

ValorGame::ValorGame() = default;
ValorGame::~ValorGame() = default;

Vector3 ValorGame::to_godot(double x, double y, double z) {
    // y down, z forward at yaw 0 -> Godot's y up, -z forward: a half turn about x.
    return Vector3(real_t(x / kUnitsPerMetre), real_t(-y / kUnitsPerMetre), real_t(-z / kUnitsPerMetre));
}

// The repository's root, three levels above the project (engine/godot/project); a recording's
// inputs (build/engine) and the recompiler's tables are under it, the extracted files too.
std::string ValorGame::repo_root() const {
    String p = ProjectSettings::get_singleton()->globalize_path("res://").path_join("../../..").simplify_path();
    return p.utf8().get_data();
}

bool ValorGame::mount_disc(const String& path) {
    std::string err;
    if (valor::disc_mounted() && valor::disc_image() == path.utf8().get_data()) return true;
    if (!valor::disc_mount(path.utf8().get_data(), &err)) {
        error_ = String(err.c_str());
        return false;
    }
    return true;
}

String ValorGame::get_disc_image() const { return String(valor::disc_image().c_str()); }

bool ValorGame::load_level(const String& data_dir, int mission, int level) {
    loaded_ = false;
    std::string dir = data_dir.utf8().get_data(), err;
    if (!session_.load(dir, mission, level, &err) || !render_.load(dir, session_.info(), &err) ||
        !models_.load(dir, mission, level, &err)) {
        error_ = String(err.c_str());
        return false;
    }
    data_dir_ = dir, mission_ = mission, level_ = level;
    engine_.reset();
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
    PackedInt32Array bones;      // 4 per vertex (skinned meshes)
    PackedFloat32Array weights;  // 4 per vertex
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
        if (!surf[i].bones.is_empty()) {
            arrays[Mesh::ARRAY_BONES] = surf[i].bones;
            arrays[Mesh::ARRAY_WEIGHTS] = surf[i].weights;
        }
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
        Actor a{int(i), m, {}, -1, {r.pos[0], r.pos[1], r.pos[2]}, {r.ang[0], r.ang[1], r.ang[2]}};
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
    const Actor& a = actors_[i];
    // The model turned by RotMatrixYXZ of its angles and placed at its position; in Godot's axes
    // (y and z flipped) the basis is C M C.
    int16_t m[3][3];
    valor::rot_yxz(a.ang[0], a.ang[1], a.ang[2], m);
    const float c[3] = {1, -1, -1};
    Basis b;
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) b[row][col] = real_t(c[row] * c[col] * m[row][col] / 4096.0);
    return Transform3D(b, to_godot(a.pos[0], a.pos[1], a.pos[2]));
}

int ValorGame::add_actor(int64_t model, Vector3 pos, Vector3i angles, int anim, int frame) {
    const valor::Model* m = models_.by_hash(uint32_t(model));
    if (!m) return -1;
    Actor a{-1, m, {}, anim, {int32_t(pos.x), int32_t(pos.y), int32_t(pos.z)},
            {int16_t(angles.x), int16_t(angles.y), int16_t(angles.z)}, true};
    a.inst.model = m;
    if (m->skinned()) {
        a.inst.start(anim, frame, frame, 2);
        a.inst.build(models_);
    }
    actors_.push_back(std::move(a));
    return int(actors_.size()) - 1;
}

void ValorGame::remove_skinned_actors() {
    std::vector<Actor> keep;
    for (Actor& a : actors_)
        if (!a.model->skinned()) keep.push_back(std::move(a));
    actors_ = std::move(keep);
}

Ref<ArrayMesh> ValorGame::build_actor_mesh(int i) {
    if (i < 0 || size_t(i) >= actors_.size()) {
        Ref<ArrayMesh> mesh;
        mesh.instantiate();
        return mesh;
    }
    Actor& a = actors_[i];
    return mesh_of(*a.model, &a.inst.pose, &a.inst.joints);
}

// A model's mesh: skinned by the pose (bone quaternions and joints) when it has a skeleton, else
// its static faces.
Ref<ArrayMesh> ValorGame::mesh_of(const valor::Model& m, const std::vector<valor::Quat>* pose,
                                  const std::vector<std::array<int16_t, 3>>* joints) {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
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
        if (!pose || !joints || pose->empty()) return mesh;
        auto verts = valor::skin_vertices(m, *pose, *joints);
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
        if (!a.model->skinned() || a.frozen) continue;
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
    if (engine_) {
        // the camera's eye (the death camera's own when dying)
        auto p = valor::sim::view::camera_view(engine_->m).eye();
        return to_godot(p[0], p[1], p[2]);
    }
    const valor::Player& player = session_.player();
    const valor::Walker& w = player.w;
    return to_godot(w.pos[0] / 65536.0, w.pos[1] / 65536.0 + player.eye_offset, w.pos[2] / 65536.0);
}

double ValorGame::get_yaw() const {
    if (engine_) {
        auto c = valor::sim::view::camera_view(engine_->m);
        if (c.death()) {  // the death camera looks from its eye to its look point
            auto a = c.eye(), b = c.look();
            return -std::atan2(b[0] - a[0], b[2] - a[2]);
        }
        return -(valor::sim::view::player(engine_->m).look_yaw() / 65536.0) * kAngle;
    }
    return -(session_.player().yaw() / 65536.0) * kAngle;
}
double ValorGame::get_pitch() const {
    if (engine_) {
        auto c = valor::sim::view::camera_view(engine_->m);
        if (c.death()) {
            auto a = c.eye(), b = c.look();
            double h = std::hypot(b[0] - a[0], b[2] - a[2]);
            return std::atan2(-(b[1] - a[1]), h);  // y down
        }
        return (valor::sim::view::player(engine_->m).look_pitch() / 65536.0) * kAngle;
    }
    return (session_.player().pitch() / 65536.0) * kAngle;
}

Dictionary ValorGame::get_state() const {
    if (engine_) {
        namespace view = valor::sim::view;
        const valor::sim::Mem& m = engine_->m;
        view::Entity sol = view::soldier(m);
        view::Player p = view::player(m);
        Dictionary d;
        d["tick"] = int64_t(view::tick(m));
        auto pos = sol.a ? sol.position() : std::array<double, 3>{0, 0, 0};
        d["x"] = pos[0], d["y"] = pos[1], d["z"] = pos[2];
        d["ground"] = sol.a && sol.on_ground();
        d["crouched"] = p.a && (p.state() & 2);
        d["zone"] = 0;
        d["health"] = sol.a ? sol.health() / 65536.0 : 0.0;
        d["weapon"] = p.a ? int(p.weapon()) : -1;
        d["clip"] = p.a ? int(p.clip()) : 0;
        d["reserve"] = p.a ? int(p.reserve()) : 0;
        // For the presentation layer (present/): the slot (the ammo icon), aim mode (the reticle)
        // and the first-person weapon model's animation and frame (the view model).
        d["slot"] = p.a ? int(p.slot()) : -1;
        d["aiming"] = p.a && (p.state() & 0x20);
        view::Instance wi = sol.a ? view::instance(sol) : view::Instance{};
        d["weapon_model"] = int64_t(sol.a ? sol.model_hash() : 0);
        d["weapon_anim"] = wi.a ? int(wi.anim()) : -1;
        d["weapon_frame"] = wi.a ? int(wi.frame()) : 0;
        // The soldier's body angles (4096 per turn), which the weapon model is drawn with: they trail
        // the view in the aiming stances (movement.md).
        d["body_pitch"] = sol.a ? double(sol.pitch()) / 65536.0 : 0.0;
        d["body_yaw"] = sol.a ? double(sol.yaw()) / 65536.0 : 0.0;
        // The scope and zoom (hud.md), the muzzle flash, the music, the objectives (the pause menu's).
        d["scoped"] = p.a && (p.state() & 0x100);
        d["zoom"] = p.a ? int64_t(p.zoom()) : 400;
        d["flash"] = p.a ? int(p.flash()) : 0;
        // the crosshair and the reticle (hud.md, Aim reticle): the stance, the crosshair, the kept
        // difference to the aim point's projection
        d["stance"] = p.a ? int(p.stance()) : 0;
        d["crosshair"] = p.a ? Vector2(p.crosshair_x(), p.crosshair_y()) : Vector2(256, 131);
        d["reticle_offset"] = p.a ? Vector2(p.reticle_dx(), p.reticle_dy()) : Vector2();
        d["player_flags"] = p.a ? int64_t(p.state()) : 0;
        d["music"] = view::music_started(m);
        Array objs;
        for (const view::Objective& o : view::objectives(m)) {
            Dictionary od;
            od["id"] = int64_t(o.id);
            od["done"] = o.done;
            od["text"] = String(o.text.c_str());
            objs.push_back(od);
        }
        d["objectives"] = objs;
        // the text list (hud.md): {text, x, y (512 x 240), color, additive, life}, and the hit
        // wedges' timers (right, left, front, behind: 40 when hit)
        Array texts;
        for (const view::Text& t : view::texts(m)) {
            Dictionary td;
            td["text"] = String(t.text.c_str());
            td["x"] = t.x;
            td["y"] = t.y;
            td["color"] = Color(t.rgb[0] / 255.0f, t.rgb[1] / 255.0f, t.rgb[2] / 255.0f);
            td["additive"] = t.additive;
            td["life"] = t.life;
            texts.push_back(td);
        }
        d["texts"] = texts;
        auto ht = view::hit_timers(m);
        d["hit_wedges"] = PackedInt32Array({ht[0], ht[1], ht[2], ht[3]});
        auto fc = view::fade(m);
        d["fade"] = float(std::max({fc[0], fc[1], fc[2]})) / 255.0f;  // hud.md's fades: the black's opacity now
        int alive = 0, dead = 0;
        for (const view::Entity& e : view::entities(m))
            if (e.type() == 0x1402 || e.type() == 0x1405) (e.dead() ? dead : alive)++;
        d["enemies"] = alive;
        d["enemies_dead"] = dead;
        d["kills"] = view::stat(m, 4);  // g_stat_kills: the pause menu's TOTAL ENEMIES NEUTRALIZED
        return d;
    }
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

// ---------------------------------------------------------------- the engine, live

bool ValorGame::start_engine(int ammo) {
    return start_engine_with(valor::sim::game_defaults(mission_, level_, ammo), true);
}

bool ValorGame::start_engine_game(const PackedByteArray& game) {
    std::vector<uint8_t> g(size_t(game.size()));
    if (g.size() != 0x780) {
        error_ = "the save data must be 0x780 bytes";
        return false;
    }
    memcpy(g.data(), game.ptr(), g.size());
    return start_engine_with(g, false);
}

bool ValorGame::start_engine_with(const std::vector<uint8_t>& game, bool auto_advance) {
    if (!loaded_) return false;
    // the repository root (a recording's inputs; the game's files come from the disc image or
    // <root>/extracted/moh)
    std::string root = repo_root();
    feed_ = std::make_unique<valor::sim::Feed>();
    feed_->live = true;
    std::string lv = std::to_string(mission_) + ":" + std::to_string(level_);
    engine_ = std::make_unique<valor::sim::Engine>(root, lv, *feed_);
    valor::sim::LevelBuild lb;
    lb.rand_seed = 0x2468ACE1;
    std::string err;
    engine_->auto_advance = auto_advance;
    if (!engine_->load_disc(game, lb, &err)) {
        error_ = String(("the engine: " + err).c_str());
        engine_.reset();
        return false;
    }
    engine_->m.w8(0x80093B78, 1);  // an analog pad
    // drawn and culled by the original's view test (entity_in_view) at the recording's
    // projection, whatever field of view Godot's camera is given
    engine_->start_live(nullptr);
    prev_buttons_ = 0;
    return true;
}

void ValorGame::engine_tick(int64_t buttons, int64_t forward, int64_t strafe, int64_t turn, int64_t pitch) {
    if (!engine_) return;
    uint32_t now = uint32_t(buttons) & 0xFFFF;
    feed_->live_pad = {now, 0, prev_buttons_, 0};
    prev_buttons_ = now;
    feed_->live_role = {forward, strafe, pitch, turn};
    feed_->live_role_alt = {0, 0, 0, 0};
    if (replay_) {
        valor::sim::I t = engine_->tick_no + 1;
        auto it = replay_->pad.find({engine_->level, t});
        if (it != replay_->pad.end()) feed_->live_pad = it->second;
        for (int r = 0; r < 4; r++) {
            feed_->live_role[size_t(r)] = replay_->role(engine_->level, t, 0, r);
            feed_->live_role_alt[size_t(r)] = replay_->role_alt(engine_->level, t, 0, r);
        }
    }
    engine_->live_frame();
    sync_engine_level();
    session_.vblanks(2);  // the world's animated lights
    session_.end_frame();
}

void ValorGame::engine_pause_frame() {
    if (!engine_) return;
    engine_->live_paused_frame();
    sync_engine_level();
}

// The engine went on to another level: its files for drawing (the engine itself is built).
void ValorGame::sync_engine_level() {
    if (!engine_ || engine_->level_changes == level_changes_seen_) return;
    level_changes_seen_ = engine_->level_changes;
    std::string err;
    int mi = engine_->mission_n, li = engine_->level_n;
    if (session_.load(data_dir_, mi, li, &err) && render_.load(data_dir_, session_.info(), &err) && models_.load(data_dir_, mi, li, &err)) {
        mission_ = mi, level_ = li;
        pages_.unref();
        layer_of_.clear();
        page_images_.clear();
        actors_.clear();
        level_changed_ = true;
        skin_meshes_.clear();
    } else
        error_ = String(("the next level: " + err).c_str());
}

bool ValorGame::is_level_over() const { return engine_ && engine_->level_over; }

Dictionary ValorGame::get_level_results() const {
    Dictionary d;
    if (!engine_) return d;
    const valor::sim::LevelResults& r = engine_->results;
    d["exit"] = int64_t(r.exit_code);
    d["objectives"] = int64_t(r.objectives);
    d["done"] = int64_t(r.done);
    d["primary_complete"] = r.primary_complete;
    d["health"] = int64_t(r.health);
    PackedInt32Array ammo, wk, stats;
    for (auto v : r.ammo) ammo.push_back(int32_t(v));
    for (auto v : r.weapon_kills) wk.push_back(int32_t(v));
    for (auto v : r.stats) stats.push_back(int32_t(v));
    d["ammo"] = ammo;
    d["weapon_kills"] = wk;
    d["stats"] = stats;
    return d;
}

PackedByteArray ValorGame::get_engine_game() const {
    PackedByteArray out;
    if (!engine_) return out;
    std::vector<uint8_t> g = engine_->save_data();
    out.resize(int64_t(g.size()));
    memcpy(out.ptrw(), g.data(), g.size());
    return out;
}

bool ValorGame::continue_engine(const PackedByteArray& game) {
    if (!engine_ || game.size() != 0x780) return false;
    std::vector<uint8_t> g(0x780);
    memcpy(g.data(), game.ptr(), g.size());
    std::string err;
    if (!engine_->load_next(g, &err)) {
        error_ = String(("the next level: " + err).c_str());
        return false;
    }
    sync_engine_level();
    return true;
}

Color ValorGame::get_fade() const {
    if (!engine_) return Color(0, 0, 0, 0);
    auto c = valor::sim::view::fade(engine_->m);
    // subtractive: a channel's colour darkens it; the fades are grey levels, drawn as black
    int k = std::max({c[0], c[1], c[2]});
    return Color(0, 0, 0, float(k) / 255.0f);
}

bool ValorGame::take_level_change() {
    bool c = level_changed_;
    level_changed_ = false;
    return c;
}

void ValorGame::debug_end_level() {
    if (engine_) engine_->m.w32(0x8009C840, engine_->m.u32(0x8009C840) | 0x24);  // level_set(3, 1)
}

bool ValorGame::is_campaign_done() const { return engine_ && engine_->campaign_done; }

bool ValorGame::set_engine_replay(const String& log_path) {
    if (log_path.is_empty()) {  // back to the host's input
        replay_.reset();
        return true;
    }
    replay_ = std::make_unique<valor::sim::Feed>();
    // the recompiler's tables under the repository root
    if (!replay_->load(repo_root(), log_path.utf8().get_data())) {
        replay_.reset();
        error_ = "can't read the replay " + log_path;
        return false;
    }
    return true;
}

Array ValorGame::get_engine_voices() const {
    Array out;
    if (!engine_) return out;
    namespace view = valor::sim::view;
    for (const view::Voice& v : view::voices(engine_->m)) {
        Dictionary d;
        d["voice"] = v.index;
        d["status"] = v.status();
        d["flags"] = int64_t(v.flags());
        d["handle"] = int64_t(v.handle());
        d["bank"] = int(v.bank());
        d["sound"] = int(v.sound());
        d["loop"] = int(v.loop());
        d["cls"] = int(v.cls());
        d["volume"] = int64_t(v.volume());
        d["pitch"] = int64_t(v.pitch());
        d["age"] = int(v.age());
        d["fade_out"] = int(v.fade_out());
        d["pos"] = Vector3(v.x(), v.y(), v.z());
        out.push_back(d);
    }
    return out;
}

Array ValorGame::get_engine_sprites() const {
    Array out;
    if (!engine_) return out;
    for (const valor::sim::view::Sprite& sp : valor::sim::view::sprites(engine_->m)) {
        Dictionary d;
        d["pos"] = to_godot(sp.pos[0], sp.pos[1], sp.pos[2]);
        d["half"] = Vector2(real_t(sp.half_w / kUnitsPerMetre), real_t(sp.half_h / kUnitsPerMetre));
        d["color"] = Color(sp.rgb[0] / 128.0f, sp.rgb[1] / 128.0f, sp.rgb[2] / 128.0f);
        d["rot"] = Vector3i(sp.rot[0], sp.rot[1], sp.rot[2]);
        d["billboard"] = sp.billboard;
        d["triangle"] = sp.triangle;
        // the texture as a 4-bit page, CLUT and UV (tpage x / 64 | y / 256 << 4)
        d["tpage"] = int((sp.tex_x / 64) | ((sp.tex_y / 256) << 4));
        d["clut"] = int((sp.clut_x / 16) | (sp.clut_y << 6));
        d["u"] = int((sp.tex_x % 64) * 4);
        d["v"] = int(sp.tex_y % 256);
        d["w"] = int(sp.w);
        d["h"] = int(sp.h);
        d["additive"] = sp.additive;
        out.push_back(d);
    }
    return out;
}

Dictionary ValorGame::get_engine_effects() const {
    Dictionary d;
    if (!engine_) return d;
    namespace view = valor::sim::view;
    const valor::sim::Mem& m = engine_->m;
    auto col = [](const uint8_t* c) { return Color(c[0] / 128.0f, c[1] / 128.0f, c[2] / 128.0f); };
    Array streaks;
    for (const view::Streak& s : view::streaks(m)) {
        Dictionary e;
        e["p0"] = to_godot(s.p0[0], s.p0[1], s.p0[2]);
        e["p1"] = to_godot(s.p1[0], s.p1[1], s.p1[2]);
        e["c0"] = col(s.c0), e["c1"] = col(s.c1);
        e["tpage"] = int((s.tex_x / 64) | ((s.tex_y / 256) << 4));
        e["clut"] = int((s.clut_x / 16) | (s.clut_y << 6));
        e["u"] = int((s.tex_x % 64) * 4), e["v"] = int(s.tex_y % 256);
        e["w"] = int(s.w), e["h"] = int(s.h);
        e["mode"] = s.mode, e["quad"] = s.quad;
        streaks.push_back(e);
    }
    d["streaks"] = streaks;
    Array puffs;
    for (const view::Puff& f : view::puffs(m)) {
        Dictionary e;
        e["pos"] = to_godot(f.pos[0], f.pos[1], f.pos[2]);
        e["scale"] = f.scale, e["angle"] = int(f.angle);
        e["color"] = col(f.rgb), e["drop"] = f.drop, e["on_floor"] = f.on_floor;
        puffs.push_back(e);
    }
    d["puffs"] = puffs;
    Array shots;
    for (const view::Shot& s : view::enemy_shots(m)) {
        Dictionary e;
        e["handle"] = int64_t(s.handle);
        e["from"] = to_godot(s.from[0], s.from[1], s.from[2]);
        shots.push_back(e);
    }
    d["enemy_shots"] = shots;
    view::ExplosionLight l = view::explosion_light(m);
    if (l.on) {
        Dictionary e;
        e["pos"] = to_godot(l.pos[0], l.pos[1], l.pos[2]);
        e["color"] = Color(l.rgb[0] / 255.0f, l.rgb[1] / 255.0f, l.rgb[2] / 255.0f);
        e["radius"] = l.radius, e["radius2"] = l.radius2, e["scorch"] = l.scorch;
        d["light"] = e;
    }
    return d;
}

Dictionary ValorGame::get_engine_actors() const {
    Dictionary out;
    if (!engine_) return out;
    namespace view = valor::sim::view;
    const valor::sim::Mem& m = engine_->m;
    uint32_t player = view::soldier(m).a;
    for (const view::Entity& e : view::entities(m)) {
        if (e.removed() || e.a == player || e.type() == 2 || e.type() == 3) continue;
        uint32_t hash = e.model_hash();
        const valor::Model* model = hash ? models_.by_hash(hash) : nullptr;
        if (!model || (!model->skinned() && model->static_faces.empty())) continue;
        Dictionary d;
        d["model"] = int64_t(hash);
        d["skinned"] = model->skinned();
        d["type"] = int64_t(e.type());
        d["dead"] = e.dead();
        auto ang = e.angles();
        int16_t r[3][3];
        valor::rot_yxz(ang[0], ang[1], ang[2], r);
        const float c[3] = {1, -1, -1};
        Basis b;
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 3; col++) b[row][col] = real_t(c[row] * c[col] * r[row][col] / 4096.0);
        auto p = e.position();
        d["transform"] = Transform3D(b, to_godot(p[0], p[1], p[2]));
        out[int64_t(e.handle())] = d;
    }
    return out;
}

// The model's faces in its bones' own space (the vertices before R_b v + joint_b), each corner
// bound to its bone at weight 1: Godot's skinning then places them on the GPU.
Ref<ArrayMesh> ValorGame::build_skin_mesh(int64_t model_hash) {
    auto it = skin_meshes_.find(uint32_t(model_hash));
    if (it != skin_meshes_.end()) return it->second;
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    const valor::Model* m = models_.by_hash(uint32_t(model_hash));
    if (!m || !m->skinned()) return mesh;
    Surface surf[5];
    const valor::LevelInfo& info = session_.info();
    for (const valor::SkinFace& f : m->faces) {
        bool ok = true;
        for (int k = 0; k < 3; k++) ok &= f.bone[k] < m->bone_vertices.size() && f.vertex[k] < m->bone_vertices[f.bone[k]].size();
        if (!ok) continue;
        Surface& s = surf[semi(f.tpage) ? 1 + blend_mode(f.tpage) : 0];
        float l = float(layer(f.tpage, f.clut));
        for (int k = 0; k < 3; k++) {
            const auto& v = m->bone_vertices[f.bone[k]][f.vertex[k]];
            valor::RenderVertex rv{0, 0, 0, f.rgb[k][0], f.rgb[k][1], f.rgb[k][2]};
            uint8_t col[3];
            render_.lit_colour(info, rv, col);
            s.v.push_back(to_godot(v[0], v[1], v[2]));
            s.c.push_back(Color(col[0] / 255.0f, col[1] / 255.0f, col[2] / 255.0f));
            s.uv.push_back(Vector2(f.uv[k][0] / 256.0f, f.uv[k][1] / 256.0f));
            s.uv2.push_back(Vector2(l, float(valor::LevelRender::light_index(info, rv) + 1)));
            s.bones.append_array(PackedInt32Array({int32_t(f.bone[k]), 0, 0, 0}));
            s.weights.append_array(PackedFloat32Array({1.0f, 0.0f, 0.0f, 0.0f}));
        }
    }
    add_surfaces(mesh, surf);
    skin_meshes_[uint32_t(model_hash)] = mesh;
    return mesh;
}

Ref<Skin> ValorGame::setup_skeleton(int64_t model_hash, Object* skeleton) {
    Ref<Skin> skin;
    skin.instantiate();
    Skeleton3D* sk = Object::cast_to<Skeleton3D>(skeleton);
    const valor::Model* m = models_.by_hash(uint32_t(model_hash));
    if (!sk || !m) return skin;
    sk->clear_bones();
    int n = int(m->bone_vertices.size());
    skin->set_bind_count(n);
    for (int b = 0; b < n; b++) {
        sk->add_bone(String("b") + String::num_int64(b));
        skin->set_bind_bone(b, b);
        skin->set_bind_pose(b, Transform3D());
    }
    return skin;
}

// Each bone: its quaternion's matrix in Godot's axes (C R C, C = diag(1, -1, -1)) and its joint,
// as skin_vertices places the bone's vertices.
bool ValorGame::pose_skeleton(int64_t handle, Object* skeleton) {
    Skeleton3D* sk = Object::cast_to<Skeleton3D>(skeleton);
    if (!engine_ || !sk) return false;
    namespace view = valor::sim::view;
    for (const view::Entity& e : view::entities(engine_->m)) {
        if (int64_t(e.handle()) != handle) continue;
        view::Instance in = view::instance(e);
        auto quats = in.quaternions();
        auto joints = in.joints();
        const float c[3] = {1, -1, -1};
        int n = sk->get_bone_count();
        for (int b = 0; b < n; b++) {
            int16_t r[3][3] = {{0x1000, 0, 0}, {0, 0x1000, 0}, {0, 0, 0x1000}};
            if (size_t(b) < quats.size()) valor::quat_matrix(quats[size_t(b)].data(), r);
            Basis basis;
            for (int row = 0; row < 3; row++)
                for (int col = 0; col < 3; col++) basis[row][col] = real_t(c[row] * c[col] * r[row][col] / 4096.0);
            std::array<int16_t, 3> j = size_t(b) < joints.size() ? joints[size_t(b)] : std::array<int16_t, 3>{0, 0, 0};
            sk->set_bone_pose(b, Transform3D(basis, to_godot(j[0], j[1], j[2])));
        }
        return true;
    }
    return false;
}

Ref<ArrayMesh> ValorGame::build_engine_actor_mesh(int64_t handle) {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    if (!engine_) return mesh;
    namespace view = valor::sim::view;
    const valor::sim::Mem& m = engine_->m;
    for (const view::Entity& e : view::entities(m)) {
        if (int64_t(e.handle()) != handle) continue;
        const valor::Model* model = models_.by_hash(e.model_hash());
        if (!model) return mesh;
        view::Instance in = view::instance(e);
        std::vector<valor::Quat> pose;
        for (const auto& q : in.quaternions()) pose.push_back(q);
        std::vector<std::array<int16_t, 3>> joints = in.joints();
        return mesh_of(*model, &pose, &joints);
    }
    return mesh;
}

}  // namespace godot
