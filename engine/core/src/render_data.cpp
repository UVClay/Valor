#include "valor/render_data.h"

#include <cstdio>
#include <functional>

#include "valor/io.h"

namespace valor {

namespace {

uint16_t rd16(const std::vector<uint8_t>& d, size_t o) { return o + 2 <= d.size() ? uint16_t(d[o] | (d[o + 1] << 8)) : 0; }
uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return o + 4 <= d.size()
               ? uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8) | (uint32_t(d[o + 2]) << 16) | (uint32_t(d[o + 3]) << 24)
               : 0;
}

}  // namespace

bool RenderChunk::parse(const std::vector<uint8_t>& d) {
    if (d.size() < 0x44) return false;
    uint32_t node_off = rd32(d, 0x08), face_off = rd32(d, 0x10);
    uint32_t n_verts = rd32(d, 0x14), vert_off = rd32(d, 0x18);
    uint32_t n_cols = rd32(d, 0x24), col_off = rd32(d, 0x28);
    if (vert_off + 8 * size_t(n_verts) > d.size() || col_off + 4 * size_t(n_cols) > d.size()) return false;
    verts.resize(n_verts);
    for (uint32_t i = 0; i < n_verts; i++) {
        size_t p = vert_off + 8 * i, c = col_off + 4 * i;
        verts[i] = {int16_t(rd16(d, p)), int16_t(rd16(d, p + 2)), int16_t(rd16(d, p + 4)),
                    i < n_cols ? d[c] : uint8_t(128), i < n_cols ? d[c + 1] : uint8_t(128),
                    i < n_cols ? d[c + 2] : uint8_t(128)};
    }
    // The render tree: a leaf (+0xC > 0) is 0x1C bytes with its face count and first face; an inner
    // node 0x24 bytes with three children (offsets from the node table, -1 none).
    faces.clear();
    std::vector<uint32_t> stack = {0};
    std::vector<bool> seen;
    while (!stack.empty()) {
        uint32_t n = stack.back();
        stack.pop_back();
        size_t p = node_off + size_t(n);
        if (p + 0x1C > d.size()) return false;
        if (seen.size() <= n) seen.resize(n + 1);
        if (seen[n]) continue;
        seen[n] = true;
        uint32_t count = rd32(d, p + 0xC);
        if (count) {
            size_t f = face_off + size_t(rd32(d, p + 0x18));
            for (uint32_t k = 0; k < count; k++, f += 16) {
                if (f + 16 > d.size()) return false;
                RenderFace face;
                face.v[0] = rd16(d, f), face.v[1] = rd16(d, f + 2), face.v[2] = rd16(d, f + 4);
                face.uv[0][0] = d[f + 6], face.uv[0][1] = d[f + 7];
                face.clut = rd16(d, f + 8);
                face.uv[1][0] = d[f + 10], face.uv[1][1] = d[f + 11];
                face.tpage = rd16(d, f + 12);
                face.uv[2][0] = d[f + 14], face.uv[2][1] = d[f + 15];
                if (face.v[0] >= n_verts || face.v[1] >= n_verts || face.v[2] >= n_verts) return false;
                faces.push_back(face);
            }
        } else {
            if (p + 0x24 > d.size()) return false;
            // Children pushed in reverse so they're visited in order.
            for (int c = 2; c >= 0; c--) {
                uint32_t child = rd32(d, p + 0x18 + 4 * c);
                if (child != 0xFFFFFFFF) stack.push_back(child);
            }
        }
    }
    return true;
}

bool Vram::load_taf_tims(const std::vector<uint8_t>& t, int* tims) {
    size_t p = 0;
    int n = 0;
    auto upload = [&](size_t blk) -> size_t {
        uint32_t len = rd32(t, blk);
        int x = rd16(t, blk + 4), y = rd16(t, blk + 6), w = rd16(t, blk + 8), h = rd16(t, blk + 10);
        size_t data = blk + 12;
        if (data + size_t(w) * h * 2 > t.size()) return 0;
        for (int row = 0; row < h; row++)
            for (int col = 0; col < w; col++)
                if (x + col < kW && y + row < kH)
                    px[(y + row) * kW + x + col] = rd16(t, data + 2 * (size_t(row) * w + col));
        return len;
    };
    while (p + 8 <= t.size() && rd32(t, p) == 0x10) {
        uint32_t flags = rd32(t, p + 4);
        size_t q = p + 8;
        if (flags & 8) {
            size_t len = upload(q);
            if (!len) return false;
            q += len;
        }
        size_t len = upload(q);
        if (!len) return false;
        p = q + len;
        n++;
    }
    if (tims) *tims = n;
    return n > 0;
}

void Vram::decode_page(uint16_t tpage, uint16_t clut, std::vector<uint8_t>& rgba) const {
    rgba.assign(256 * 256 * 4, 0);
    int px0 = (tpage & 0xF) * 64, py0 = ((tpage >> 4) & 1) * 256;
    int depth = (tpage >> 7) & 3;
    int cx = (clut & 0x3F) * 16, cy = (clut >> 6) & 0x1FF;
    auto at = [&](int x, int y) -> uint16_t { return px[(y & (kH - 1)) * kW + (x & (kW - 1))]; };
    for (int v = 0; v < 256; v++) {
        for (int u = 0; u < 256; u++) {
            uint16_t c;
            if (depth == 0) {
                uint16_t w = at(px0 + u / 4, py0 + v);
                c = at(cx + ((w >> ((u & 3) * 4)) & 0xF), cy);
            } else if (depth == 1) {
                uint16_t w = at(px0 + u / 2, py0 + v);
                c = at(cx + ((w >> ((u & 1) * 8)) & 0xFF), cy);
            } else {
                c = at(px0 + u, py0 + v);
            }
            uint8_t* o = &rgba[(v * 256 + u) * 4];
            int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            o[0] = uint8_t(r << 3 | r >> 2), o[1] = uint8_t(g << 3 | g >> 2), o[2] = uint8_t(b << 3 | b >> 2);
            o[3] = c == 0 ? 0 : (c & 0x8000) ? 128 : 255;
        }
    }
}

bool LevelRender::load(const std::string& data_dir, const LevelInfo& info, std::string* err) {
    chunks.clear();
    char rel[96];
    for (int n = 1;; n++) {
        std::snprintf(rel, sizeof rel, "/MSN%d/LVL%d/TSP0/%d_%d_C%d.TSP", info.mission, info.level, info.mission,
                      info.level, n);
        std::vector<uint8_t> bytes;
        if (!read_file(data_dir + rel, bytes)) break;
        RenderChunk ch;
        ch.number = n;
        if (!ch.parse(bytes)) {
            if (err) *err = std::string("bad TSP render data: ") + rel;
            return false;
        }
        chunks.push_back(std::move(ch));
    }
    std::snprintf(rel, sizeof rel, "/MSN%d/LVL%d/%d_%d0.TAF", info.mission, info.level, info.mission, info.level);
    std::vector<uint8_t> taf;
    if (!read_file(data_dir + rel, taf) || !vram.load_taf_tims(taf)) {
        if (err) *err = std::string("bad or missing TAF: ") + rel;
        return false;
    }
    return !chunks.empty();
}

void LevelRender::lit_colour(const LevelInfo& info, const RenderVertex& v, uint8_t out[3]) const {
    out[0] = v.r, out[1] = v.g, out[2] = v.b;
    if (info.lights.empty() || v.r >= 40) return;
    uint32_t c = 0;
    if (v.r < info.lights.size() && !info.lights[v.r].steps.empty()) c = info.lights[v.r].steps[0];
    out[0] = c & 0xFF, out[1] = (c >> 8) & 0xFF, out[2] = (c >> 16) & 0xFF;
}

}  // namespace valor
