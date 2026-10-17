// Software rasterizer reproducing the PS1 GPU: affine texturing, CLUT textures, dithering,
// semi-transparency, mask bit, drawing area and texture window. Draws into gpu.vram.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "runtime.h"

#include "gpu.h"
#include "overlay.h"

namespace psx::hw {

namespace {

const int8_t DITHER[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};

int sext11(uint32_t v) { return int32_t(v << 21) >> 21; }

Vertex vertex_xy(uint32_t w) {
    Vertex v{};
    v.x = sext11(w) + gpu.offset_x;
    v.y = sext11(w >> 16) + gpu.offset_y;
    return v;
}

void set_color(Vertex &v, uint32_t c) {
    v.r = c & 0xFF;
    v.g = (c >> 8) & 0xFF;
    v.b = (c >> 16) & 0xFF;
}

void apply_texpage(DrawState &s, uint32_t tp) {
    s.tex_x = (tp & 0xF) * 64;
    s.tex_y = ((tp >> 4) & 1) * 256;
    s.semi_mode = (tp >> 5) & 3;
    s.tex_depth = (tp >> 7) & 3;
}

uint16_t texel(const DrawState &s, int u, int v) {
    // Texture window: replace masked bits of the coordinate with the offset.
    uint32_t tw = gpu.tex_window;
    int mx = (tw & 0x1F) * 8, my = ((tw >> 5) & 0x1F) * 8;
    int ox = ((tw >> 10) & 0x1F) * 8, oy = ((tw >> 15) & 0x1F) * 8;
    u = ((u & 0xFF) & ~mx) | (ox & mx);
    v = ((v & 0xFF) & ~my) | (oy & my);
    const uint16_t *vram = gpu.vram.data();
    auto at = [&](int x, int y) { return vram[(y & 511) * 1024 + (x & 1023)]; };
    switch (s.tex_depth) {
    case 0: {
        uint16_t w = at(s.tex_x + u / 4, s.tex_y + v);
        int idx = (w >> ((u & 3) * 4)) & 0xF;
        return at(s.clut_x + idx, s.clut_y);
    }
    case 1: {
        uint16_t w = at(s.tex_x + u / 2, s.tex_y + v);
        int idx = (w >> ((u & 1) * 8)) & 0xFF;
        return at(s.clut_x + idx, s.clut_y);
    }
    default:
        return at(s.tex_x + u, s.tex_y + v);
    }
}

bool in_area(int x, int y) {
    return x >= gpu.area_x1 && x <= gpu.area_x2 && y >= gpu.area_y1 && y <= gpu.area_y2;
}

// Shade, blend and write one pixel. r/g/b are 8-bit vertex colors at this pixel.
void plot(const DrawState &s, int x, int y, int r, int g, int b, int u, int v, bool dither) {
    uint16_t &dst = gpu.vram[(y & 511) * 1024 + (x & 1023)];
    if (gpu.mask_check && (dst & 0x8000)) return;

    bool semi = s.semi;
    uint16_t mask_bit = gpu.mask_set ? 0x8000 : 0;
    int cr, cg, cb;
    if (s.textured) {
        uint16_t t = texel(s, u, v);
        if (t == 0) return;  // fully transparent texel
        semi = semi && (t & 0x8000);
        mask_bit |= t & 0x8000;
        int tr = (t & 0x1F) << 3, tg = ((t >> 5) & 0x1F) << 3, tb = ((t >> 10) & 0x1F) << 3;
        if (s.raw) {
            cr = tr; cg = tg; cb = tb;
            dither = false;
        } else {
            cr = (tr * r) >> 7; cg = (tg * g) >> 7; cb = (tb * b) >> 7;
        }
    } else {
        cr = r; cg = g; cb = b;
    }
    if (dither) {
        int d = DITHER[y & 3][x & 3];
        cr += d; cg += d; cb += d;
    }
    cr = std::clamp(cr, 0, 255) >> 3;
    cg = std::clamp(cg, 0, 255) >> 3;
    cb = std::clamp(cb, 0, 255) >> 3;

    if (semi) {
        int br = dst & 0x1F, bg = (dst >> 5) & 0x1F, bb = (dst >> 10) & 0x1F;
        switch (s.semi_mode) {
        case 0: cr = (br + cr) >> 1; cg = (bg + cg) >> 1; cb = (bb + cb) >> 1; break;
        case 1: cr = std::min(31, br + cr); cg = std::min(31, bg + cg); cb = std::min(31, bb + cb); break;
        case 2: cr = std::max(0, br - cr); cg = std::max(0, bg - cg); cb = std::max(0, bb - cb); break;
        case 3: cr = std::min(31, br + (cr >> 2)); cg = std::min(31, bg + (cg >> 2)); cb = std::min(31, bb + (cb >> 2)); break;
        }
    }
    dst = uint16_t(cr | (cg << 5) | (cb << 10) | mask_bit);
}

// Triangle rasterization with attribute plane equations (affine, like the hardware).
void triangle(const DrawState &s, Vertex a, Vertex b, Vertex c) {
    // The GPU rejects polygons with very large extents.
    int minx = std::min({a.x, b.x, c.x}), maxx = std::max({a.x, b.x, c.x});
    int miny = std::min({a.y, b.y, c.y}), maxy = std::max({a.y, b.y, c.y});
    if (maxx - minx >= 1024 || maxy - miny >= 512) return;

    int64_t area = int64_t(b.x - a.x) * (c.y - a.y) - int64_t(c.x - a.x) * (b.y - a.y);
    if (area == 0) return;
    if (area < 0) {
        std::swap(b, c);
        area = -area;
    }

    // Attribute gradients in 16.16 fixed point.
    auto grad = [&](int va, int vb, int vc, int64_t &dx, int64_t &dy) {
        dx = ((int64_t(vb - va) * (c.y - a.y) - int64_t(vc - va) * (b.y - a.y)) << 16) / area;
        dy = ((int64_t(b.x - a.x) * (vc - va) - int64_t(c.x - a.x) * (vb - va)) << 16) / area;
    };
    int64_t drdx, drdy, dgdx, dgdy, dbdx, dbdy, dudx, dudy, dvdx, dvdy;
    grad(a.r, b.r, c.r, drdx, drdy);
    grad(a.g, b.g, c.g, dgdx, dgdy);
    grad(a.b, b.b, c.b, dbdx, dbdy);
    grad(a.u, b.u, c.u, dudx, dudy);
    grad(a.v, b.v, c.v, dvdx, dvdy);

    if (hwgl::active()) hwgl::triangle(s, a, b, c);

    minx = std::max(minx, gpu.area_x1);
    maxx = std::min(maxx, gpu.area_x2);
    miny = std::max(miny, gpu.area_y1);
    maxy = std::min(maxy, gpu.area_y2);
    if (minx <= maxx && miny <= maxy) hwgl::vram_changed(minx, miny, maxx - minx + 1, maxy - miny + 1);

    // Edge functions. Pixels exactly on an edge belong to only one of two triangles sharing
    // it, so quads don't double-blend.
    auto edge = [](const Vertex &p, const Vertex &q, int x, int y) {
        return int64_t(q.x - p.x) * (y - p.y) - int64_t(q.y - p.y) * (x - p.x);
    };
    // Like the GPU, draw left and top edges but not right and bottom ones. With positive area
    // (y down), left edges run upward and top edges run rightward.
    auto top_left = [](const Vertex &p, const Vertex &q) {
        return (q.y < p.y) || (p.y == q.y && q.x > p.x);
    };
    int64_t bias0 = top_left(b, c) ? 0 : -1, bias1 = top_left(c, a) ? 0 : -1, bias2 = top_left(a, b) ? 0 : -1;
    bool dither = s.dither && (s.shaded || (s.textured && !s.raw));

    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            // With area > 0 every edge function is non-negative inside the triangle.
            int64_t w0 = edge(b, c, x, y) + bias0, w1 = edge(c, a, x, y) + bias1, w2 = edge(a, b, x, y) + bias2;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            int64_t fx = x - a.x, fy = y - a.y;
            int r = a.r + int((drdx * fx + drdy * fy + 0x8000) >> 16);
            int g = a.g + int((dgdx * fx + dgdy * fy + 0x8000) >> 16);
            int bl = a.b + int((dbdx * fx + dbdy * fy + 0x8000) >> 16);
            int u = a.u + int((dudx * fx + dudy * fy) >> 16);
            int v = a.v + int((dvdx * fx + dvdy * fy) >> 16);
            plot(s, x, y, std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(bl, 0, 255), u, v, dither);
        }
    }
}

void line(const DrawState &s, const Vertex &a, const Vertex &b) {
    int dx = b.x - a.x, dy = b.y - a.y;
    if (std::abs(dx) >= 1024 || std::abs(dy) >= 512) return;
    int steps = std::max(std::abs(dx), std::abs(dy));
    bool dither = s.dither && s.shaded;
    if (hwgl::active()) hwgl::line(s, a, b);
    hwgl::vram_changed(std::min(a.x, b.x), std::min(a.y, b.y), std::abs(dx) + 1, std::abs(dy) + 1);
    for (int i = 0; i <= steps; i++) {
        int x = steps ? a.x + (dx * i + (dx >= 0 ? steps / 2 : -steps / 2)) / steps : a.x;
        int y = steps ? a.y + (dy * i + (dy >= 0 ? steps / 2 : -steps / 2)) / steps : a.y;
        if (!in_area(x, y)) continue;
        auto lerp = [&](int p, int q) { return steps ? p + (q - p) * i / steps : p; };
        plot(s, x, y, lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), 0, 0, dither);
    }
}

void draw_polygon(const std::vector<uint32_t> &w) {
    uint32_t op = w[0] >> 24;
    DrawState s{};
    s.shaded = op & 0x10;
    s.textured = op & 0x04;
    s.semi = op & 0x02;
    s.raw = op & 0x01;
    s.dither = gpu.texpage & 0x200;
    apply_texpage(s, gpu.texpage);
    int nverts = (op & 0x08) ? 4 : 3;
    Vertex v[4];
    size_t i = 1;
    uint32_t color = w[0];
    for (int n = 0; n < nverts; n++) {
        if (n > 0 && s.shaded) color = w[i++];
        v[n] = vertex_xy(w[i++]);
        set_color(v[n], s.shaded ? color : w[0]);
        if (s.textured) {
            uint32_t t = w[i++];
            v[n].u = t & 0xFF;
            v[n].v = (t >> 8) & 0xFF;
            if (n == 0) {
                s.clut_x = ((t >> 16) & 0x3F) * 16;
                s.clut_y = (t >> 22) & 0x1FF;
            } else if (n == 1) {
                // The polygon's texpage also becomes the current one.
                uint32_t tp = t >> 16;
                gpu.texpage = (gpu.texpage & ~0x9FFu) | (tp & 0x9FF);
                gpu.stat = (gpu.stat & ~0x81FFu) | (tp & 0x1FF) | ((tp & 0x800) << 4);
                apply_texpage(s, tp);
            }
        }
    }
    if (s.textured && s.raw) for (auto &vx : v) vx.r = vx.g = vx.b = 0x80;
    triangle(s, v[0], v[1], v[2]);
    if (nverts == 4) triangle(s, v[1], v[2], v[3]);
}

void draw_line(const std::vector<uint32_t> &w) {
    uint32_t op = w[0] >> 24;
    DrawState s{};
    s.shaded = op & 0x10;
    s.semi = op & 0x02;
    s.dither = gpu.texpage & 0x200;
    apply_texpage(s, gpu.texpage);
    std::vector<Vertex> pts;
    size_t i = 1;
    uint32_t color = w[0];
    while (i < w.size()) {
        if (pts.size() > 0 && s.shaded) {
            if ((w[i] & 0xF000F000) == 0x50005000 && (op & 0x08)) break;
            color = w[i++];
            if (i >= w.size()) break;
        }
        if ((w[i] & 0xF000F000) == 0x50005000 && (op & 0x08) && pts.size() >= 2) break;
        Vertex v = vertex_xy(w[i++]);
        set_color(v, s.shaded ? color : w[0]);
        pts.push_back(v);
        if (!(op & 0x08) && pts.size() == 2) break;
    }
    for (size_t n = 1; n < pts.size(); n++) line(s, pts[n - 1], pts[n]);
}

void draw_rect(const std::vector<uint32_t> &w) {
    uint32_t op = w[0] >> 24;
    DrawState s{};
    s.textured = op & 0x04;
    s.semi = op & 0x02;
    s.raw = op & 0x01;
    apply_texpage(s, gpu.texpage);
    size_t i = 1;
    Vertex base = vertex_xy(w[i++]);
    set_color(base, w[0]);
    int u0 = 0, v0 = 0;
    if (s.textured) {
        uint32_t t = w[i++];
        u0 = t & 0xFF;
        v0 = (t >> 8) & 0xFF;
        s.clut_x = ((t >> 16) & 0x3F) * 16;
        s.clut_y = (t >> 22) & 0x1FF;
    }
    int ww, hh;
    switch ((op >> 3) & 3) {
    case 0: ww = w[i] & 0x3FF; hh = (w[i] >> 16) & 0x1FF; break;
    case 1: ww = hh = 1; break;
    case 2: ww = hh = 8; break;
    default: ww = hh = 16; break;
    }
    int r = base.r, g = base.g, b = base.b;
    if (s.textured && s.raw) r = g = b = 0x80;
    if (hwgl::active()) {
        Vertex v = base;
        v.r = r; v.g = g; v.b = b; v.u = u0; v.v = v0;
        hwgl::rect(s, v, ww, hh);
    }
    hwgl::vram_changed(base.x, base.y, ww, hh);
    for (int y = 0; y < hh; y++)
        for (int x = 0; x < ww; x++) {
            int px = base.x + x, py = base.y + y;
            if (!in_area(px, py)) continue;
            plot(s, px, py, r, g, b, u0 + x, v0 + y, false);
        }
}

}  // namespace

// Debugging: VALOR_TRACE_GPU="frame,x,y,w,h" logs commands drawn on that frame whose
// vertices fall inside the rectangle (screen coordinates, before the drawing offset).
static void trace_command(const std::vector<uint32_t> &cmd) {
    static int frame = -1, rx, ry, rw, rh;
    if (frame == -1) {
        const char *e = getenv("VALOR_TRACE_GPU");
        frame = -2;
        if (e && sscanf(e, "%d,%d,%d,%d,%d", &frame, &rx, &ry, &rw, &rh) != 5) frame = -2;
    }
    if (frame < 0 || gpu.frames < uint64_t(frame) || gpu.frames > uint64_t(frame) + 1) return;  // two refreshes: one game frame
    bool hit = false;
    for (size_t i = 1; i < cmd.size(); i++) {
        int x = sext11(cmd[i]), y = sext11(cmd[i] >> 16);  // screen-relative (before the offset)
        if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) hit = true;
    }
    if (!hit) return;
    std::string words;
    char b[12];
    for (uint32_t w : cmd) { snprintf(b, sizeof b, " %08X", w); words += b; }
    log("gpu: off(%d,%d) tp=%04X tw=%05X @%08X%s", gpu.offset_x, gpu.offset_y, gpu.texpage, gpu.tex_window,
        gpu_packet_addr, words.c_str());
}

void gpu_draw(const std::vector<uint32_t> &cmd) {
    gpu.primitives++;
    overlay_counters.primitives++;
    trace_command(cmd);
    uint32_t op = cmd[0] >> 24;
    if (op < 0x40) draw_polygon(cmd);
    else if (op < 0x60) draw_line(cmd);
    else draw_rect(cmd);
}

}  // namespace psx::hw
