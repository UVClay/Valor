// GPU command interface (GP0/GP1 at 0x1F801810/14) and VRAM.
#include <vector>

#include "gpu.h"
#include "hw.h"
#include "overlay.h"

namespace psx::hw {

GpuState gpu;

namespace {

// Words per GP0 command (including the command word); polylines are variable.
int command_length(uint32_t cmd) {
    uint32_t op = cmd >> 24;
    if (op >= 0x20 && op < 0x40) {
        bool quad = op & 0x08, textured = op & 0x04, shaded = op & 0x10;
        int verts = quad ? 4 : 3;
        int per_vertex = 1 + (textured ? 1 : 0) + (shaded ? 1 : 0);
        return 1 + verts * per_vertex - (shaded ? 1 : 0);
    }
    if (op >= 0x40 && op < 0x60) {
        if (op & 0x08) return -1;  // polyline: until terminator
        return (op & 0x10) ? 4 : 3;
    }
    if (op >= 0x60 && op < 0x80) {
        int size = (op >> 3) & 3;
        return 2 + ((op & 0x04) ? 1 : 0) + (size == 0 ? 1 : 0);
    }
    if (op == 0x02) return 3;
    if (op >= 0x80 && op < 0xA0) return 4;
    if (op >= 0xA0 && op < 0xC0) return 3;
    if (op >= 0xC0 && op < 0xE0) return 3;
    return 1;
}

std::vector<uint32_t> fifo;
int expected;

// CPU -> VRAM / VRAM -> CPU transfer in progress.
struct Transfer {
    int x, y, w, h, i, n;
} upload, download;
bool uploading, downloading;

uint16_t &vram_at(int x, int y) { return gpu.vram[(y & 511) * 1024 + (x & 1023)]; }

void begin_rect_transfer(Transfer &t, uint32_t xy, uint32_t wh) {
    t.x = xy & 0x3FF;
    t.y = (xy >> 16) & 0x1FF;
    t.w = ((wh & 0xFFFF) - 1) % 1024 + 1;
    t.h = (((wh >> 16) & 0xFFFF) - 1) % 512 + 1;
    t.i = 0;
    t.n = t.w * t.h;
}

void upload_halfword(uint16_t v) {
    Transfer &t = upload;
    int x = t.x + t.i % t.w, y = t.y + t.i / t.w;
    uint16_t &px = vram_at(x, y);
    if (!(gpu.mask_check && (px & 0x8000))) px = v | (gpu.mask_set ? 0x8000 : 0);
    if (++t.i >= t.n) {
        uploading = false;
        hwgl::upload(t.x, t.y, t.w, t.h);
    }
}

void execute(const std::vector<uint32_t> &w) {
    uint32_t op = w[0] >> 24;
    if (op == 0x02) {  // fill rectangle (ignores mask and drawing area)
        int x0 = w[1] & 0x3F0, y0 = (w[1] >> 16) & 0x1FF;
        int ww = ((w[2] & 0x3FF) + 15) & ~15, hh = (w[2] >> 16) & 0x1FF;
        uint32_t c = w[0];
        uint16_t px = uint16_t(((c >> 3) & 31) | (((c >> 11) & 31) << 5) | (((c >> 19) & 31) << 10));
        for (int y = 0; y < hh; y++)
            for (int x = 0; x < ww; x++) vram_at(x0 + x, y0 + y) = px;
        hwgl::fill(x0, y0, ww, hh, c);
    } else if (op >= 0x20 && op < 0x80) {
        gpu_draw(w);
    } else if (op >= 0x80 && op < 0xA0) {  // VRAM -> VRAM copy
        int sx = w[1] & 0x3FF, sy = (w[1] >> 16) & 0x1FF, dx = w[2] & 0x3FF, dy = (w[2] >> 16) & 0x1FF;
        int ww = ((w[3] & 0xFFFF) - 1) % 1024 + 1, hh = (((w[3] >> 16) & 0xFFFF) - 1) % 512 + 1;
        std::vector<uint16_t> tmp(size_t(ww) * hh);
        for (int y = 0; y < hh; y++)
            for (int x = 0; x < ww; x++) tmp[size_t(y) * ww + x] = vram_at(sx + x, sy + y);
        for (int y = 0; y < hh; y++)
            for (int x = 0; x < ww; x++) {
                uint16_t &px = vram_at(dx + x, dy + y);
                if (!(gpu.mask_check && (px & 0x8000))) px = tmp[size_t(y) * ww + x] | (gpu.mask_set ? 0x8000 : 0);
            }
        hwgl::copy(sx, sy, dx, dy, ww, hh);
    } else if (op >= 0xA0 && op < 0xC0) {
        begin_rect_transfer(upload, w[1], w[2]);
        uploading = true;
    } else if (op >= 0xC0 && op < 0xE0) {
        begin_rect_transfer(download, w[1], w[2]);
        downloading = true;
    } else if (op == 0xE1) {
        gpu.texpage = w[0] & 0x3FFF;
        gpu.stat = (gpu.stat & ~0x87FFu) | (w[0] & 0x7FF) | ((w[0] & 0x800) << 4);
    } else if (op == 0xE2) {
        gpu.tex_window = w[0] & 0xFFFFF;
    } else if (op == 0xE3) {
        gpu.area_x1 = w[0] & 0x3FF;
        gpu.area_y1 = (w[0] >> 10) & 0x3FF;
    } else if (op == 0xE4) {
        gpu.area_x2 = w[0] & 0x3FF;
        gpu.area_y2 = (w[0] >> 10) & 0x3FF;
    } else if (op == 0xE5) {
        gpu.offset_x = int32_t(w[0] << 21) >> 21;
        gpu.offset_y = int32_t((w[0] >> 11) << 21) >> 21;
    } else if (op == 0xE6) {
        gpu.mask_set = w[0] & 1;
        gpu.mask_check = w[0] & 2;
        gpu.stat = (gpu.stat & ~0x1800u) | ((w[0] & 3) << 11);
    } else if (op == 0x1F) {
        gpu.stat |= 1u << 24;
        irq_raise(IRQ_GPU);
    }
}

}  // namespace

void gpu_init() {
    gpu = GpuState{};
    gpu.vram.assign(1024 * 512, 0);
}

void gpu_gp0(uint32_t word) {
    if (uploading) {
        upload_halfword(word & 0xFFFF);
        if (uploading) upload_halfword(word >> 16);
        return;
    }
    if (fifo.empty()) expected = command_length(word);
    fifo.push_back(word);
    if (expected < 0) {  // polyline: terminated by 0x5xxx5xxx after at least two vertices
        bool shaded = (fifo[0] >> 24) & 0x10;
        size_t min = shaded ? 4 : 3;
        if (fifo.size() < min || (word & 0xF000F000) != 0x50005000) return;
    } else if (int(fifo.size()) < expected) {
        return;
    }
    std::vector<uint32_t> cmd;
    cmd.swap(fifo);
    execute(cmd);
}

uint32_t gpu_read_data() {
    if (!downloading) return gpu.read_latch;
    uint32_t v = 0;
    for (int half = 0; half < 2 && downloading; half++) {
        Transfer &t = download;
        v |= uint32_t(vram_at(t.x + t.i % t.w, t.y + t.i / t.w)) << (16 * half);
        if (++t.i >= t.n) downloading = false;
    }
    return v;
}

static void gp1(uint32_t v) {
    uint32_t op = (v >> 24) & 0x3F;
    switch (op) {
    case 0x00:  // reset
        fifo.clear();
        uploading = downloading = false;
        gpu.stat = 0x14802000;
        gpu.display_disabled = true;
        break;
    case 0x01: fifo.clear(); uploading = downloading = false; break;
    case 0x02: gpu.stat &= ~(1u << 24); break;
    case 0x03:
        gpu.display_disabled = v & 1;
        gpu.stat = (gpu.stat & ~(1u << 23)) | ((v & 1) << 23);
        break;
    case 0x04: gpu.stat = (gpu.stat & ~0x60000000u) | ((v & 3) << 29); break;
    case 0x05: {
        int x = v & 0x3FE, y = (v >> 10) & 0x1FF;
        if (x != gpu.display_x || y != gpu.display_y) overlay_note_flip();  // the game shows a new frame
        gpu.display_x = x;
        gpu.display_y = y;
        break;
    }
    case 0x06: gpu.hrange = v & 0xFFFFFF; break;
    case 0x07: gpu.vrange = v & 0xFFFFF; break;
    case 0x08:
        gpu.display_mode = v & 0xFF;
        gpu.stat = (gpu.stat & ~0x7F4000u) | ((v & 0x3F) << 17) | ((v & 0x40) << 10) | ((v & 0x80) << 7);
        break;
    case 0x10:
        switch (v & 7) {
        case 2: gpu.read_latch = gpu.tex_window; break;
        case 3: gpu.read_latch = gpu.area_x1 | (gpu.area_y1 << 10); break;
        case 4: gpu.read_latch = gpu.area_x2 | (gpu.area_y2 << 10); break;
        case 5: gpu.read_latch = (gpu.offset_x & 0x7FF) | ((gpu.offset_y & 0x7FF) << 11); break;
        case 7: gpu.read_latch = 2; break;  // GPU version
        }
        break;
    }
}

uint32_t gpu_read(uint32_t phys) {
    if (phys == 0x1F801810) return gpu_read_data();
    // Always ready: commands, VRAM->CPU, DMA block. Bit 25 mirrors the DMA direction's request.
    uint32_t s = gpu.stat | 0x1C000000;
    uint32_t dir = (s >> 29) & 3;
    if (dir == 1 || dir == 2) s |= 1u << 25;
    else if (dir == 3) s |= (downloading ? 1u : 0) << 25;
    s ^= gpu.odd_field << 31;
    return s;
}

void gpu_write(uint32_t phys, uint32_t v) {
    if (phys == 0x1F801810) gpu_gp0(v);
    else gp1(v);
}

void gpu_vblank() {
    gpu.odd_field ^= 1;
    gpu.frames++;
    gpu_present();
}

}  // namespace psx::hw
