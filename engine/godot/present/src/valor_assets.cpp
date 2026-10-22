#include "valor_assets.h"

#include "valor/disc.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <cstdio>
#include <cstring>
#include <functional>

#include "adpcm.h"
#include "valor/anim.h"
#include "valor/io.h"

using namespace godot;

namespace {

constexpr uint32_t kOverlay = 0x80039D90;  // LEVEL.BIN's load address (tools/recon/fn.py)
constexpr size_t kBody = 0x800;            // BSD offsets count from the file's sector 1

uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return o + 4 <= d.size() ? uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24 : 0;
}
uint16_t rd16(const std::vector<uint8_t>& d, size_t o) {
    return o + 2 <= d.size() ? uint16_t(d[o] | d[o + 1] << 8) : 0;
}

Vector3 to_godot(double x, double y, double z) {
    return Vector3(real_t(x / ValorAssets::kUnitsPerMetre), real_t(-y / ValorAssets::kUnitsPerMetre),
                   real_t(-z / ValorAssets::kUnitsPerMetre));
}

Ref<AudioStreamWAV> wav(const std::vector<int16_t>& pcm, int rate, int loop_from) {
    Ref<AudioStreamWAV> s;
    s.instantiate();
    PackedByteArray bytes;
    bytes.resize(int64_t(pcm.size() * 2));
    if (!pcm.empty()) memcpy(bytes.ptrw(), pcm.data(), pcm.size() * 2);
    s->set_format(AudioStreamWAV::FORMAT_16_BITS);
    s->set_mix_rate(rate);
    s->set_stereo(false);
    s->set_data(bytes);
    if (loop_from >= 0 && size_t(loop_from) < pcm.size()) {
        s->set_loop_mode(AudioStreamWAV::LOOP_FORWARD);
        s->set_loop_begin(loop_from);
        s->set_loop_end(int(pcm.size()));
    }
    return s;
}

}  // namespace

void ValorAssets::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load", "data_dir", "mission", "level", "american"), &ValorAssets::load);
    ClassDB::bind_method(D_METHOD("mount_disc", "path"), &ValorAssets::mount_disc);
    ClassDB::bind_method(D_METHOD("get_error"), &ValorAssets::get_error);
    ClassDB::bind_method(D_METHOD("get_level_info"), &ValorAssets::get_level_info);
    ClassDB::bind_method(D_METHOD("level_bin", "addr", "len"), &ValorAssets::level_bin);
    ClassDB::bind_method(D_METHOD("level_string", "addr"), &ValorAssets::level_string);
    ClassDB::bind_method(D_METHOD("page_image", "tpage", "clut"), &ValorAssets::page_image);
    ClassDB::bind_method(D_METHOD("region_image", "tpage", "clut", "u", "v", "w", "h"), &ValorAssets::region_image);
    ClassDB::bind_method(D_METHOD("get_bank_count"), &ValorAssets::get_bank_count);
    ClassDB::bind_method(D_METHOD("get_sound_count", "bank"), &ValorAssets::get_sound_count);
    ClassDB::bind_method(D_METHOD("get_sound", "bank", "sound"), &ValorAssets::get_sound);
    ClassDB::bind_method(D_METHOD("get_music", "ambience_only"), &ValorAssets::get_music);
    ClassDB::bind_method(D_METHOD("get_model_bank", "hash"), &ValorAssets::get_model_bank);
    ClassDB::bind_method(D_METHOD("get_anim_frames", "hash", "anim"), &ValorAssets::get_anim_frames);
    ClassDB::bind_method(D_METHOD("build_model_mesh", "hash", "anim", "frame"), &ValorAssets::build_model_mesh);
    ClassDB::bind_method(D_METHOD("get_texture_pages"), &ValorAssets::get_texture_pages);
    ClassDB::bind_method(D_METHOD("pvs_load"), &ValorAssets::pvs_load);
    ClassDB::bind_method(D_METHOD("pvs_leaf_ids", "surface"), &ValorAssets::pvs_leaf_ids);
    ClassDB::bind_method(D_METHOD("pvs_visibility", "camera"), &ValorAssets::pvs_visibility);
    ClassDB::bind_method(D_METHOD("get_model_list_faces", "hash"), &ValorAssets::get_model_list_faces);
    ClassDB::bind_method(D_METHOD("bone_point", "hash", "anim", "frame", "bone", "offset"), &ValorAssets::bone_point);
    ClassDB::bind_method(D_METHOD("stick_roles", "rx", "ry", "lx", "ly"), &ValorAssets::stick_roles);
}

bool ValorAssets::mount_disc(const String& path) {
    std::string p = path.utf8().get_data(), err;
    if (valor::disc_mounted()) return true;
    if (!valor::disc_mount(p, &err)) {
        error_ = String(err.c_str());
        return false;
    }
    return true;
}

bool ValorAssets::load(const String& data_dir, int mission, int level, bool american) {
    std::string dir = data_dir.utf8().get_data(), err;
    data_dir_ = dir, mission_ = mission, level_ = level;
    char rel[64];
    std::snprintf(rel, sizeof rel, "/MSN%d/LVL%d/%d_%d.BSD", mission, level, mission, level);
    if (!info_.load(dir, mission, level, &err) || !valor::read_file(dir + rel, bsd_) || !models_.load(dir, mission, level, &err)) {
        error_ = String(err.empty() ? rel : err.c_str());
        return false;
    }
    if (!valor::read_file(dir + "/../LEVEL.BIN", level_bin_)) {
        error_ = "can't read LEVEL.BIN";
        return false;
    }
    std::vector<uint8_t> taf;
    std::snprintf(rel, sizeof rel, "/MSN%d/LVL%d/%d_%d%d.TAF", mission, level, mission, level, american ? 1 : 0);
    if (!valor::read_file(dir + rel, taf) || !vram_.load_taf_tims(taf)) {
        error_ = String("bad or missing TAF: ") + rel;
        return false;
    }
    // The TIMs (formats.md, TAF), then {u32 count, count x u32 offsets from the table} of VABs.
    size_t p = 0;
    while (p + 8 <= taf.size() && rd32(taf, p) == 0x10) {
        size_t q = p + 8;
        if (rd32(taf, p + 4) & 8) q += rd32(taf, q);
        p = q + rd32(taf, q);
    }
    banks_.clear(), bank_offsets_.clear(), sounds_.clear();
    uint32_t count = rd32(taf, p);
    for (uint32_t i = 0; i < count && i < 8; i++) {
        size_t vab = p + rd32(taf, p + 4 + 4 * i);
        if (rd32(taf, vab) != 0x56414270) break;  // "pBAV"
        // audio.md, The VAB banks: the header, 0x800 of empty programs, the VAG size table
        // (256 u16, entry n + 1 the size of VAG n in 8-byte units), then the VAGs.
        // Header +0x12 programs, +0x16 VAGs (standard VAB).
        size_t programs = rd16(taf, vab + 0x12), vags = std::min<size_t>(rd16(taf, vab + 0x16), 255);
        size_t sizes = vab + 0x20 + 0x800 + programs * 0x200;
        size_t body = sizes + 0x200;
        std::vector<uint32_t> offs{0};
        uint32_t total = 0;
        for (size_t n = 1; n <= vags; n++) {
            total += uint32_t(rd16(taf, sizes + 2 * n)) * 8;
            offs.push_back(total);
        }
        size_t end = std::min(taf.size(), body + total);
        banks_.emplace_back(taf.begin() + std::min(body, taf.size()), taf.begin() + end);
        bank_offsets_.push_back(offs);
    }
    // The stick curve g_axis_curve (0x8009E3AC) is static data in LEVEL.BIN.
    controls_.cfg = valor::PadConfig::fresh_card();
    PackedByteArray curve = level_bin(0x8009E3AC, 128);
    for (int i = 0; i < 128; i++) controls_.cfg.curve[i] = curve[i];
    controls_.pad.analog = 1;
    layer_of_.clear();
    page_images_.clear();
    pages_.unref();
    pages_dirty_ = true;
    error_ = "";
    return true;
}

Dictionary ValorAssets::get_level_info() const {
    Dictionary d;
    size_t h = kBody;
    d["mission"] = mission_;
    d["level"] = level_;
    // formats.md: bytes stored into the gradient as R, B, G (sky_gradient_init 0x80088980).
    d["sky"] = Color(bsd_[h + 0x58C] / 255.0f, bsd_[h + 0x58E] / 255.0f, bsd_[h + 0x58D] / 255.0f);
    d["sky_rgb"] = PackedByteArray();
    PackedByteArray sky;
    sky.push_back(bsd_[h + 0x58C]), sky.push_back(bsd_[h + 0x58E]), sky.push_back(bsd_[h + 0x58D]);
    d["sky_rgb"] = sky;
    d["star_height"] = bsd_[h + 0x58F];
    d["moon"] = rd32(bsd_, h + 0x594) != 0;
    d["moon_y"] = int16_t(rd16(bsd_, h + 0x594));
    d["moon_z"] = int16_t(rd16(bsd_, h + 0x596));
    d["fog_near"] = info_.fog_near;
    d["fog_color"] = Color(info_.fog_rgb[0] / 255.0f, info_.fog_rgb[1] / 255.0f, info_.fog_rgb[2] / 255.0f);
    d["view_distance"] = int64_t(info_.view_distance);
    return d;
}

PackedByteArray ValorAssets::level_bin(int64_t addr, int64_t len) const {
    PackedByteArray out;
    int64_t o = addr - int64_t(kOverlay);
    for (int64_t i = 0; i < len; i++) out.push_back(o + i >= 0 && size_t(o + i) < level_bin_.size() ? level_bin_[size_t(o + i)] : 0);
    return out;
}

String ValorAssets::level_string(int64_t addr) const {
    std::string s;
    for (int64_t o = addr - int64_t(kOverlay); o >= 0 && size_t(o) < level_bin_.size() && level_bin_[size_t(o)]; o++)
        s.push_back(char(level_bin_[size_t(o)]));
    return String(s.c_str());
}

Ref<Image> ValorAssets::page_image(int tpage, int clut) const {
    std::vector<uint8_t> rgba;
    vram_.decode_page(uint16_t(tpage), uint16_t(clut), rgba);
    PackedByteArray bytes;
    bytes.resize(int64_t(rgba.size()));
    memcpy(bytes.ptrw(), rgba.data(), rgba.size());
    return Image::create_from_data(256, 256, false, Image::FORMAT_RGBA8, bytes);
}

Ref<Image> ValorAssets::region_image(int tpage, int clut, int u, int v, int w, int h) const {
    Ref<Image> page = page_image(tpage, clut);
    return page->get_region(Rect2i(u, v, w, h));
}

int ValorAssets::get_sound_count(int bank) const {
    return bank >= 0 && size_t(bank) < bank_offsets_.size() ? int(bank_offsets_[size_t(bank)].size()) - 1 : 0;
}

Ref<AudioStreamWAV> ValorAssets::get_sound(int bank, int sound) {
    if (sound < 0 || sound >= get_sound_count(bank)) return Ref<AudioStreamWAV>();
    uint64_t key = uint64_t(bank) << 32 | uint32_t(sound);
    auto it = sounds_.find(key);
    if (it != sounds_.end()) return it->second;
    const auto& offs = bank_offsets_[size_t(bank)];
    const auto& body = banks_[size_t(bank)];
    size_t a = std::min<size_t>(offs[size_t(sound)], body.size()), b = std::min<size_t>(offs[size_t(sound) + 1], body.size());
    valor_present::Adpcm d = valor_present::adpcm_decode(body.data() + a, b - a, false);
    // The game's loops are re-triggers (audio.md, Every frame), not ADPCM loop points.
    Ref<AudioStreamWAV> s = wav(d.pcm, 22050, -1);
    sounds_[key] = s;
    return s;
}

Ref<AudioStreamWAV> ValorAssets::get_music(bool ambience_only) const {
    char rel[64];
    std::snprintf(rel, sizeof rel, "/MSN%d/LVL%d/M%d_%d%s.VB", mission_, level_, mission_, level_, ambience_only ? "A" : "");
    std::vector<uint8_t> vb;
    if (!valor::read_file(data_dir_ + rel, vb)) return Ref<AudioStreamWAV>();
    // Headerless ADPCM read to its end; the track loops from its start (audio.md, Music).
    valor_present::Adpcm d = valor_present::adpcm_decode(vb.data(), vb.size(), true);
    return wav(d.pcm, 22050, 0);
}

int ValorAssets::get_model_bank(int64_t hash) const {
    uint32_t count = rd32(bsd_, kBody + 0x5A4);
    for (uint32_t i = 0; i < count; i++) {
        size_t o = kBody + 0x5A8 + 0x100 * size_t(i);
        if (rd32(bsd_, o) == uint32_t(hash)) return int16_t(rd16(bsd_, o + 0xC8));
    }
    return -1;
}

int ValorAssets::get_anim_frames(int64_t hash, int anim) const {
    const valor::Model* m = models_.by_hash(uint32_t(hash));
    if (!m || anim < 0 || size_t(anim) >= m->anims.size()) return 0;
    return int(m->anims[size_t(anim)].frames.size());
}

int ValorAssets::layer(uint16_t tpage, uint16_t clut) {
    uint32_t key = uint32_t(tpage & 0x19F) | uint32_t(clut) << 16;
    auto it = layer_of_.find(key);
    if (it != layer_of_.end()) return it->second;
    page_images_.push_back(page_image(tpage, clut));
    int n = int(layer_of_.size());
    layer_of_[key] = n;
    pages_dirty_ = true;
    return n;
}

Ref<Texture2DArray> ValorAssets::get_texture_pages() {
    if (pages_dirty_ || pages_.is_null()) {
        pages_.instantiate();
        if (!page_images_.is_empty()) pages_->create_from_images(page_images_);
        pages_dirty_ = false;
    }
    return pages_;
}

Ref<ArrayMesh> ValorAssets::build_model_mesh(int64_t hash, int anim, int frame) {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    const valor::Model* m = models_.by_hash(uint32_t(hash));
    if (!m || !m->skinned() || anim < 0 || size_t(anim) >= m->anims.size() || m->anims[size_t(anim)].frames.empty())
        return mesh;
    valor::AnimInstance inst;
    inst.model = m;
    inst.start(anim, frame, frame, 2);
    inst.build(models_);
    if (inst.pose.empty()) return mesh;
    auto verts = valor::skin_vertices(*m, inst.pose, inst.joints);
    struct Surface {
        PackedVector3Array v;
        PackedColorArray c;
        PackedVector2Array uv, uv2;
    } surf[5];
    for (const valor::SkinFace& f : m->faces) {
        bool semi = f.tpage & 0x4000;
        Surface& s = surf[semi ? 1 + ((f.tpage >> 5) & 3) : 0];
        bool ok = true;
        for (int k = 0; k < 3; k++) ok &= f.bone[k] < verts.size() && f.vertex[k] < verts[f.bone[k]].size();
        if (!ok) continue;
        float l = float(layer(f.tpage, f.clut));
        for (int k = 0; k < 3; k++) {
            const auto& v = verts[f.bone[k]][f.vertex[k]];
            s.v.push_back(to_godot(v[0], v[1], v[2]));
            s.c.push_back(Color(f.rgb[k][0] / 255.0f, f.rgb[k][1] / 255.0f, f.rgb[k][2] / 255.0f));
            s.uv.push_back(Vector2(f.uv[k][0] / 256.0f, f.uv[k][1] / 256.0f));
            s.uv2.push_back(Vector2(l, 0.0f));
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
    return mesh;
}

PackedInt32Array ValorAssets::stick_roles(int rx, int ry, int lx, int ly) const {
    valor::Controls c = controls_;
    c.pad.sticks[0] = uint8_t(rx), c.pad.sticks[1] = uint8_t(ry), c.pad.sticks[2] = uint8_t(lx), c.pad.sticks[3] = uint8_t(ly);
    PackedInt32Array out;
    for (int r = 0; r < 4; r++) out.push_back(c.role_axis(r));
    return out;
}

Vector3 ValorAssets::bone_point(int64_t hash, int anim, int frame, int bone, Vector3i offset) {
    const valor::Model* m = models_.by_hash(uint32_t(hash));
    if (!m || !m->skinned() || anim < 0 || size_t(anim) >= m->anims.size() || m->anims[size_t(anim)].frames.empty())
        return Vector3();
    valor::AnimInstance inst;
    inst.model = m;
    inst.start(anim, frame, frame, 2);
    inst.build(models_);
    valor::HitPartTemplate t;
    t.a = t.b = uint16_t(bone);
    t.off[0] = int16_t(offset.x), t.off[1] = int16_t(offset.y), t.off[2] = int16_t(offset.z);
    int16_t out[3];
    valor::part_centre(t, inst.joints, &inst.pose, 0, out);
    return to_godot(out[0], out[1], out[2]);
}

Dictionary ValorAssets::get_model_list_faces(int64_t hash) const {
    Dictionary d;
    uint32_t count = rd32(bsd_, kBody + 0x5A4);
    for (uint32_t i = 0; i < count; i++) {
        size_t o = kBody + 0x5A8 + 0x100 * size_t(i);
        if (rd32(bsd_, o) != uint32_t(hash)) continue;
        size_t list = kBody + rd32(bsd_, o + 0x30), verts = kBody + rd32(bsd_, o + 0x88), cols = kBody + rd32(bsd_, o + 0xC4);
        int nv = rd16(bsd_, o + 0x8C);
        if (list == kBody || verts == kBody) return d;
        PackedVector2Array v;
        PackedColorArray c;
        for (int k = 0; k < nv; k++) {
            v.push_back(Vector2(int16_t(rd16(bsd_, verts + 8 * size_t(k))), int16_t(rd16(bsd_, verts + 8 * size_t(k) + 2))));
            c.push_back(Color(bsd_[cols + 4 * size_t(k)] / 255.0f, bsd_[cols + 4 * size_t(k) + 1] / 255.0f, bsd_[cols + 4 * size_t(k) + 2] / 255.0f));
        }
        // {u32 count, count x u32}: three 10-bit vertex indices per face
        PackedInt32Array t;
        uint32_t n = rd32(bsd_, list);
        for (uint32_t f = 0; f < n && f < 1024; f++) {
            uint32_t w = rd32(bsd_, list + 4 + 4 * size_t(f));
            t.push_back(int(w & 0x3FF)), t.push_back(int((w >> 10) & 0x3FF)), t.push_back(int((w >> 20) & 0x3FF));
        }
        d["verts"] = v, d["colors"] = c, d["tris"] = t;
        return d;
    }
    return d;
}

int ValorAssets::pvs_load() {
    pvs_chunks_.clear(), pvs_leaf_path_.clear(), pvs_leaves_ = 0;
    for (auto& f : pvs_face_leaf_) f.clear();
    for (int n = 1;; n++) {
        char rel[96];
        std::snprintf(rel, sizeof rel, "/MSN%d/LVL%d/TSP0/%d_%d_C%d.TSP", mission_, level_, mission_, level_, n);
        std::vector<uint8_t> d;
        if (!valor::read_file(data_dir_ + rel, d)) break;
        uint32_t node_off = rd32(d, 0x08), face_off = rd32(d, 0x10);
        PvsChunk ch;
        std::map<uint32_t, int> index;  // node offset -> node index
        auto node_at = [&](uint32_t off) -> int {
            auto it = index.find(off);
            if (it != index.end()) return it->second;
            size_t p = node_off + size_t(off);
            PvsNode nd;
            for (int k = 0; k < 3; k++) nd.min[k] = int16_t(rd16(d, p + 2 * k)), nd.max[k] = int16_t(rd16(d, p + 6 + 2 * k));
            nd.a = rd32(d, p + 0x10), nd.b = rd32(d, p + 0x14);
            nd.leaf = rd32(d, p + 0xC) != 0;
            int i = int(ch.nodes.size());
            ch.nodes.push_back(nd);
            index[off] = i;
            return i;
        };
        // The walk RenderChunk::parse makes (depth first, children in order, each node once), so
        // the faces come in the world mesh's order; each leaf remembers its path from the root.
        int chunk = int(pvs_chunks_.size());
        std::vector<std::pair<uint32_t, std::vector<std::pair<int, int>>>> stack = {{0u, {}}};
        std::vector<bool> seen;
        while (!stack.empty()) {
            auto [off, path] = stack.back();
            stack.pop_back();
            size_t p = node_off + size_t(off);
            if (p + 0x1C > d.size()) break;
            if (seen.size() <= off) seen.resize(off + 1);
            if (seen[off]) continue;
            seen[off] = true;
            int ni = node_at(off);
            path.push_back({chunk, ni});
            uint32_t count = rd32(d, p + 0xC);
            if (count) {
                int leaf = pvs_leaves_++;
                ch.nodes[size_t(ni)].leaf_id = leaf;
                pvs_leaf_path_.push_back(path);
                size_t f = face_off + size_t(rd32(d, p + 0x18));
                for (uint32_t k = 0; k < count; k++, f += 16) {
                    uint16_t tpage = rd16(d, f + 12);
                    int surf = (tpage & 0x4000) ? 1 + ((tpage >> 5) & 3) : 0;
                    for (int c = 0; c < 3; c++) pvs_face_leaf_[surf].push_back(float(leaf));
                }
            } else {
                if (p + 0x24 > d.size()) break;
                for (int c = 2; c >= 0; c--) {
                    uint32_t child = rd32(d, p + 0x18 + 4 * c);
                    if (child != 0xFFFFFFFF) {
                        ch.nodes[size_t(ni)].child[c] = node_at(child);
                        stack.push_back({child, path});
                    }
                }
            }
        }
        pvs_chunks_.push_back(std::move(ch));
    }
    return pvs_leaves_;
}

PackedFloat32Array ValorAssets::pvs_leaf_ids(int surface) const {
    PackedFloat32Array out;
    if (surface < 0 || surface >= 5) return out;
    out.resize(int64_t(pvs_face_leaf_[surface].size()));
    if (!pvs_face_leaf_[surface].empty()) memcpy(out.ptrw(), pvs_face_leaf_[surface].data(), pvs_face_leaf_[surface].size() * sizeof(float));
    return out;
}

PackedByteArray ValorAssets::pvs_visibility(Vector3 camera) {
    int32_t c[3] = {int32_t(camera.x), int32_t(camera.y), int32_t(camera.z)};
    uint32_t cam_a = 0, cam_b = 0;
    for (const PvsChunk& ch : pvs_chunks_) {
        if (ch.nodes.empty()) continue;
        // pvs_leaf_find: into boxes holding the point, children 1, 0, 2; a leaf lower (smaller max y)
        // than the best so far takes over: its B the chunk's answer, its A ORed in
        int16_t best = 0x7FFF;
        uint32_t b = 0;
        // func_8008C964: x and z within the box, the box's bottom (max y, y down) at or below the
        // point, and its top (min y) not below the best leaf's bottom so far
        auto inside = [&](const PvsNode& n) {
            return c[0] >= n.min[0] && c[0] <= n.max[0] && c[2] >= n.min[2] && c[2] <= n.max[2] && n.max[1] >= c[1] &&
                   best >= n.min[1];
        };
        std::function<void(int)> find = [&](int i) {
            const PvsNode& n = ch.nodes[size_t(i)];
            if (!inside(n)) return;
            if (n.leaf) {
                if (n.max[1] < best) best = n.max[1], b = n.b, cam_a |= n.a;
                return;
            }
            for (int k : {1, 0, 2})
                if (n.child[k] >= 0) find(n.child[k]);
        };
        find(0);
        cam_b |= b;
    }
    PackedByteArray vis;
    vis.resize(pvs_leaves_);
    for (int leaf = 0; leaf < pvs_leaves_; leaf++) {
        bool ok = true;
        for (auto [ci, ni] : pvs_leaf_path_[size_t(leaf)]) {
            const PvsNode& n = pvs_chunks_[size_t(ci)].nodes[size_t(ni)];
            if (!(n.a & cam_b) || !(n.b & cam_a)) {
                ok = false;
                break;
            }
        }
        vis.set(leaf, ok ? 255 : 0);
    }
    return vis;
}
