#include "valor/level.h"

#include <cstdio>

#include "valor/io.h"

namespace valor {

namespace {

constexpr size_t kBody = 0x800;  // BSD offsets are body offsets: file offset - 0x800
constexpr uint32_t kPlayerStartClass = 0x8877B646;

struct Reader {
    const std::vector<uint8_t>& d;
    bool ok = true;
    uint32_t u32(size_t o) {
        if (o + 4 > d.size()) return ok = false, 0;
        return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8) | (uint32_t(d[o + 2]) << 16) | (uint32_t(d[o + 3]) << 24);
    }
    int16_t s16(size_t o) {
        if (o + 2 > d.size()) return ok = false, 0;
        return int16_t(d[o] | (d[o + 1] << 8));
    }
    uint8_t u8(size_t o) {
        if (o >= d.size()) return ok = false, 0;
        return d[o];
    }
};

}  // namespace

bool LevelInfo::load(const std::string& data_dir, int m, int l, std::string* err) {
    char rel[64];
    std::snprintf(rel, sizeof rel, "/MSN%d/LVL%d/%d_%d.BSD", m, l, m, l);
    std::vector<uint8_t> d;
    if (!read_file(data_dir + rel, d)) {
        if (err) *err = "can't read " + data_dir + rel;
        return false;
    }
    Reader r{d};
    mission = m, level = l;
    const size_t h = kBody;
    tsp_count = r.u32(h + 0x80);
    chunk_slots = r.u32(h + 0x84);
    first_chunk = r.u32(h + 0x88);
    fog_near = r.s16(h + 0xCC);
    for (int i = 0; i < 3; i++) fog_rgb[i] = r.u8(h + 0xCE + i);
    uint32_t n_lights = r.u32(h + 0xD8);
    lights.clear();
    for (uint32_t i = 0; i < n_lights && i < 40; i++) {
        size_t e = h + 0xDC + 0x14 * i;
        uint32_t n = r.u32(e), off = r.u32(e + 4);
        AnimLight light;
        light.step = r.u32(e + 8);
        for (uint32_t k = 0; k < n && r.ok; k++) light.steps.push_back(r.u32(h + off + 4 * k));
        lights.push_back(std::move(light));
    }
    // Stored R, B, G (formats.md).
    sky_rgb[0] = r.u8(h + 0x58C), sky_rgb[2] = r.u8(h + 0x58D), sky_rgb[1] = r.u8(h + 0x58E);
    view_distance = r.u32(h + 0x590);
    if (!view_distance) view_distance = 4500;

    // The entity table: count, size, two words, count pairs {state, record offset from the first
    // record}; the player start is the class 0x8877B646 record whose +0x34 is the player number.
    size_t et = h + r.u32(h + 0x53C);
    uint32_t count = r.u32(et);
    size_t base = et + 16 + size_t(count) * 8;
    bool found = false;
    for (uint32_t i = 0; i < count && r.ok && !found; i++) {
        size_t rec = base + r.u32(et + 16 + i * 8 + 4);
        if (r.u32(rec) == kPlayerStartClass && r.s16(rec + 0x34) == 0) {
            for (int k = 0; k < 3; k++) start.pos[k] = r.s16(rec + 0x10 + 2 * k);
            start.yaw = r.s16(rec + 0x1A);
            found = true;
        }
    }
    if (!r.ok || !found) {
        if (err) *err = std::string("bad BSD or no player start: ") + rel;
        return false;
    }
    return true;
}

}  // namespace valor
