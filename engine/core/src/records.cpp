#include "valor/records.h"

#include <cstdlib>

#include "valor/fixed.h"

namespace valor {

namespace {

constexpr size_t kBody = 0x800;

struct Rd {
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
    uint8_t u8(size_t o) {
        if (o >= d.size()) return ok = false, 0;
        return d[o];
    }
};

// RotMatrixYXZ (exe 0x8001AF98) of halfword angles, as the GTE's rows.
void rot_yxz(int32_t ax, int32_t ay, int32_t az, int32_t m[3][3]) {
    SinCos x = sincos_angle(ax), y = sincos_angle(ay), z = sincos_angle(az);
    int64_t sx = x.s, cx = x.c, sy = y.s, cy = y.c, sz = z.s, cz = z.c;
    m[1][2] = s16(-sx);
    m[0][2] = s16((sy * cx) >> 12);
    m[2][2] = s16((cy * cx) >> 12);
    m[1][0] = s16((sz * cx) >> 12);
    m[1][1] = s16((cz * cx) >> 12);
    int64_t t = (sy * sx) >> 12;
    m[0][0] = s16(((cy * cz) >> 12) + ((t * sz) >> 12));
    m[0][1] = s16(-((cy * sz) >> 12) + ((t * cz) >> 12));
    t = (cy * sx) >> 12;
    m[2][1] = s16(((sy * sz) >> 12) + ((t * cz) >> 12));
    m[2][0] = s16(-((sy * cz) >> 12) + ((t * sz) >> 12));
}

}  // namespace

bool Record::contains(const int32_t p[3]) const {
    int64_t a = shape[1], b = shape[2], c = shape[3];
    int64_t cx = pos[0], cy = pos[1], cz = pos[2];
    switch (shape[0]) {
    case 0: {  // sphere: the GTE squares s16 differences
        int64_t d0 = s16(p[0] - cx), d1 = s16(p[1] - cy), d2 = s16(p[2] - cz);
        return s32(a * a - (d0 * d0 + d1 * d1 + d2 * d2)) > 0;
    }
    case 1: {  // cylinder
        int64_t lo = -b, hi = -c;
        if (hi < lo) std::swap(lo, hi);
        int64_t dy = p[1] - cy;
        if (dy < lo || hi < dy) return false;
        int64_t dx = p[0] - cx, dz = p[2] - cz;
        if (a < -dx || a < dx || a < -dz || a < dz) return false;
        return s32(a * a - (dx * dx + dz * dz)) > 0;
    }
    case 2: {  // box: an axis-aligned pre-test, then the turned one
        int64_t hx = iabs(a) >> 1, hy = iabs(b) >> 1, hz = iabs(c) >> 1;
        if (hx < iabs(p[0] - cx) || hy < iabs(p[1] - cy) || hz < iabs(p[2] - cz)) return false;
        int32_t ax = s16(-ang[0]), ay = s16(-ang[1]), az = s16(-ang[2]);
        if (!ax && !ay && !az) return true;
        int32_t m[3][3];
        rot_yxz(ax, ay, az, m);
        int64_t v[3] = {s16(cx - p[0]), s16(cy - p[1]), s16(cz - p[2])};
        int64_t r[3];
        for (int i = 0; i < 3; i++) r[i] = iabs((m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2]) >> 12);
        return !(hx < r[0] || hy < r[1] || hz < r[2]);
    }
    }
    return false;
}

bool Records::load(const std::vector<uint8_t>& d, std::string* err) {
    Rd r{d};
    records.clear();
    zones.clear();
    // The entity table: count, size, two words, count pairs {state, offset from the first record}.
    size_t et = kBody + r.u32(kBody + 0x53C);
    uint32_t count = r.u32(et);
    size_t base = et + 16 + size_t(count) * 8;
    for (uint32_t i = 0; i < count && r.ok; i++) {
        size_t a = base + r.u32(et + 16 + i * 8 + 4);
        Record rec;
        rec.cls = r.u32(a);
        rec.kind = r.u32(a + 0xC);
        for (int k = 0; k < 3; k++) rec.pos[k] = int16_t(r.u16(a + 0x10 + 2 * k));
        for (int k = 0; k < 3; k++) rec.ang[k] = int16_t(r.u16(a + 0x18 + 2 * k));
        for (int k = 0; k < 4; k++) rec.shape[k] = int16_t(r.u16(a + 0x28 + 2 * k));
        if (rec.kind == 0) {
            if (rec.cls == kClassStream)
                for (int k = 0; k < 4; k++) rec.chunks[k] = int16_t(r.u16(a + 0x30 + 2 * k));
        } else {
            // The list offsets: 0 is no list.
            rec.enter = r.u32(a + 0x38), rec.leave = r.u32(a + 0x3C);
            rec.c_enter = r.u32(a + 0x40), rec.c_leave = r.u32(a + 0x44);
            rec.use = r.u32(a + 0x4C);
        }
        records.push_back(rec);
    }
    // zones_parse (0x80086F98): s32 the highest zone id (-1: none), then per zone a byte holding
    // 255 minus the count, a pad byte, and the count u16 record indices.
    size_t zo = kBody + r.u32(kBody + 0x598);
    int32_t n = int32_t(r.u32(zo));
    size_t p = zo + 4;
    for (int32_t z = 0; z <= n && r.ok; z++) {
        int cnt = 0xFF - r.u8(p);
        std::vector<uint16_t> list;
        for (int k = 0; k < cnt; k++) list.push_back(r.u16(p + 2 + 2 * k));
        zones.push_back(std::move(list));
        p += 2 + 2 * size_t(cnt);
    }
    if (!r.ok) {
        if (err) *err = "bad BSD records or zone lists";
        return false;
    }
    return true;
}

const std::vector<uint16_t>& Records::zone(uint16_t z) const {
    static const std::vector<uint16_t> none;
    return z < zones.size() ? zones[z] : none;
}

void ZoneTracker::update(const Records& recs, uint16_t z, const int32_t pos[3], bool is_player,
                         const std::function<void(int, bool)>& event) {
    const std::vector<uint16_t>& idx = recs.zone(z);
    uint32_t b = 0;
    for (size_t k = 0; k < idx.size() && k < 32; k++) {
        const Record& r = recs.records[idx[k]];
        if (is_player) {
            if (r.kind != 0 && !r.enter && !r.leave && !r.use) continue;
        } else if (r.kind == 0 || (!r.c_enter && !r.c_leave)) {
            continue;
        }
        if (r.contains(pos)) b |= 1u << k;
    }
    zone = z, bits = b;
    uint16_t pz = prev_zone;
    uint32_t pb = prev_bits;
    const std::vector<uint16_t>& old = recs.zone(pz);
    if (z == 0xFFFF) {
        if (pz != z)
            for (size_t k = 0; k < old.size() && k < 32; k++)
                if (pb >> k & 1) event(old[k], false);
    } else if (pz == 0xFFFF) {
        for (size_t k = 0; k < idx.size() && k < 32; k++)
            if (b >> k & 1) event(idx[k], true);
    } else if (z == pz) {
        uint32_t ch = b ^ pb;
        for (size_t k = 0; k < idx.size() && k < 32; k++)
            if ((pb & ch) >> k & 1) event(idx[k], false);
        for (size_t k = 0; k < idx.size() && k < 32; k++)
            if ((b & ch) >> k & 1) event(idx[k], true);
    } else {
        // The walk of the new and the old sorted lists: a change is reported only for a record in
        // both, until one list ends; then the other's tail (levels.md, Triggers).
        size_t i = 0, j = 0;
        const std::vector<uint16_t>& a = idx;
        while (i < a.size() && j < old.size()) {
            uint16_t s1 = a[i], s2 = old[j];
            if (s1 == s2) {
                bool was = pb >> j & 1, now = b >> i & 1;
                if (now != was) event(s1, now);
            }
            if (!(s2 < s1)) i++;
            if (s1 >= s2) j++;
        }
        if (i >= a.size())
            for (; j < old.size(); j++)
                if (pb >> j & 1) event(old[j], false);
        for (; i < a.size(); i++)
            if (b >> i & 1) event(a[i], true);
    }
    prev_zone = z, prev_bits = b;
}

void Streaming::start(int first_chunk, int slots) {
    for (int i = 0; i < 4; i++) slot_chunk[i] = i < slots ? first_chunk + i : -1;
    record = nullptr;
    loading = -1;
}

void Streaming::collision_slots(int out[4]) const {
    for (int i = 0; i < 4; i++) out[i] = i == loading ? -1 : slot_chunk[i];
}

bool Streaming::run() {
    if (!record) return false;
    auto listed = [&](int c) {
        for (int k = 0; k < 4; k++)
            if (record->chunks[k] == c) return true;
        return false;
    };
    // A read in progress for a chunk the record lists is left to finish; one it doesn't list is
    // replaced (the original prints a warning).
    if (loading != -1 && listed(slot_chunk[loading])) return false;
    auto loaded = [&](int c) {
        for (int k = 0; k < 4; k++)
            if (slot_chunk[k] == c) return true;
        return false;
    };
    int need = -1, slot = -1;
    for (int k = 0; k < 4 && need < 0; k++)  // chunk_stream_needed: the record's order
        if (!loaded(record->chunks[k])) need = record->chunks[k];
    for (int k = 0; k < 4 && slot < 0; k++)  // chunk_stream_evictable: the slots' order
        if (slot_chunk[k] != -1 && !listed(slot_chunk[k])) slot = k;
    if (need < 0 || slot < 0) return false;
    slot_chunk[slot] = need;
    loading = instant ? -1 : slot;
    loads++;
    return true;
}

}  // namespace valor
