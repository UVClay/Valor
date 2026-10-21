#include "valor/model.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <functional>
#include <map>

#include "valor/fixed.h"
#include "valor/io.h"

namespace valor {

namespace {

struct Body {
    const std::vector<uint8_t>& d;
    bool ok = true;
    uint32_t u32(size_t o) {
        if (o + 4 > d.size()) return ok = false, 0;
        return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8) | (uint32_t(d[o + 2]) << 16) | (uint32_t(d[o + 3]) << 24);
    }
    uint16_t u16(size_t o) {
        if (o + 2 > d.size()) return ok = false, 0;
        return uint16_t(d[o] | (d[o + 1] << 8));
    }
    int16_t s16(size_t o) { return int16_t(u16(o)); }
    uint8_t u8(size_t o) {
        if (o >= d.size()) return ok = false, 0;
        return d[o];
    }
};

constexpr uint32_t kNone = 0xFFFFFFFF;

// level_load's inheritance (formats.md, Models): each group whose first word is 0 (or -1) is copied
// from the base model.
void inherit(std::vector<uint8_t>& m, const std::vector<uint8_t>& base) {
    auto w = [&](const std::vector<uint8_t>& v, int o) {
        return uint32_t(v[o]) | (uint32_t(v[o + 1]) << 8) | (uint32_t(v[o + 2]) << 16) | (uint32_t(v[o + 3]) << 24);
    };
    auto empty = [&](int o) { return w(m, o) == 0 || w(m, o) == kNone; };
    auto copy = [&](int from, int to) { std::memcpy(&m[from], &base[from], size_t(to - from)); };
    if (empty(0x4)) copy(0x4, 0x8), copy(0x3C, 0x84), copy(0x88, 0x9C), copy(0xCC, 0xD4);
    if (empty(0xC)) copy(0xC, 0x10), copy(0x94, 0x96);
    if (empty(0x10)) copy(0x10, 0x18);
    // +0x8's group: +0x18 and +0x20..+0x37 (a model keeps its own +0x38 face table: 1:3's soldier
    // model 0xCABD3AC1 inherits its animations from 0xB8534EAA, not its faces; checked in RAM).
    if (empty(0x8)) copy(0x8, 0xC), copy(0x18, 0x1C), copy(0x20, 0x38);
    if (empty(0x84)) copy(0x84, 0x88);
    if (empty(0x9C)) copy(0x9C, 0xA0);
    if (empty(0xA0)) copy(0xA0, 0xA4);
    if (empty(0xA4)) copy(0xA4, 0xB0);
    if (empty(0xB0)) copy(0xB0, 0xB8);
    if (empty(0xB8)) copy(0xB8, 0xBE);
    if (empty(0xC0)) copy(0xBE, 0xC4);
    if (empty(0xC4)) copy(0xC4, 0xC8);
    if (empty(0xC8)) copy(0xC8, 0xCC);
    if (int16_t(m[0xD4] | (m[0xD5] << 8)) < 0) copy(0xD4, 0xE8);
    for (int o = 0xEC; o <= 0xF8; o += 4)
        if (w(m, o) == kNone) copy(o, o + 4);
    if (w(m, 0xFC) == kNone) copy(0xFC, 0x100);
}

}  // namespace

void Models::unpack3(uint32_t w, int16_t out[3]) {
    auto sx = [](uint32_t v, int n) { return int16_t((v >> (n - 1)) ? int32_t(v) - (1 << n) : int32_t(v)); };
    out[0] = sx(w & 0x3FF, 10);
    out[1] = sx((w >> 10) & 0x7FF, 11);
    out[2] = sx((w >> 21) & 0x3FF, 10);
}

bool Models::load(const std::string& data_dir, int m, int l, std::string* err) {
    models.clear();
    if (!gte.load(data_dir, err)) return false;
    char rel[64];
    std::snprintf(rel, sizeof rel, "/MSN%d/LVL%d/%d_%d.BSD", m, l, m, l);
    std::vector<uint8_t> file;
    if (!read_file(data_dir + rel, file) || file.size() < 0x800 + 0x5A8) {
        if (err) *err = std::string("can't read ") + rel;
        return false;
    }
    body_.assign(file.begin() + 0x800, file.end());
    Body b{body_};
    uint32_t anim_tab = b.u32(0x544), frame_tab = b.u32(0x54C), bone_tab = b.u32(0x55C);
    uint32_t facetab_tab = b.u32(0x564), face_tab = b.u32(0x56C), vidx_tab = b.u32(0x574);
    uint32_t vtab_tab = b.u32(0x57C), vert_tab = b.u32(0x584);
    quat_base_ = b.u32(0x554);
    uint32_t count = b.u32(0x5A4);
    std::vector<std::vector<uint8_t>> raw(count);
    std::map<uint32_t, int> by;
    for (uint32_t i = 0; i < count; i++) {
        size_t o = 0x5A8 + 0x100 * size_t(i);
        if (o + 0x100 > body_.size()) break;
        raw[i].assign(body_.begin() + o, body_.begin() + o + 0x100);
        by[b.u32(o)] = int(i);
    }
    for (auto& r : raw) {
        if (r.size() < 0x100) continue;
        uint32_t base = uint32_t(r[0xE8]) | (uint32_t(r[0xE9]) << 8) | (uint32_t(r[0xEA]) << 16) | (uint32_t(r[0xEB]) << 24);
        auto it = by.find(base);
        if (base != kNone && it != by.end()) inherit(r, raw[it->second]);
    }
    for (auto& r : raw) {
        Model md;
        if (r.size() < 0x100) {
            models.push_back(md);
            continue;
        }
        Body mr{r};
        md.hash = mr.u32(0), md.base = mr.u32(0xE8), md.type = int32_t(mr.u32(0xFC));
        md.radius = int32_t(mr.u32(0xA0));
        for (int k = 0; k < 3; k++) md.scale[k] = int32_t(mr.u32(0xA4 + 4 * k));
        // Animations: {u16 count, u16, count offsets into the animation table (-1: not loaded)}.
        uint32_t al = mr.u32(0x8);
        if (al != kNone && al != 0) {
            uint32_t n = b.u16(al);
            md.anims.resize(n);
            for (uint32_t k = 0; k < n && b.ok; k++) {
                uint32_t eo = b.u32(al + 4 + 4 * k);
                if (eo == kNone) continue;
                size_t e = anim_tab + eo;
                Anim& a = md.anims[k];
                uint32_t nf = b.u8(e);
                a.bones = b.u8(e + 1);
                uint32_t fo = frame_tab + b.u32(e + 4);
                for (uint32_t f = 0; f < nf; f++) {
                    size_t p = fo + 20 * size_t(f);
                    AnimFrame fr;
                    fr.move = b.u32(p), fr.root = b.u32(p + 4);
                    for (int j = 0; j < 3; j++) fr.turn[j] = b.s16(p + 8 + 2 * j);
                    fr.keys = b.u8(p + 0xE), fr.nquats = b.u8(p + 0xF);
                    uint32_t q = b.u32(p + 0x10);
                    fr.quats = q == kNone ? -1 : int32_t(q);
                    a.frames.push_back(fr);
                }
            }
        }
        // The skeleton: first child / next sibling records from the root.
        uint32_t root = mr.u32(0x9C);
        if (root != kNone && md.anims.size()) {
            std::map<uint32_t, int> seen;
            std::vector<uint32_t> stack = {root};
            // Breadth of records: each record's children and siblings by offset.
            std::function<int(uint32_t)> walk = [&](uint32_t off) -> int {
                if (off == kNone || seen.count(off) || md.bones.size() > 256) return off == kNone ? -1 : seen[off];
                int idx = int(md.bones.size());
                seen[off] = idx;
                md.bones.push_back({});
                size_t p = bone_tab + off;
                Bone bn;
                bn.index = b.u16(p);
                for (int j = 0; j < 3; j++) bn.offset[j] = b.s16(p + 2 + 2 * j);
                uint32_t child = b.u32(p + 0xC), sib = b.u32(p + 0x10);
                bn.first_child = walk(child);
                bn.next_sibling = walk(sib);
                md.bones[idx] = bn;
                return idx;
            };
            walk(root);
        }
        // Skinned faces: the face table entry {offset, count}, 28 bytes each.
        if (md.skinned()) {
            uint32_t ft = mr.u32(0x38);
            if (ft != kNone) {
                uint32_t fo = b.u32(facetab_tab + ft), fn = b.u32(facetab_tab + ft + 4);
                for (uint32_t k = 0; k < fn && b.ok && fo != kNone; k++) {
                    size_t p = face_tab + fo + 28 * size_t(k);
                    SkinFace f;
                    for (int c = 0; c < 3; c++)
                        for (int j = 0; j < 3; j++) f.rgb[c][j] = b.u8(p + 4 * c + j);
                    f.uv[0][0] = b.u8(p + 12), f.uv[0][1] = b.u8(p + 13), f.clut = b.u16(p + 14);
                    f.uv[1][0] = b.u8(p + 16), f.uv[1][1] = b.u8(p + 17), f.tpage = b.u16(p + 18);
                    f.uv[2][0] = b.u8(p + 20), f.uv[2][1] = b.u8(p + 21);
                    for (int c = 0; c < 3; c++) f.vertex[c] = b.u8(p + 22 + 2 * c), f.bone[c] = b.u8(p + 23 + 2 * c);
                    md.faces.push_back(f);
                }
            }
            // Vertices per bone: the vertex-table index {offset, bones}, then {offset, count}.
            uint32_t vi = mr.u32(0x84);
            if (vi != kNone) {
                uint32_t vo = b.u32(vidx_tab + vi), vn = b.u32(vidx_tab + vi + 4);
                md.bone_vertices.resize(vn);
                for (uint32_t k = 0; k < vn && b.ok && vn < 256; k++) {
                    size_t e = vtab_tab + vo + 8 * size_t(k);
                    uint32_t po = b.u32(e), pn = b.u32(e + 4);
                    for (uint32_t j = 0; j < pn && po != kNone && b.ok && pn < 4096; j++) {
                        size_t p = vert_tab + po + 8 * size_t(j);
                        md.bone_vertices[k].push_back({b.s16(p), b.s16(p + 2), b.s16(p + 4)});
                    }
                }
            }
        }
        // Static geometry: faces {count, 16 bytes each}, vertices, colours.
        uint32_t sf = mr.u32(0x2C), sv = mr.u32(0x88), sc = mr.u32(0xC4);
        uint32_t nv = mr.u16(0x8C);
        if (sf != kNone && sf != 0) {
            uint32_t n = b.u32(sf);
            for (uint32_t k = 0; k < n && b.ok && n < 65536; k++) {
                size_t p = sf + 4 + 16 * size_t(k);
                StaticFace f;
                f.uv[0][0] = b.u8(p), f.uv[0][1] = b.u8(p + 1), f.clut = b.u16(p + 2);
                f.uv[1][0] = b.u8(p + 4), f.uv[1][1] = b.u8(p + 5), f.tpage = b.u16(p + 6);
                f.uv[2][0] = b.u8(p + 8), f.uv[2][1] = b.u8(p + 9);
                uint32_t w = b.u32(p + 12);
                f.v[0] = w & 0x3FF, f.v[1] = (w >> 10) & 0x3FF, f.v[2] = (w >> 20) & 0x3FF;
                md.static_faces.push_back(f);
            }
        }
        if (sv != kNone && sv != 0)
            for (uint32_t k = 0; k < nv && b.ok; k++)
                md.static_vertices.push_back({b.s16(sv + 8 * k), b.s16(sv + 8 * k + 2), b.s16(sv + 8 * k + 4)});
        if (sc != kNone && sc != 0)
            for (uint32_t k = 0; k < nv && b.ok; k++)
                md.static_colours.push_back({b.u8(sc + 4 * k), b.u8(sc + 4 * k + 1), b.u8(sc + 4 * k + 2)});
        // Hit parts: 0x48-byte templates.
        uint32_t np = mr.u16(0xBE), pt = mr.u32(0xC0);
        for (uint32_t k = 0; k < np && pt != kNone && pt != 0 && b.ok && np < 64; k++) {
            size_t p = pt + 0x48 * size_t(k);
            HitPartTemplate h;
            h.kind = int32_t(b.u32(p + 8)), h.a = b.u16(p + 0x1C), h.b = b.u16(p + 0x1E);
            h.radius = b.s16(p + 0x34), h.radius2 = int32_t(b.u32(p + 0x38));
            for (int j = 0; j < 3; j++) h.off[j] = b.s16(p + 0x40 + 2 * j);
            md.parts.push_back(h);
        }
        models.push_back(std::move(md));
    }
    if (!b.ok) {
        if (err) *err = std::string("bad model data in ") + rel;
        return false;
    }
    return true;
}

const Model* Models::by_hash(uint32_t hash) const {
    for (const Model& m : models)
        if (m.hash == hash) return &m;
    return nullptr;
}

std::vector<Quat> Models::decode(int32_t off, int n) const {
    std::vector<Quat> out;
    if (off < 0) return out;
    Body b{body_};
    auto sx = [](uint32_t v) { return int16_t((v & 0x800) ? int32_t(v & 0xFFF) - 0x1000 : int32_t(v & 0xFFF)); };
    size_t p = quat_base_ + uint32_t(off);
    for (int i = 0; int(out.size()) < n; i += 3) {
        uint32_t w0 = b.u32(p + 4 * i), w1 = b.u32(p + 4 * i + 4);
        int16_t w = sx(w0 >> 20), x = sx((w0 >> 4) & 0xFFF), y = sx(w1 & 0xFFF);
        int16_t z = sx((((w1 >> 12) & 0xF) << 8) | (((w0 >> 16) & 0xF) << 4) | (w0 & 0xF));
        out.push_back({int16_t(x * 2), int16_t(y * 2), int16_t(z * 2), int16_t(w * 2)});
        if (int(out.size()) == n) break;
        uint32_t w2 = b.u32(p + 4 * i + 8);
        x = sx(w1 >> 20), y = sx((w2 >> 16) & 0xFFF), w = sx((w2 >> 4) & 0xFFF);
        z = sx(((w2 >> 28) << 8) | ((w2 & 0xF) << 4) | ((w1 >> 16) & 0xF));
        out.push_back({int16_t(x * 2), int16_t(y * 2), int16_t(z * 2), int16_t(w * 2)});
        if (!b.ok) break;
    }
    return out;
}

Quat quat_interp(const Gte& g, const Quat& a, const Quat& b, int k) {
    constexpr int64_t kSame = 8, kLerp = 140;  // 0x8009EAB0, 0x8009EAAC
    int64_t d = (int64_t(a[0]) * b[0] + int64_t(a[1]) * b[1] + int64_t(a[2]) * b[2] + int64_t(a[3]) * b[3] + 0x800) >> 12;
    bool neg = d < 0;
    if (!(d < 0x1000 - kSame || 0x1000 + kSame < d)) return k == 1 ? a : b;
    if (neg) d = -d;
    Quat r;
    if (0x1000 - d < kLerp) {
        for (int i = 0; i < 4; i++) {
            int64_t x = a[i], y = b[i];
            if (k == 1)
                r[i] = s16(x + (((neg ? -y : y) - x + 2) >> 2));
            else if (k == 2)
                r[i] = s16(((neg ? x - y : x + y) + 1) >> 1);
            else if (neg)
                r[i] = s16(((x + y + 2) >> 2) - y);
            else
                r[i] = s16(y + ((x - y + 2) >> 2));
        }
        return r;
    }
    int64_t s = g.sqrt12(s32((0x01000800 - d * d) >> 12));
    int64_t th = g.ratan2(int32_t(s), int32_t(d));
    int64_t tt = (th + (k == 2 ? 1 : 2)) >> (k == 2 ? 1 : 2);
    auto tdiv = [](int64_t x, int64_t y) {
        int64_t q = iabs(x) / iabs(y);
        return (x >= 0) == (y >= 0) ? q : -q;
    };
    int64_t wa = tdiv(int64_t(g.rsin(int32_t(th - tt))) << 17, s);
    int64_t wb = tdiv(int64_t(g.rsin(int32_t(tt))) << 17, s);
    if (k == 3) {
        std::swap(wa, wb);
        if (neg) wa = -wa;
    } else if (neg) {
        wb = -wb;
    }
    for (int i = 0; i < 4; i++) r[i] = s16((wa * a[i] + wb * b[i] + 0x10000) >> 17);
    return r;
}

std::vector<Quat> Models::frame_pose(const Model& m, int anim, int frame) const {
    if (anim < 0 || size_t(anim) >= m.anims.size()) return {};
    const Anim& a = m.anims[anim];
    if (frame < 0 || size_t(frame) >= a.frames.size()) return {};
    const AnimFrame& f = a.frames[frame];
    if (f.quats >= 0) return decode(f.quats, f.nquats);
    int nxt = frame + (f.keys >> 4), prv = frame - (f.keys & 0xF);
    if (nxt < 0 || size_t(nxt) >= a.frames.size() || prv < 0) return {};
    int k = nxt - prv == 2 ? 2 : frame - prv;
    if (k < 0) k = prv - frame;
    std::vector<Quat> qa = decode(a.frames[prv].quats, f.nquats), qb = decode(a.frames[nxt].quats, f.nquats);
    std::vector<Quat> out;
    for (size_t i = 0; i < qa.size() && i < qb.size(); i++) out.push_back(quat_interp(gte, qa[i], qb[i], k));
    return out;
}

std::vector<std::array<int16_t, 3>> skeleton_joints(const Model& m, const std::vector<Quat>& quats,
                                                    const int16_t root[3], int nbones) {
    std::vector<std::array<int16_t, 3>> out(std::max<size_t>(m.bone_vertices.size(), 64), {0, 0, 0});
    if (m.bones.empty()) return out;
    const int16_t ident[3][3] = {{0x1000, 0, 0}, {0, 0x1000, 0}, {0, 0, 0x1000}};
    std::function<void(int, const int16_t (*)[3], const int64_t*)> walk = [&](int bi, const int16_t (*prot)[3],
                                                                             const int64_t* ppos) {
        while (bi >= 0) {
            const Bone& bn = m.bones[bi];
            if (bn.index > nbones) return;  // ends the whole sibling chain
            int16_t rot[3][3];
            if (bn.index < quats.size())
                quat_matrix(quats[bn.index].data(), rot);
            else
                std::memcpy(rot, ident, sizeof rot);
            int64_t pos[3];
            for (int i = 0; i < 3; i++)
                pos[i] = ppos[i] + ((int64_t(prot[i][0]) * (bn.offset[0] * 16) + int64_t(prot[i][1]) * (bn.offset[1] * 16) +
                                     int64_t(prot[i][2]) * (bn.offset[2] * 16))
                                    << 3);
            if (bn.first_child >= 0) walk(bn.first_child, rot, pos);
            if (bn.index >= out.size()) out.resize(bn.index + 1, {0, 0, 0});
            for (int i = 0; i < 3; i++) out[bn.index][i] = s16((2 * pos[i] + 0x80000) >> 20);
            bi = bn.next_sibling;
        }
    };
    int64_t rp[3] = {int64_t(root[0]) << 19, int64_t(root[1]) << 19, int64_t(root[2]) << 19};
    walk(0, ident, rp);
    return out;
}

std::vector<std::vector<std::array<int32_t, 3>>> skin_vertices(const Model& m, const std::vector<Quat>& pose,
                                                               const std::vector<std::array<int16_t, 3>>& joints) {
    std::vector<std::vector<std::array<int32_t, 3>>> out(m.bone_vertices.size());
    for (size_t b = 0; b < m.bone_vertices.size(); b++) {
        int16_t rot[3][3] = {{0x1000, 0, 0}, {0, 0x1000, 0}, {0, 0, 0x1000}};
        if (b < pose.size()) quat_matrix(pose[b].data(), rot);
        std::array<int16_t, 3> j = b < joints.size() ? joints[b] : std::array<int16_t, 3>{0, 0, 0};
        for (const auto& v : m.bone_vertices[b]) {
            int32_t in[3] = {v[0], v[1], v[2]}, r[3];
            mvmva12(rot, in, r);
            out[b].push_back({r[0] + j[0], r[1] + j[1], r[2] + j[2]});
        }
    }
    return out;
}

void part_centre(const HitPartTemplate& p, const std::vector<std::array<int16_t, 3>>& joints,
                 const std::vector<Quat>* quats, int32_t eye, int16_t out[3]) {
    if (p.a == 0xFFFE || p.a == 0xFFFF) {
        for (int i = 0; i < 3; i++) out[i] = p.off[i];
        if (p.a == 0xFFFE) out[1] = s16(out[1] + eye);
        return;
    }
    int32_t rot[3] = {0, 0, 0};
    bool has_off = p.off[0] || p.off[1] || p.off[2];
    if (has_off && quats && p.a < quats->size()) {
        int16_t m[3][3];
        quat_matrix((*quats)[p.a].data(), m);
        int32_t v[3] = {p.off[0], p.off[1], p.off[2]};
        mvmva12(m, v, rot);
        for (int i = 0; i < 3; i++) rot[i] = s16(rot[i]);
    }
    std::array<int16_t, 3> z = {0, 0, 0};
    const auto& ja = p.a < joints.size() ? joints[p.a] : z;
    const auto& jb = p.b < joints.size() ? joints[p.b] : z;
    for (int i = 0; i < 3; i++) out[i] = s16(rot[i] + ((ja[i] + jb[i]) >> 1));
}

}  // namespace valor
