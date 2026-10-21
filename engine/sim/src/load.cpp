// The level's starting state from the disc files (load.h). Each step names the original's routine;
// the order is theirs, so the heap hands out the same addresses.
#include "valor/sim/load.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>

#include "valor/io.h"
#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/scripts.h"

namespace valor::sim {

namespace {
constexpr U GP = 0x80035D1C;
constexpr I CHECK = 0xB562439F;
constexpr U G_GAME = 0x800385CC;
constexpr U G_PARAMS = 0x800939F4;
constexpr U G_LEVEL_PTR = 0x8009CC64;
constexpr U G_MODELS_PTR = 0x8009EC58, G_MODEL_COUNT = 0x8009EC54;
constexpr U G_SLOT_COUNT = 0x8009E874, G_SLOT_WEAPON = 0x8009E878;
constexpr U G_WEAPONS = 0x80093640;
constexpr U G_HANDLE_COUNT = 0x8009D674, G_HANDLE_SERIAL = 0x8009D670;
constexpr U G_CHAR_POOL = 0x800B7B08, G_POSE_POOL = 0x800C4378;
constexpr U G_ROLE_AXES = 0x800B74B0, G_ROLE_BUTTONS = 0x800B75F0, G_AXIS_CURVES = 0x800B7470;
constexpr U G_ACTIONS = 0x800B7790, G_ACTION_EXCL = 0x800B77E0;
constexpr U G_LOOK_MODE = 0x8009E3A0, G_AIM_PITCH_FROM_MOVE = 0x8009E398;
constexpr U G_SUBDIV = 0x800AA168;
constexpr U G_ZONE_RECORDS = 0x8009EC5C, G_PATHS_PTR = 0x8009E290, G_PATH_COUNT = 0x8009E28C;
constexpr U G_LEVEL_SCRIPTS = 0x8009D5AC, G_MSG_SUBS = 0x8009D5B0, G_MSG_FREE = 0x8009D5B4;
constexpr U G_MSG_HEAD = 0x8009D5B8, G_MSG_TAIL = 0x8009D5BC, POOL_NODES = 0x800AD460;
constexpr U G_CD_FILES = 0x80093B10;

bool read(const std::string& path, std::vector<uint8_t>& out) { return read_file(path, out); }
}  // namespace

// ---------------------------------------------------------------- the heap

void Heap::init(U base, U size) {
    U b = (base + 0xF) & ~U(0xF);
    I n = (size >> 4) - 2;
    m.w32(GP + 0x406C, n);
    m.w32(GP + 0x4064, n);
    m.w32(b, n);
    U end = U(b + (n << 4));
    m.w32(b + 0xC, CHECK - (n + b));
    m.w32(GP + 0x404C, base);
    m.w32(GP + 0x4044, size);
    m.w32(GP + 0x3F24, b);
    m.w32(GP + 0x403C, 0);
    m.w32(GP + 0x405C, 0);
    m.w32(b + 4, b);
    m.w32(b + 8, 0);
    m.w32(GP + 0x3F28, b);
    m.w32(end, 0);
    m.w32(end + 4, b);
    m.w32(end + 8, 0);
    m.w32(end + 0xC, CHECK - end);
    m.w32(GP + 0x3F2C, end);
    m.w32(GP + 0x4054, 0x10000);
    m.w32(GP + 0x4034, base);
    m.w32(GP + 0x4038, size);
}

U Heap::malloc(I size, I tag, I flags) {
    if (size == 0) return 0;
    I n = ((size + 0xF) >> 4) + 1;
    U r8 = U(m.u32(GP + 0x3F24));
    U start = r8;
    U r7 = U(m.u32(r8 + 4));
    I r4;
    for (;;) {
        r4 = m.u32(r7);
        if (r4 >= n) break;
        bool back = r7 == start;
        r8 = r7;
        if (back) return 0;
        r7 = U(m.u32(r7 + 4));
    }
    if (r4 == n) {
        if (r7 == r8) return 0;
        I nx = m.u32(r7 + 4);
        m.w32(r8 + 4, nx);
        m.w32(r8 + 0xC, CHECK - (m.u32(r8) + nx + m.u32(r8 + 8)));
    } else {
        I nx = m.u32(r7 + 4), fl = m.u32(r7 + 8);
        r4 -= n;
        m.w32(r7, r4);
        m.w32(r7 + 0xC, CHECK - (r4 + nx + fl));
        r7 = U(r7 + (r4 << 4));
    }
    m.w32(GP + 0x3F24, r8);
    I serial = m.u32(GP + 0x4054);
    I f = (flags | 0x80000000) | serial;
    m.w32(r7 + 0xC, CHECK - (n + tag + f));
    m.w32(r7, n);
    m.w32(r7 + 4, tag);
    m.w32(r7 + 8, f);
    I fr = m.u32(GP + 0x406C) - n;
    m.w32(GP + 0x406C, fr);
    I used = m.u32(GP + 0x405C) + n;
    m.w32(GP + 0x405C, used);
    if (m.s32(GP + 0x403C) < S32(used)) m.w32(GP + 0x403C, used);
    if (S32(fr) < m.s32(GP + 0x4064)) m.w32(GP + 0x4064, fr);
    m.w32(GP + 0x4054, serial + 0x10000);
    return r7 + 0x10;
}

U Heap::calloc(I size, I count) {
    if (count != 1) size = S32(size * count);
    if (size == 0) return 0;
    U p = malloc(size, 0, 0);
    if (!p) return 0;
    m.zero(p, (m.u32(p - 0x10) - 1) << 4);
    return p;
}

void Heap::free(U p) {
    U t0 = p - 0x10;
    I a3f = m.s32(t0 + 8);
    if (U32(m.u32(t0) + m.u32(t0 + 4) + m.u32(t0 + 8) + m.u32(t0 + 0xC)) != CHECK) return;
    if (a3f >= 0) return;
    U a3 = U(m.u32(GP + 0x3F24));
    m.w32(t0 + 8, 0);
    I size = m.u32(t0);
    m.w32(GP + 0x406C, m.u32(GP + 0x406C) + size);
    m.w32(GP + 0x405C, m.u32(GP + 0x405C) - size);
    U a2 = U(m.u32(a3 + 4));
    bool v1 = a3 < t0;
    if (a3 != a2) {
        while ((!v1 || t0 >= a2) && (a3 < a2 || (!v1 && t0 >= a2))) {
            a3 = a2;
            a2 = U(m.u32(a3 + 4));
            v1 = a3 < t0;
        }
    }
    I ts = m.u32(t0);
    if (U(t0 + (ts << 4)) == a2) {
        m.w32(t0, ts + m.u32(a2));
        m.w32(t0 + 4, m.u32(a2 + 4));
        if (a3 == a2) a3 = t0;
    } else
        m.w32(t0 + 4, a2);
    I as = m.u32(a3);
    if (U(a3 + (as << 4)) == t0) {
        m.w32(a3, as + m.u32(t0));
        I nx = m.u32(t0 + 4);
        t0 = a3;
        m.w32(a3 + 4, nx);
    } else {
        m.w32(a3 + 4, t0);
        m.w32(a3 + 0xC, CHECK - (as + t0 + m.u32(a3 + 8)));
    }
    m.w32(GP + 0x3F24, a3);
    m.w32(t0 + 0xC, CHECK - (m.u32(t0) + m.u32(t0 + 4) + m.u32(t0 + 8)));
}

// ---------------------------------------------------------------- the loader

namespace {

struct DiscFile {
    I lba = 0, size = 0;
};

struct Loader {
    Engine& eng;
    Mem& m;
    Heap heap;
    LevelBuild& out;
    int mission, level;
    std::map<std::string, DiscFile> disc;  // the manifest: upper-case path -> LBA, size

    std::string data() const { return eng.root + "/extracted/moh/DATA"; }
    std::string dir() const { return data() + "/MSN" + std::to_string(mission) + "/LVL" + std::to_string(level); }
    void note(const std::string& s) { out.notes.push_back(s); }
    U L() const { return U(m.u32(G_LEVEL_PTR)); }

    bool load_manifest() {
        std::ifstream f(eng.root + "/extracted/moh/manifest.json");
        std::string line, path;
        I lba = -1;
        while (std::getline(f, line)) {
            size_t p;
            if ((p = line.find("\"path\": \"")) != std::string::npos) {
                path = line.substr(p + 9, line.find('"', p + 9) - p - 9);
                for (char& c : path) c = char(std::toupper(c));
            } else if ((p = line.find("\"lba\": ")) != std::string::npos)
                lba = std::atoll(line.c_str() + p + 7);
            else if ((p = line.find("\"size\": ")) != std::string::npos && !path.empty())
                disc[path] = {lba, std::atoll(line.c_str() + p + 8)};
        }
        return !disc.empty();
    }

    U model_at(I i) const { return U(m.u32(G_MODELS_PTR) + (i << 8)); }
    // model_find (0x8006A910): the index of the hash, -1 none.
    I model_find(U hash) const {
        if (hash == 0xFFFFFFFF) return -1;
        for (I i = 0; i < m.u32(G_MODEL_COUNT); i++)
            if (U(m.u32(model_at(i))) == hash) return i;
        return -1;
    }

    // func_800363FC (file, 0, 0): the whole file into a block of its size + 1, a 0 after it.
    U file_read(const std::string& path, I* size) {
        std::vector<uint8_t> d;
        if (!read(path, d)) return 0;
        U p = heap.malloc(I(d.size()) + 1);
        if (!p) return 0;
        std::copy(d.begin(), d.end(), m.d.begin() + (p & Mem::kMask));
        m.w8(p + d.size(), 0);
        if (size) *size = I(d.size());
        return p;
    }

    // func_80049540: a CD directory entry {next, LBA, size, name} for a path, appended to the
    // list at 0x80093B10 (the path upper-cased, ";1" added; the name from after the last '\').
    U cd_entry(const std::string& path) {
        std::string up;
        if (path[0] != '\\') up = "\\";
        for (char c : path) up += char(uint8_t(c) >= 0x61 ? c - 0x20 : c);
        up += ";1";
        U tail = U(m.u32(G_CD_FILES));
        while (tail && m.u32(tail)) tail = U(m.u32(tail));
        U e = heap.malloc(0x1C);
        if (!e) return 0;
        m.w32(e, 0);
        std::string key = up.substr(1, up.size() - 3);
        for (char& c : key)
            if (c == '\\') c = '/';
        auto it = disc.find(key);
        if (it == disc.end()) note("no disc entry for " + key);
        m.w32(e + 4, it == disc.end() ? 0 : it->second.lba);
        m.w32(e + 8, it == disc.end() ? 0 : it->second.size);
        if (!m.u32(G_CD_FILES))
            m.w32(G_CD_FILES, e);
        else
            m.w32(tail, e);
        std::string name = up.substr(up.rfind('\\') + 1);
        for (size_t k = 0; k <= name.size(); k++) m.w8(e + 0xC + k, k < name.size() ? uint8_t(name[k]) : 0);
        return e;
    }

    // ---------------------------------------------------------------- entities

    // handle_alloc (0x8006D930).
    void handle_alloc(U e) {
        I used = m.u32(G_HANDLE_COUNT);
        I serial = m.u32(G_HANDLE_SERIAL) + 1;
        m.w32(G_HANDLE_SERIAL, serial);
        I slot;
        if (U32(used) < 0x100) {
            slot = used;
            m.w32(G_HANDLE_COUNT, used + 1);
        } else {
            slot = 0;
            while (slot < 0x100 && m.u32(G_HANDLES + 4 * slot)) slot++;
        }
        m.w32(G_HANDLES + 4 * slot, e);
        m.w32(G_SERIALS + 4 * slot, serial);
        m.w32(e, (serial << 16) | (slot & 0xFFFF));
        m.w32(G_REMOVED + 4 * slot, 0);
    }

    U char_alloc() {
        for (int i = 0; i < 5; i++) {
            U s = U(G_CHAR_POOL + 0x2814 * i);
            if (m.u32(s + 0x2810)) {
                m.w32(s + 0x280C, -0x43);
                m.w32(s + 0x2810, 0);
                m.zero(s, 0x280C);
                I a = m.s32(0x800AB8D0);
                if (m.s32(0x800AB8CC) >= a) a = m.s32(0x800AB8CC);
                m.w32(0x800AB8C8, a);
                return s;
            }
        }
        return 0;
    }

    U pose_alloc() {
        for (int i = 0; i < 5; i++) {
            U s = U(G_POSE_POOL + 0x1308 * i);
            if (m.u32(s + 0x1304)) {
                m.w32(s + 0x1300, -0x43);
                m.w32(s + 0x1304, 0);
                m.zero(s, 0x1300);
                I a = m.s32(0x800AB8D0) + 1;
                m.w32(0x800AB8D0, a);
                if (m.s32(0x800AB8CC) >= a) a = m.s32(0x800AB8CC);
                m.w32(0x800AB8C8, a);
                return s;
            }
        }
        return 0;
    }

    // entity_defaults (0x8006B350).
    void defaults(U e) {
        for (U o : {0x8u, 0xCu, 0x10u, 0x18u, 0x28u, 0x2Cu, 0x30u}) m.w32(e + o, 0);
        m.w32(e + 0xF4, 0x640000);
        m.w32(e + 0x294, 1);
        m.w32(e + 0x290, 1);
        for (U o : {0xF8u, 0x298u, 0x36Cu, 0x370u, 0x374u, 0x378u, 0x37Cu, 0x380u, 0x384u, 0x20Cu, 0x210u, 0x214u, 0x228u, 0x22Cu, 0x230u})
            m.w32(e + o, 0);
        for (int k = 0; k < 3; k++) m.w32(e + 0x270 + 4 * k, 0x10000);
        for (U o : {0x218u, 0x21Cu, 0x220u, 0x200u, 0x204u, 0x1FCu}) m.w32(e + o, 0);
        U inst = U(m.u32(e + 0x100));
        if (inst) {
            m.w16(inst + 0x5C, -1);
            m.w16(inst + 0x5E, 0);
            m.w16(inst + 0x62, -1);
            m.w16(inst + 0x64, 0);
            m.w16(inst + 0x3E, 0);
            m.w32(inst + 8, 0);
        }
        for (int ch = 0; ch < 2; ch++) {
            U c = U(e + 0x74 * ch);
            m.w32(c + 0x114, 0);
            m.w16(c + 0x11E, -1);
            for (U o : {0x104u, 0x108u, 0x10Cu, 0x110u}) m.w32(c + o, 0);
            m.w16(c + 0x122, 0);
            m.w32(c + 0x114, 0);
            m.w16(c + 0x120, 0);
            m.w16(c + 0x11C, -1);
        }
        m.w32(e + 0x288, 1);
        m.w32(e + 0x28C, 1);
        m.w32(e + 0x284, 1);
        for (U o : {0x24Cu, 0x250u, 0x254u, 0x1F4u}) m.w32(e + o, 0);
        m.w16(e + 0x36, 0);
        for (U o : {0x2DCu, 0x2DAu, 0x2D8u, 0x2E4u, 0x2E2u, 0x2E0u}) m.w16(e + o, 0);
        m.w16(e + 0x2E8, 0x64);
        m.w16(e + 0x2EC, 0);
        m.w32(e + 0x2D4, 0);
        // 0x8005D104: the variables +0x4C..+0xE8 and the bit word +0xEC cleared
        for (int k = 0; k < 0x28; k++) m.w32(e + 0x4C + 4 * k, 0);
        m.w32(e + 0xEC, 0);
        for (int k = 0; k < 3; k++) m.w8(e + 0x2A0 + k, 0x40);
        for (int k = 0; k < 3; k++) m.w16(e + 0x2A4 + 2 * k, 0x80);
        for (int k = 0; k < 3; k++) m.w16(e + 0x2AC + 2 * k, 0x1000);
        m.w32(e + 0x224, 0x10000);
        for (U o : {0x2B4u, 0x2B6u, 0x2B8u, 0x2BCu, 0x2BEu, 0x2C0u, 0x2C4u, 0x2C6u, 0x2C8u, 0x2CCu, 0x2CEu, 0x2D0u}) m.w16(e + o, 0);
        for (U o : {0x44u, 0x48u, 0x3Cu, 0x40u, 0x38u, 0x3A0u}) m.w32(e + o, 0);
        m.w16(e + 0x2EA, -1);
        defaults_tail(e);
    }
    void defaults_tail(U e);

    // model_instance_init (0x8006A96C): the pose buffers (one block: vertices, colours, the
    // pose's three quaternion arrays and joints), the scale, eye height and hit parts.
    I instance_init(U inst, U model, I flags, U e) {
        I size = 0;
        U buf = 0;
        I ok = 1;
        m.w32(inst + 0x6C, 0x80);
        m.w32(inst, 0);
        m.w32(inst + 0x68, 0);
        m.w32(inst + 4, flags);
        m.w32(inst + 8, model);
        if (!(flags & 1) && m.u32(model + 0x1C)) {
            if (m.u32(model + 0x2C)) size = (m.u32(m.u32(model + 0x2C)) << 4) + 0x14;
            if (m.u32(model + 0x30)) size = size + 8 + (m.u32(m.u32(model + 0x30)) << 2);
            if (m.u32(model + 0x34)) size = size + 0x14 + (m.u32(m.u32(model + 0x34)) << 4);
        }
        if (!(flags & 2)) size += m.u16(model + 0x8C) << 3;
        if (!m.u32(model + 0x14)) {
            if (!(flags & 0x40)) size += m.u16(model + 0x8C) << 3;
            if (!(flags & 0x100)) size += m.u16(model + 0x8C) << 2;
        }
        if (!(flags & 4)) size += m.u16(model + 0x8E) << 3;
        if (!(flags & 0x10)) size += 0x1000;
        if (model_find(0x42A7A5EF) >= 0) size += m.u16(model + 0xBE) * 0xB0;
        if (m.u32(model + 8)) {
            I nb = m.u8(m.u32(m.u32(model + 8) + 4) + 1) << 3;
            size += nb * 3;
        } else {
            for (U o : {0x74u, 0x7Cu, 0x84u, 0x88u, 0x90u, 0x78u, 0x80u, 0x8Cu, 0x94u}) m.w32(inst + o, 0);
            m.w16(inst + 0x70, 0);
            m.w16(inst + 0x72, 0);
        }
        if (size > 0) {
            I t = m.s32(model + 0xFC);
            buf = (t == 0x1402 || t == 0x1405) ? pose_alloc() : heap.malloc(size);
            if (!buf) return 0;
            m.w32(inst, buf);
        }
        U s1 = buf;
        if (m.u32(model + 8)) {
            U tbl = U(m.u32(model + 8));
            m.w16(inst + 0x70, m.u16(tbl));
            m.w32(inst + 0x78, tbl + 4);
            m.w16(inst + 0x72, m.u8(m.u32(tbl + 4) + 1));
            I nb = m.u16(inst + 0x72);
            m.w32(inst + 0x74, tbl);
            m.w32(inst + 0x7C, m.u32(model + 0x38));
            m.w32(inst + 0x80, m.u32(model + 0x84));
            m.w32(inst + 0x90, s1);
            s1 += U(nb << 3);
            m.w32(inst + 0x8C, s1);
            s1 += U(nb << 3);
            m.w32(inst + 0x94, s1);
            m.w32(inst + 0x84, m.u32(model + 0x9C));
            U first = U(m.u32(m.u32(inst + 0x78)));
            U qsrc = U(m.u32(m.u32(first + 4) + 0x10));
            (void)qsrc;
            U fr = U(m.u32(first + 4));
            m.copy(m.u32(inst + 0x8C), m.u32(fr + 0x10), nb << 3);
            s1 += U(nb << 3);
            m.copy(m.u32(inst + 0x90), m.u32(fr + 0x10), nb << 3);
            m.w32(inst + 0x88, 0);
            I w = m.u32(fr + 4);
            m.w16(inst + 0x98, S32(w << 22) >> 22);
            m.w16(inst + 0x9A, S32(w << 11) >> 21);
            m.w16(inst + 0x9C, S32(w << 1) >> 22);
            m.copy(inst + 0xA0, inst + 0x98, 8);
        }
        if ((flags & 1) || !m.u32(model + 0x1C)) {
            m.w32(inst + 0x14, m.u32(model + 0x20));
            m.w32(inst + 0x1C, m.u32(model + 0x28));
            m.w32(inst + 0x18, m.u32(model + 0x24));
            m.w32(inst + 0x20, m.u32(model + 0x2C));
            m.w32(inst + 0x24, m.u32(model + 0x30));
            m.w32(inst + 0x28, m.u32(model + 0x34));
            if (!m.u32(model + 0x1C)) {
                m.w32(model + 0x1C, 1);
                m.w32(inst + 4, m.u32(inst + 4) | 8);
            }
        } else {
            for (U o : {0x14u, 0x18u, 0x1Cu, 0x20u, 0x24u, 0x28u}) m.w32(inst + o, 0);
            if (m.u32(model + 0x2C)) m.w32(inst + 0x20, m.u32(model + 0x2C));
            if (m.u32(model + 0x30)) {
                m.w32(inst + 0x24, s1);
                U src = U(m.u32(model + 0x30));
                I n = (m.u32(src) << 2) + 8;
                m.copy(s1, src, n);
                s1 += U(n);
            }
            if (m.u32(model + 0x34)) m.w32(inst + 0x28, m.u32(model + 0x34));
        }
        m.w16(inst + 0x3C, m.u16(model + 0x8C));
        if (flags & 2)
            m.w32(inst + 0x2C, m.u32(model + 0x88));
        else {
            m.w32(inst + 0x2C, s1);
            I n = m.u16(model + 0x8C) << 3;
            m.copy(s1, m.u32(model + 0x88), n);
            s1 += U(n);
        }
        if (!m.u32(model + 0x14)) {
            if (!(flags & 0x40)) {
                m.w32(inst + 0x30, s1);
                s1 += U(m.u16(model + 0x8C) << 3);
            }
            m.w32(inst + 0x34, -1);
            m.w32(inst + 0x38, -1);
            if (flags & 0x100)
                m.w32(inst + 0x48, m.u32(model + 0xC4));
            else {
                m.w32(inst + 0x48, s1);
                m.copy(s1, m.u32(model + 0xC4), m.u16(model + 0x8C) << 2);
                s1 += U(m.u16(model + 0x8C) << 2);
            }
        }
        m.w16(inst + 0x42, m.u16(model + 0x8E));
        if (flags & 4)
            m.w32(inst + 0x44, m.u32(model + 0x90));
        else if (m.u32(inst + 0x44)) {
            m.w32(inst + 0x44, s1);
            I n = m.u16(model + 0x8E) << 3;
            m.copy(s1, m.u32(model + 0x90), n);
            s1 += U(n);
        } else
            m.w32(inst + 0x44, 0);
        if (!(flags & 0x10)) {
            m.w32(inst + 0x4C, s1);
            s1 += 0x800;
            m.w32(inst + 0x50, s1);
        } else {
            m.w32(inst + 0x4C, 0);
            m.w32(inst + 0x50, 0);
        }
        m.w16(inst + 0x5C, -1);
        m.w16(inst + 0x5E, 0);
        m.w32(inst + 0x54, 0);
        m.w32(inst + 0x58, 0);
        if (e) {
            m.w32(e + 0x2D4, m.u32(model + 0xA0));
            for (int k = 0; k < 3; k++) m.w32(e + 0x270 + 4 * k, m.u32(model + 0xA4 + 4 * k));
            I n = m.u16(model + 0xBE);
            m.w16(e + 0x36, n);
            for (I i = 0; i < n; i++) {
                U q = U(m.u32(e + 0x304) + 0x48 * i), t = U(m.u32(model + 0xC0) + 0x48 * i);
                m.w32(q + 4, m.u32(t + 4));
                m.w32(q + 8, m.u32(t + 0xC));
                m.w32(q + 0xC, m.u32(t + 0xC));
                m.w16(q + 0x1C, m.u16(t + 0x1C));
                m.w16(q + 0x1E, m.u16(t + 0x1E));
                m.w32(q + 0x34, m.u32(t + 0x34));
                m.w32(q + 0x30, m.u32(t + 0x30));
                m.w32(q + 0x38, m.u32(t + 0x38));
                m.w32(q + 0x10, m.u32(t + 0x10));
                m.w32(q + 0x14, m.u32(t + 0x14));
                m.w16(q + 0x40, m.u16(t + 0x40));
                m.w16(q + 0x42, m.u16(t + 0x42));
                m.w16(q + 0x44, m.u16(t + 0x44));
                m.w32(q + 0x18, inst);
                if (flags & 0x200) {
                    m.w32(q + 0x34, S32(m.u32(q + 0x34) << 1));
                    m.w32(q + 0x30, S32(m.u32(q + 0x30) << 1));
                    m.w32(q + 0x38, S32(m.u32(q + 0x38) << 1));
                }
                m.w32(q + 0x3C, 0);
            }
        }
        return ok;
    }

    // 0x8006B234: the instance's pose buffer released (characters' to their pool, else freed now
    // or after `delay` frames), its parts' instances first.
    void instance_release(U inst, U e, I delay) {
        if (!inst) return;
        if (e)
            for (I i = 0; i < m.u16(e + 0x36); i++) instance_release(U(m.u32(m.u32(e + 0x304) + 0x48 * i + 0x3C)), 0, 2);
        I f = m.u32(inst + 4);
        if (f & 8) {
            m.w32(inst + 4, f & ~I(8));
            m.w32(m.u32(inst + 8) + 0x1C, 0);
        }
        I t = e ? m.s32(e + 8) : 0;
        if (t == 0x1402 || t == 0x1405) {
            note("a character's pose buffer released");
            return;
        }
        U buf = U(m.u32(inst));
        if (buf) {
            if (delay == 0)
                heap.free(buf);
            else {
                I n = m.s32(0x8009F888);
                m.w32(0x8009F88C + 8 * n, buf);
                m.w32(0x8009F890 + 8 * n, delay);
                m.w32(0x8009F888, n + 1);
            }
        }
        m.w32(inst, 0);
    }

    // entity_create (0x8006B5B8).
    U entity_create(I flags, I obj, U hash, I iflags, I parts, I channels, I list, U model) {
        if (hash != 0xFFFFFFFF && !model) {
            I idx = model_find(hash);
            if (idx <= 0) return 0;
            model = model_at(idx);
        }
        I size = ((flags & 5) ? 0x47C : 0x3CC) + obj;
        if ((flags & 2) && hash != 0xFFFFFFFF) parts = m.u16(model + 0xBE);
        size += parts * 0x48 + list * 0x1C + channels * 0x78;
        I t = m.s32(model + 0xFC);
        U e = (t == 0x1402 || t == 0x1405) ? char_alloc() : heap.calloc(size, 1);
        if (!e) return 0;
        handle_alloc(e);
        U p = e + 0x3CC;
        m.w32(e + 0x390, 0xFFFF);
        m.w32(e + 0x394, 0);
        m.w32(e + 0x398, 0xFFFF);
        m.w32(e + 0x39C, 0);
        if (obj) {
            m.w32(e + 0x388, p);
            p += U(obj);
        } else
            m.w32(e + 0x388, 0);
        if (flags & 5) {
            m.w32(e + 0x100, p);
            p += 0xB0;
        } else
            m.w32(e + 0x100, 0);
        if (parts) {
            m.w32(e + 0x304, p);
            p += U(parts * 0x48);
        } else
            m.w32(e + 0x304, 0);
        if (list) {
            m.w32(e + 0x300, p);
            p += U(list * 0x1C);
            m.w16(e + 0x2EE, list);
        } else {
            m.w32(e + 0x300, 0);
            m.w16(e + 0x2EE, 0);
        }
        for (int ch = 0; ch < 2; ch++) {
            if (channels) {
                m.w32(e + 0x124 + 0x74 * ch, p);
                p += U(channels * 0x3C);
                m.w32(e + 0x1EC, channels);
            } else {
                m.w32(e + 0x124 + 0x74 * ch, 0);
                m.w32(e + 0x1EC, 0);
            }
        }
        defaults(e);
        if (hash == 0xFFFFFFFF || ((flags & 5) && instance_init(U(m.u32(e + 0x100)), model, iflags, e))) {
            m.w32(e + 0x38C, 0);
            return e;
        }
        heap.free(e);
        return 0;
    }

    // 0x8006A76C: RotMatrixYXZ of the angles, TransMatrix of the integer position, ScaleMatrix
    // by the scale >> 4, into +0x340.
    void matrix(U e) {
        int16_t r[3][3];
        rot_yxz(int32_t(m.s16(e + 0x22A)), int32_t(m.s16(e + 0x22E)), int32_t(m.s16(e + 0x232)), r);
        I sc[3] = {m.s32(e + 0x270) >> 4, m.s32(e + 0x274) >> 4, m.s32(e + 0x278) >> 4};
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) m.w16(e + 0x340 + 6 * i + 2 * j, S16((r[i][j] * sc[j]) >> 12));
        m.w32(e + 0x354, m.s16(e + 0x20E));
        m.w32(e + 0x358, m.s16(e + 0x212));
        m.w32(e + 0x35C, m.s16(e + 0x216));
    }

    void link_front(U e) {
        U head = U(m.u32(G_PLAYER));
        m.w32(e + 4, m.u32(head + 4));
        m.w32(head + 4, e);
    }

    // ---------------------------------------------------------------- level_load (0x80087308)

    void vab_upload(U vab, I slot) {
        constexpr U G_BANK_SPU = 0x800910E8, G_BANK_SIZES = 0x8009263C, G_BANK_PITCH = 0x8009163C, G_SPU_ALLOC = 0x800A0130;
        I hdr = (m.u16(vab + 0x12) << 9) + 0xA20;
        U s3 = U(G_BANK_SPU + slot * 4);
        I bytes = m.s32(vab + 0xC) - hdr;
        if (m.s32(s3 + 0x40)) return;
        if ((slot & 0xFFFF) >= 8 || m.u32(vab) != 0x56414270) return;
        if (m.u32(G_SPU_ALLOC) + bytes > 0x7FFFF) {
            note("vab_upload: SPU memory full");
            return;
        }
        U samples = U(vab + hdr);
        U sizes = U(G_BANK_SIZES + (slot << 9));
        m.w32(s3 + 0x20, sizes);
        m.copy(sizes, samples - 0x200, 0x200);
        m.w32(s3, m.u32(G_SPU_ALLOC));
        I tones = m.u16(vab + 0x12);
        for (I s = 0; s < 0x100; s++) {
            I pitch = 0x800;
            U t = U(vab + 0x824);
            for (I k = 0; k < tones * 0x10; k++, t += 0x20) {
                if (s == ((m.u8(t + 0x13) << 8) | m.u8(t + 0x12)) - 1) {
                    I p = m.u16(0x80090FD0 + (m.u8(t) + 4) * 2);
                    pitch = p < 0x1000 ? p : 0x1000;
                    break;
                }
            }
            m.w16(G_BANK_PITCH + slot * 2 + s * 0x10, pitch);
        }
        m.w32(G_BANK_SPU + (S32(slot << 16) >> 14) + 0x40, 1);
        m.w32(G_SPU_ALLOC, m.u32(G_SPU_ALLOC) + bytes);
    }

    void taf(U buf) {
        U p = buf;
        while (m.u32(p) == 0x10) {
            I flags = m.u32(p + 4);
            U q = p + 8;
            if ((flags >> 3) & 1) q += U(m.u32(q));
            p = U(q + m.u32(q));
        }
        I n = m.u32(p);
        for (I i = 0; i < n; i++) vab_upload(U(p + m.u32(p + 4 + 4 * i)), i);
    }

    bool level_load(std::string* err);
    void reloc_tables(U Lh);
    void model_relocate(U md, U Lh, U base);
    void scripts_setup(U md);
    void inherit(U md, I nmodels);
    U world_create();
    void zones_parse(U z);
    void paths_parse(U p);
    void records_register(U w);
    void scripts_init();
    void player_spawn();
    U camera_create();
    void camera_entity_init(U e);
    U soldier_create(U pair);
    void player_soldier_init(U e);
    void soldier_flags_reset(U e, I keep_port);
    void level_start();
    void player_reset(U e);
    void level_entry();
    void pools_reset();
    void start_position(U e);
    void tsp_load(I chunk_file, I slot);
    void stars_init(U out, I n);
    void sky();
    void music_start();
    void fade_start();
    void voice_stop(I v);
    I rand();
    I rand_calls = 0;
};

void Loader::defaults_tail(U e) {
    m.w16(e + 0x3C8, 0x64);
    m.w16(e + 0x3CA, -1);
}

bool Loader::level_load(std::string* err) {
    // the TAF (textures, then sound banks): read, uploaded, freed
    I variant = (m.u32(G_GAME + 0xD8) & 2) ? 1 : 0;
    std::string tag = std::to_string(mission) + "_" + std::to_string(level);
    U taf_buf = file_read(dir() + "/" + tag + std::to_string(variant) + ".TAF", nullptr);
    if (!taf_buf) {
        *err = "no TAF";
        return false;
    }
    m.w32(0x8009A618, variant);
    taf(taf_buf);
    heap.free(taf_buf);
    // bsd_read (0x8004A374): sector 0 a size table (the body with more and more models); the
    // largest that the heap takes (bsd_alloc 0x8004A300), its sectors of the body read
    std::vector<uint8_t> bsd;
    if (!read(dir() + "/" + tag + ".BSD", bsd) || bsd.size() < 0x800) {
        *err = "no BSD";
        return false;
    }
    auto rd32 = [&](size_t o) { return I(bsd[o] | bsd[o + 1] << 8 | bsd[o + 2] << 16 | uint32_t(bsd[o + 3]) << 24); };
    I count = rd32(0), idx = 0;
    U s4 = 0;
    for (I k = count; k > 0; k--) {
        s4 = heap.malloc(rd32(size_t(4 * k)));
        if (s4) {
            idx = k;
            break;
        }
    }
    if (!s4) {
        *err = "the BSD doesn't fit";
        return false;
    }
    size_t body = size_t((rd32(size_t(4 * idx)) >> 11) << 11);
    size_t have = std::min(body, bsd.size() - 0x800);
    std::copy(bsd.begin() + 0x800, bsd.begin() + 0x800 + have, m.d.begin() + (s4 & Mem::kMask));
    m.zero(s4 + have, I(body - have));  // the file's last sector, padded with zeros on the disc
    m.w32(G_LEVEL_PTR, s4);
    U Lh = s4;
    if (m.s32(Lh + 0xA0))
        for (U o : {0xB0u, 0xB2u, 0xB4u, 0xB6u}) m.w16(Lh + o, 0);
    m.w32(G_MODELS_PTR, s4 + 0x5A8);
    m.w32(G_MODEL_COUNT, idx + 1);
    for (U a = Lh; a < Lh + 0x320; a += 0x14) {
        m.w32(a + 0xE0, m.s32(a + 0xDC) ? s4 + m.u32(a + 0xE0) : 0);
        m.w32(a + 0xEC, 0);
    }
    for (int i = 0; i < 0x14; i++) {
        m.w16(Lh + 0x40A + 0x10 * i, 0);
        m.w16(Lh + 0x408 + 0x10 * i, 0);
    }
    for (U o : {0x544u, 0x54Cu, 0x554u, 0x55Cu, 0x564u, 0x56Cu, 0x574u, 0x57Cu, 0x584u})
        if (m.u32(Lh + o)) m.w32(Lh + o, s4 + m.u32(Lh + o));
    reloc_tables(Lh);
    I nmodels = m.u32(G_MODEL_COUNT);
    for (I i = 0; i < nmodels; i++) model_relocate(model_at(i), Lh, s4);
    I slots = 0;
    for (I i = 0; i < nmodels; i++) {
        U md = model_at(i);
        if (m.s32(md + 0xE8) != -1) inherit(md, nmodels);
        if (m.s32(md + 0xFC) == 1) {
            for (I w = 0; w < 0xD; w++) {
                U wa = U(G_WEAPONS + 0x44 * w);
                if (m.u32(wa) == m.u32(md)) {
                    m.w8(G_SLOT_WEAPON + slots, w);
                    slots++;
                    m.w32(wa + 0x40, md);
                    break;
                }
            }
        }
    }
    m.w32(G_SLOT_COUNT, slots);
    U w = world_create();
    U wobj = U(m.u32(w + 0x388));
    m.w32(G_WORLD, w);
    if (m.u32(Lh + 0x59C)) m.w32(Lh + 0x59C, s4 + m.u32(Lh + 0x59C));
    if (m.u32(Lh + 0x53C)) {
        m.w32(Lh + 0x53C, s4 + m.u32(Lh + 0x53C));
        m.w32(wobj, m.u32(Lh + 0x53C));
        records_register(w);
    }
    if (m.u32(Lh + 0x598)) {
        m.w32(Lh + 0x598, s4 + m.u32(Lh + 0x598));
        zones_parse(U(m.u32(Lh + 0x598)));
    }
    if (m.u32(Lh + 0x540)) {
        U t = U(s4 + m.u32(Lh + 0x540));
        m.w32(Lh + 0x540, t);
        m.w32(wobj + 4, t);
        m.w32(wobj + 8, t + 8);
        m.w32(wobj + 0xC, m.u32(t));
    }
    if (m.u32(Lh + 0x5A0)) {
        m.w32(Lh + 0x5A0, s4 + m.u32(Lh + 0x5A0));
        paths_parse(U(m.u32(Lh + 0x5A0)));
    } else {
        m.w32(G_PATHS_PTR, 0);
        m.w32(G_PATH_COUNT, 0);
    }
    // the fog (SetFogNear and the GTE's colours): drawing only
    scripts_init();
    // the level logic over the records, scripts and nodes now in place
    eng.L.load(m, eng.level);
    eng.logic = std::make_unique<LevelLogic>(eng);
    eng.logic->bind(w);
    player_spawn();
    link_front(w);
    note("ammo_hud_init (the HUD's ammunition panel): drawing only, not built");
    for (int v = 1; v < 0x18; v++) voice_stop(v);  // sound_stop_all
    m.w32(0x8009EC50, 1);
    return true;
}

void Loader::reloc_tables(U Lh) {
    auto table = [&](U list_o, U count_o, I stride, U field, U base_o) {
        U p = U(m.u32(Lh + list_o));
        I n = m.u32(Lh + count_o);
        for (I i = 0; i < n; i++, p += U(stride)) {
            I v = m.u32(p + field);
            m.w32(p + field, v != 0xFFFFFFFF ? m.u32(Lh + base_o) + v : 0);
        }
    };
    table(0x544, 0x548, 8, 4, 0x54C);
    table(0x54C, 0x550, 0x14, 0x10, 0x554);
    {
        U base = U(m.u32(Lh + 0x55C));
        I n = m.u32(Lh + 0x560);
        U p = base;
        for (I i = 0; i < n; i++, p += 0x14) {
            for (U f : {0xCu, 0x10u}) {
                I v = m.u32(p + f);
                m.w32(p + f, v != 0xFFFFFFFF ? base + v : 0);
            }
        }
    }
    table(0x564, 0x568, 8, 0, 0x56C);
    table(0x574, 0x578, 8, 0, 0x57C);
    table(0x57C, 0x580, 8, 0, 0x584);
}

void Loader::model_relocate(U md, U Lh, U base) {
    I v = m.u32(md + 8);
    if (v == 0xFFFFFFFF)
        m.w32(md + 8, 0);
    else {
        U a = U(base + v);
        m.w32(md + 8, a);
        for (I k = 0; k < m.u16(a); k++) {
            U q = U(a + 4 + 4 * k);
            I x = m.u32(q);
            m.w32(q, x != 0xFFFFFFFF ? x + m.u32(Lh + 0x544) : 0);
        }
    }
    auto rel = [&](U o, U base_o) {
        I x = m.u32(md + o);
        m.w32(md + o, x == 0xFFFFFFFF ? 0 : x + m.u32(Lh + base_o));
    };
    rel(0x38, 0x564);
    rel(0x84, 0x574);
    rel(0x9C, 0x55C);
    for (U o : {0xB4u, 0x88u, 0x90u, 0xC4u})
        if (m.u32(md + o)) m.w32(md + o, base + m.u32(md + o));
    if (m.u32(md + 4)) {
        m.w32(md + 4, base + m.u32(md + 4));
        note("a model with a bounds block (model_bounds_setup) not built");
    }
    if (m.u32(md + 0xC)) {
        m.w32(md + 0xC, base + m.u32(md + 0xC));
        scripts_setup(md);
    }
    for (U o : {0x24u, 0x2Cu, 0x30u, 0x34u, 0xC0u})
        if (m.u32(md + o)) m.w32(md + o, base + m.u32(md + o));
}

void Loader::scripts_setup(U md) {
    U t = U(m.u32(md + 0xC));
    I n = m.s32(md + 0xB0);
    for (I k = 0; k < n; k++) m.w32(m.u32(md + 0xB4) + 0x3C * k, 0);
    I cnt = m.u16(t);
    m.w16(md + 0xBC, cnt);
    m.w32(md + 0xB8, t + 8);
    m.w16(md + 0x94, m.u16(t + 4));
    for (I k = 0; k < cnt; k++) {
        U e = U(t + 8 + 16 * k);
        m.w32(e + 0xC, t + m.u32(e + 0xC));
    }
}

void Loader::inherit(U md, I nmodels) {
    U b = 0;
    for (I k = 0; k < nmodels; k++) {
        b = model_at(k);
        if (m.u32(b) == m.u32(md + 0xE8)) break;
    }
    auto cp32 = [&](U o) { m.w32(md + o, m.u32(b + o)); };
    auto cp16 = [&](U o) { m.w16(md + o, m.u16(b + o)); };
    if (m.s32(md + 0xB0) == 0) cp32(0xB0), cp32(0xB4);
    if (m.s32(md + 0xA0) == 0) cp32(0xA0);
    if (!m.s32(md + 0xA4) && !m.s32(md + 0xA8) && !m.s32(md + 0xAC)) cp32(0xA4), cp32(0xA8), cp32(0xAC);
    if (m.u16(md + 0xC8) == 0) cp16(0xC8);
    if (m.s32(md + 4) == 0) {
        cp32(4);
        m.copy(md + 0x3C, b + 0x3C, 8);
        for (int k = 0; k < 8; k++) cp16(0x44 + 8 * k), cp16(0x46 + 8 * k), cp16(0x48 + 8 * k);
        cp32(0x88), cp16(0x8C), cp16(0x8E), cp32(0x90), cp16(0x96), cp32(0x98), cp16(0xCC), cp16(0xCE), cp16(0xD0);
    }
    if (m.s32(md + 0xC) == 0) cp32(0xC), cp16(0x94);
    if (m.s16(md + 0xD4) < 0) {
        cp16(0xD4);
        m.copy(md + 0xD8, b + 0xD8, 8);
        if (m.s16(md + 0xD4) < 0) {
            cp16(0xD6);
            m.copy(md + 0xE0, b + 0xE0, 8);
        }
    }
    if (m.s32(md + 0xC0) == 0) cp32(0xC0), cp16(0xBE);
    if (m.s32(md + 0xB8) == 0) cp32(0xB8), cp16(0xBC);
    if (m.s32(md + 0x10) == 0) cp32(0x10), cp32(0x14);
    for (U o : {0x8u, 0x18u, 0x20u, 0x24u, 0x28u, 0x2Cu, 0x30u, 0x34u, 0x38u, 0x84u, 0x9Cu, 0xC4u})
        if (m.s32(md + o) == 0) cp32(o);
    if (m.s32(md + 0xFC) == -1) cp32(0xFC);
    for (int k = 0; k < 4; k++)
        if (m.s32(md + 0xEC + 4 * k) == -1) {
            for (int j = 0; j < 4; j++) cp32(0xEC + 4 * j);
            break;
        }
}

// world_create (0x80053E68): the world entity (type 3), the primitive pool, the TSP files' CD
// entries (the chunk numbers past +0x88 the first +0x84 slots load), and the chunk slots'
// buffers (the largest file + 0x7FF each).
U Loader::world_create() {
    m.w32(0x80098960, 0);
    U e = entity_create(0, 0x1C, 0xFFFFFFFF, 0, 0, 0, 0, 0);
    if (!e) return 0;
    m.w32(e + 8, 3);
    m.w32(e + 0x10, 0x203);
    m.w32(e + 0x36C, 0x80053C00);
    m.w32(e + 0x370, 0x80053C20);
    m.w32(e + 0x384, 0x80053D40);
    for (int k = 0; k < 3; k++) m.w32(e + 0x270 + 4 * k, 0x10000);
    for (U o : {0x14u, 0x374u, 0x378u, 0x20Cu, 0x210u, 0x214u, 0x228u, 0x22Cu, 0x230u}) m.w32(e + o, 0);
    m.w32(e + 0x284, 1);
    m.w32(e + 0xC, m.u32(e + 0xC) | 0x08020000);
    U Lh = L();
    m.w32(0x80098980, heap.malloc(m.s32(Lh + 0x98) * 2));
    m.w32(0x8009905C, m.u32(Lh + 0x80));
    m.w32(0x80098984, m.u32(Lh + 0x84));  // g_chunk_count
    I n_files = m.s32(Lh + 0x80);
    I s0 = 1, s3 = 0;
    std::string prefix;
    for (U a = Lh; m.u8(a); a++) prefix += char(m.u8(a));
    for (I s2 = 0; s2 < n_files; s2++) {
        U ent;
        do {
            I num = s0;
            s0++;
            ent = cd_entry(prefix + std::to_string(num) + ".tsp");
        } while (!ent);
        if (m.s32(Lh + 0x88) < s0 && s3 < m.s32(Lh + 0x84)) {
            m.w32(0x80099060 + 4 * s3, s0 - 1);
            s3++;
        }
        if (U32(m.u32(0x80098960)) < U32(m.u32(ent + 8))) m.w32(0x80098960, m.u32(ent + 8));
    }
    for (I k = 0; k < m.s32(0x80098984); k++) m.w32(0x8009904C + 4 * k, heap.malloc(m.u32(0x80098960) + 0x7FF));
    return e;
}

void Loader::zones_parse(U z) {
    I n = m.s32(z);
    if (n == -1) {
        m.w32(G_ZONE_RECORDS, 0);
        return;
    }
    U t = heap.malloc((n + 1) * 4);
    m.w32(G_ZONE_RECORDS, t);
    U s0 = z + 4, s2 = z + 6;
    for (I k = 0; k < n + 1; k++) {
        m.w32(t + 4 * k, s0);
        s0 = U(s2 + ((0xFF - m.u8(s0)) * 2));
        s2 = s0 + 2;
    }
}

void Loader::paths_parse(U p) {
    I n = m.s8(p);
    m.w32(G_PATH_COUNT, n);
    U t = heap.malloc(n * 8);
    m.w32(G_PATHS_PTR, t);
    if (!t) {
        m.w32(G_PATH_COUNT, 0);
        return;
    }
    U s = p + 2;
    for (I k = 0; k < n; k++, t += 8) {
        I c = m.s8(s);
        U pts = s + 2;
        m.w32(t + 4, pts);
        m.w16(t + 2, c);
        U after = U(pts + c * 0x14);
        m.w16(t, m.u16(after));
        s = after + 4;
    }
}

// entity_records_register (0x8007529C): the table's pairs, each record's offset made a pointer,
// then by kind its lists (0x80075120) and template (+0x60 for kinds 2, 6; +0x74 for kind 3).
void Loader::records_register(U w) {
    U obj = U(m.u32(w + 0x388));
    U t = U(m.u32(obj));
    if (!t) return;
    I n = m.u32(t);
    m.w32(obj + 0x14, t + 0x10);
    m.w32(obj + 0x10, n);
    U base = U(t + 0x10 + (n << 3));
    m.w32(0x80098988, base);
    m.w32(obj + 0x18, base);
    for (I i = 0; i < n; i++) {
        U pair = U(t + 0x10 + 8 * i + 4);
        U r = U(base + m.u32(pair));
        m.w32(pair, r);
        I kind = m.u32(r + 0xC);
        auto lists = [&]() {
            for (U o = 0x38; o <= 0x50; o += 4)
                if (m.u32(r + o)) m.w32(r + o, r + m.u32(r + o));
        };
        if (kind >= 7 || kind == 0) continue;
        lists();
        if (kind == 2 || kind == 6) {
            if (m.u32(r + 0x60)) m.w32(r + 0x60, r + m.u32(r + 0x60));
        } else if (kind == 3) {
            if (m.u32(r + 0x74)) m.w32(r + 0x74, r + m.u32(r + 0x74));
        }
    }
}

// func_80066108 / msg_lists_init (0x80066790): the level scripts from the header, one
// subscription list per message, the node pool's free list, the queue emptied, the built-ins.
void Loader::scripts_init() {
    m.w32(G_LEVEL_SCRIPTS, m.u32(L() + 0x59C));
    I n = m.u32(G_LEVEL_SCRIPTS) ? m.u32(m.u32(G_LEVEL_SCRIPTS)) : 0;
    if (m.u32(G_MSG_SUBS)) {
        heap.free(U(m.u32(G_MSG_SUBS)));
        m.w32(G_MSG_SUBS, 0);
    }
    U t = heap.malloc(n * 4);
    m.w32(G_MSG_SUBS, t);
    if (!t) return;
    for (I k = 0; k < n; k++) m.w32(t + 4 * k, 0);
    m.w32(G_MSG_FREE, POOL_NODES);
    for (U k = 0; k < 0x3FF; k++) m.w32(POOL_NODES + 0x24 * k + 0x20, POOL_NODES + 0x24 * (k + 1));
    m.w32(POOL_NODES + 0x8FFC, 0);
    m.w32(G_MSG_TAIL, 0);
    m.w32(G_MSG_HEAD, 0);
    // g_level_builtins: the 22 built-ins' functions, by number (levels.md)
    static const U builtins[22] = {0x80069514, 0x80069714, 0x800698F0, 0x80069A08, 0x80069A74, 0x80069ACC,
                                   0x80069B14, 0x80069D5C, 0x80069FC4, 0x8006A038, 0x8006A0C8, 0x8006A184,
                                   0x80069968, 0x80069D04, 0x80069DEC, 0x80069E44, 0x80069E84, 0x80069EC4,
                                   0x80069F2C, 0x80069B88, 0x80069CC4, 0x80069F6C};
    for (U k = 0; k < 22; k++) m.w32(0x8009D5C0 + 4 * k, builtins[k]);
    m.w32(0x80098628, 0x78);  // g_msg_line_y
}

// player_spawn (0x80087294): the camera entity at the list's head, the player's soldier from the
// player start record, the camera following it.
void Loader::player_spawn() {
    U cam = camera_create();
    U head = U(m.u32(G_ENTITIES));
    m.w32(G_PLAYER, cam);
    m.w32(G_ENTITIES, cam);
    m.w32(cam + 4, head);
    eng.logic->bind(cam);
    // player_start_record (0x800754CC, 0): the record of class 0x8877B646 whose +0x34 is 0
    U wobj = U(m.u32(m.u32(G_WORLD) + 0x388));
    U pair = 0;
    for (I i = 0; i < m.s32(wobj + 0x10); i++) {
        U pr = U(m.u32(wobj + 0x14) + 8 * i);
        U r = U(m.u32(pr + 4));
        if (m.u32(r) == 0x8877B646 && m.s16(r + 0x34) == 0) {
            pair = pr;
            break;
        }
    }
    U sol = 0;
    if (pair && !(m.u32(pair) & 1)) {
        // record_spawn_type(1, pair) (0x80074BB8) -> 0x8006D574
        sol = soldier_create(pair);
        if (sol) {
            if (U(m.u32(U(m.u32(sol + 0x100)) + 8))) {
                matrix(sol);
                I s = m.s16(m.u32(m.u32(sol + 0x100) + 8) + 0x94);
                if (s >= 0) {
                    eng.logic->bind(sol);
                    VM vm(eng, sol);
                    vm.script_start(s, 1, 0);
                    vm.run_a();
                }
            }
            link_front(sol);
            m.w32(pair, m.u32(pair) | 1);
            m.w32(sol + 0x1C, pair);
            Ent* ent = eng.logic->bind(sol);
            ent->rec = eng.L.by_pair.count(pair) ? eng.L.by_pair[pair] : nullptr;
            if (m.u32(m.u32(pair + 4) + 0xC)) eng.logic->msg_start(ent, 0);
            U r = U(m.u32(pair + 4));
            for (int k = 0; k < 3; k++) {
                m.w32(sol + 0x228 + 4 * k, S32(m.s16(r + 0x18 + 2 * k) << 16));
                m.w32(sol + 0x20C + 4 * k, S32(m.s16(r + 0x10 + 2 * k) << 16));
            }
            for (int k = 0; k < 3; k++) {
                m.w32(sol + 0x258 + 4 * k, m.u32(sol + 0x20C + 4 * k));
                m.w32(sol + 0x3BC + 4 * k, m.u32(sol + 0x20C + 4 * k));
            }
        }
    }
    m.w32(G_SOLDIER, sol);
    if (sol) player_soldier_init(sol);
    // func_80048DC8 (camera, soldier, 0): the camera's target handle
    U co = U(m.u32(cam + 0x388));
    m.w32(co + 0xC, sol ? m.u32(sol) : 0);
    if (sol && (m.u32(sol + 0xC) & 1)) m.w32(m.u32(sol + 0x388) + 0xA0, m.u32(cam));
}

// 0x8004787C: the camera entity, with the model 0xCBE42983 (its behaviour script started).
U Loader::camera_create() {
    I idx = model_find(0xCBE42983);
    U e;
    if (idx > 0) {
        e = entity_create(1, 0x120, 0xCBE42983, 0x157, 0, 5, 0, model_at(idx));
        if (!e) return 0;
        U inst = U(m.u32(e + 0x100));
        m.w32(e + 0x10, m.u32(e + 0x10) | 0x200);
        if (inst && m.u32(inst + 8)) {
            matrix(e);
            I s = m.s16(m.u32(inst + 8) + 0x94);
            if (s >= 0) {
                VM vm(eng, e);
                vm.script_start(s, 1, 0);
                vm.run_a();
            }
            m.w32(e + 0x370, 0x80046DD4);
        } else
            m.w32(e + 0x370, 0);
    } else {
        e = entity_create(0, 0x120, 0xFFFFFFFF, 0, 0, 0, 0, 0);
        if (!e) return 0;
        m.w32(e + 0x370, 0);
        m.w32(e + 0x10, m.u32(e + 0x10) | 0x200);
    }
    U co = U(m.u32(e + 0x388));
    // __builtin_new(0xF0) and 0x8008CFE8: the camera's animation state
    U a = heap.malloc(0xF0);
    m.w32(a + 0x3C, 0);
    m.w32(a + 0x40, 0);
    for (int k = 0; k < 6; k++) m.w32(a + 0x44 + 0x1C * k + 0x18, 0);
    m.w32(a + 0xEC, 0);
    m.w32(co + 0xC8, a);
    camera_entity_init(e);
    return e;
}

// camera_entity_init (0x8004770C).
void Loader::camera_entity_init(U e) {
    m.w32(e + 0xC, 0x20002);
    m.w32(e + 0x36C, 0x800473DC);
    m.w32(e + 0x384, 0x800475C4);
    U o = U(m.u32(e + 0x388));
    m.w32(e + 8, 2);
    for (U k : {0x14u, 0x374u, 0x378u, 0x37Cu, 0x284u}) m.w32(e + k, 0);
    m.w32(e + 0x10, m.u32(e + 0x10) | 0x213);
    for (int k = 0; k < 3; k++) m.w32(e + 0x3BC + 4 * k, m.u32(e + 0x20C + 4 * k));
    m.w32(o + 0x9C, 0x80);
    m.w32(o + 0x14, 0);
    m.w16(o + 0xD8, 3);
    m.w16(o + 0xDA, 3);
    m.w16(o + 0xE0, 2);
    m.w16(o + 0xD0, 0);
    m.w16(o + 0xD2, 0);
    m.w16(o + 0xD4, 0);
    // 0x8004760C: the eye (-240) and the look points
    m.w32(o + 0x94, -0xF0);
    m.w32(o + 0x98, -0xEE);
    for (U k : {0x20u, 0x2Cu, 0x38u, 0x78u, 0x84u}) m.w32(o + k, 0xC80000);
    for (U k : {0x18u, 0x24u, 0x30u, 0x48u, 0x50u, 0x54u, 0x5Cu, 0x60u, 0x68u, 0x80u}) m.w32(o + k, 0);
    m.w32(o + 0x1C, S32(m.s32(o + 0x98) << 16));
    m.w32(o + 0x28, S32(m.s32(o + 0x98) << 16));
    m.w32(o + 0x34, S32(m.s32(o + 0x98) << 16));
    m.w32(o + 0x4C, S32(m.s32(o + 0x94) << 16));
    m.w32(o + 0x58, S32(m.s32(o + 0x94) << 16));
    m.w32(o + 0x64, S32(m.s32(o + 0x94) << 16));
    m.w32(o + 0x7C, S32(m.s32(o + 0x94) << 16));
    m.w32(o + 0x88, S32(m.s32(o + 0x94) << 16));
    m.w32(o + 0x8C, 0);
    m.w32(o + 0x10, 0);
    m.w32(o + 0xA0, 0);
    m.w16(o + 0xCE, 0);
    I far = m.s32(L() + 0x590);
    m.w32(o + 0xC4, far > 0 ? far : 0x1194);
    m.w32(o + 0x90, 0);
    U a = U(m.u32(o + 0xC8));
    m.w32(a + 0x18, 0);
    m.w32(a + 0x1C, 0xFFFF0000);
    m.w32(a + 0x20, 0);
    m.w32(a + 0x30, 0x02000000);
    m.w32(a + 0x34, 0x01800000);
    m.w32(a + 0x38, 0);
    m.w32(a + 0x3C, S32(m.s32(0x8009A628) << 16));
    m.w32(a + 0x40, S32(m.s32(o + 0xC4) << 16));
}

// 0x8007BD68: the player's soldier: the first slot's weapon model, 0x14 channels.
U Loader::soldier_create(U) {
    U hash = U(m.u32(G_WEAPONS + 0x44 * m.u8(G_SLOT_WEAPON)));
    U e = entity_create(3, 0x310, hash, 0x147, 0, 0x14, 0, 0);
    if (!e) return 0;
    m.w32(e + 8, 1);
    m.w32(e + 0xC, 1);
    m.w32(e + 0x14, 0x88204650);
    m.w8(e + 0x35, 0);
    m.w32(e + 0x204, 0x240000);
    m.w32(e + 0x36C, 0x8007A6B0);
    m.w32(e + 0x370, 0x8007B8F4);
    m.w32(e + 0x374, 0x8007C1D0);
    m.w32(e + 0x378, 0x8007B4B8);
    m.w32(e + 0x37C, 0x8007C030);
    m.w32(e + 0x384, 0x8007BA00);
    return e;
}

// 0x8007BC0C: the soldier's flags, its weapon block's state, standing.
void Loader::soldier_flags_reset(U e, I keep_port) {
    U o = U(m.u32(e + 0x388));
    m.w32(e + 0xC, 0x29);
    m.w32(e + 0x10, m.u8(o + 0x152) == 0 ? 0x21000 : 0x22000);
    m.w32(o + 0x8C, 1);
    m.w8(o + 0x54, 0);
    I f = m.u32(o) & 0x30480000;
    m.w32(o, f);
    m.w32(o + 4, f);
    m.w32(o + 0x40, m.s32(0x80093644));
    player_stand_port(eng, e, keep_port ? m.u8(o + 0x152) : -1);
    m.w32(o + 0x1C0, -1);
    for (U k : {0x13Cu, 0x12Cu, 0x50u, 0x4Cu, 0x1C4u}) m.w32(o + k, 0);
    m.w16(e + 0x2DA, -0x64);
    m.w16(e + 0x2E8, 0xC8);
    m.w16(e + 0x2EC, 0x64);
    m.w16(e + 0x3C8, 0x80);
    if (m.u32(G_PLAYER)) {
        m.w32(e + 0x228, 0);
        m.w32(o + 0x178, 0);
        m.w32(o + 0x17C, 0);
        m.w32(o + 0x180, 0);
        view_pitch_target(eng, o, U(m.u32(m.u32(G_PLAYER) + 0x388)));
    }
}

// player_soldier_init (0x8007BE04).
void Loader::player_soldier_init(U e) {
    U o = U(m.u32(e + 0x388));
    m.w8(o + 0x152, 0);
    m.w32(e + 0xF0, 0x800000);
    m.w32(e + 0xF4, 0x800000);
    m.w32(o + 0x118, -1);
    soldier_flags_reset(e, 0);
    m.w32(e + 0x2D4, 0xF00000);
    m.w32(o + 0x34, 0xF00000);
    m.w32(o, 0x400000);
    m.w16(o + 0xB4, 0xD0);
    m.w8(o + 0x153, 0xFF);
    m.w16(o + 0xB6, 0x56);
    m.w32(o + 8, m.u8(0x80093A00));
    player_pad_config(eng, e, o, m.u8(o + 0x152));
    for (U k : {0x11Cu, 0x124u, 0x12Cu, 0x4Cu, 0x50u, 0x128u, 0x134u, 0x138u}) m.w32(o + k, 0);
    m.w32(e + 0x4C + 4 * 0xE, 0);
    m.w32(e + 0x4C + 4 * 2, 0);
    for (int k = 0; k < 3; k++) m.w16(e + 0x27C + 2 * k, 0);
    m.w32(e + 0x284, 1);
    m.w16(o + 0x140, 0);
    // 0x800469E0: the weapons' ammunition names (weapon 7's from the table's last entry)
    static const U names[13] = {0x8009E834, 0x8009E838, 0x8009E83C, 0x8009E840, 0x8009E844, 0x8009E848, 0x8009E84C,
                                0x8009E870, 0x8009E850, 0x8009E854, 0x8009E858, 0x8009E85C, 0x8009E860};
    for (int k = 0; k < 13; k++) m.w32(G_WEAPONS + 0x44 * k + 0x3C, m.u32(names[k]));
    // 0x8007555C: at the player start record, looking along it
    U wobj = U(m.u32(m.u32(G_WORLD) + 0x388));
    U rec = 0;
    for (I i = 0; i < m.s32(wobj + 0x10); i++) {
        U pr = U(m.u32(wobj + 0x14) + 8 * i);
        U r = U(m.u32(pr + 4));
        if (m.u32(r) == 0x8877B646 && m.s16(r + 0x34) == m.u8(o + 0x152)) {
            rec = r;
            break;
        }
    }
    if (rec) {
        for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, S32(m.s16(rec + 0x10 + 2 * k) << 16));
        m.w32(e + 0x228, S32(m.s16(rec + 0x18) << 16));
        m.w32(e + 0x22C, S32(m.s16(rec + 0x1A) << 16));
        for (int k = 0; k < 3; k++) m.w32(e + 0x258 + 4 * k, m.u32(e + 0x20C + 4 * k));
        m.w32(e + 0x264, m.u32(e + 0x228));
        m.w32(e + 0x268, m.u32(e + 0x22C));
        m.w32(e + 0x230, S32(m.s16(rec + 0x1C) << 16));
        m.w32(e + 0x26C, m.u32(e + 0x230));
        m.w32(o + 0x178, m.u32(e + 0x228));
        m.w32(o + 0x184, m.u32(e + 0x22C));
        m.w32(o + 0x180, S32(m.s32(e + 0x228) + m.s32(e + 0x234) - m.s32(o + 0x178)));
        m.w32(o + 0x18C, S32(m.s32(e + 0x22C) + m.s32(e + 0x238) - m.s32(o + 0x184)));
    }
    if (m.u32(o + 0x1C8) < 2) {
        m.w32(o + 0x180, S32(m.s32(e + 0x228) + m.s32(e + 0x234) - (m.s32(o + 0x178) - m.s32(o + 0x17C) * 4)));
        m.w32(o + 0x18C, S32(m.s32(e + 0x22C) + m.s32(e + 0x238) - (m.s32(o + 0x184) + m.s32(o + 0x188) * 8)));
    } else {
        m.w32(o + 0x180, S32(m.s32(e + 0x228) + m.s32(e + 0x234) - m.s32(o + 0x178)));
        m.w32(o + 0x18C, S32(m.s32(e + 0x22C) + m.s32(e + 0x238) - m.s32(o + 0x184)));
    }
    matrix(e);
    I mi = model_find(0x3C8662);
    m.w32(o + 0x130, mi <= 0 ? 0 : model_at(mi));
    m.w32(o + 0xA4, 0x190);
    m.w32(o + 0xAC, 1);
    m.w32(o + 0xB0, m.u32(e + 0x370));
    I w = m.u8(o + 0x55);
    I stance;
    if (m.u32(o) & 0x20) {
        static const I a[13] = {0, 1, 5, 6, 0, 2, 1, 6, 0, 0, 6, 1, 0};
        stance = w < 13 ? a[w] : 7;
    } else {
        static const I a[13] = {7, 8, 9, 9, 7, 7, 8, 9, 7, 7, 9, 8, 7};
        stance = w < 13 ? a[w] : 7;
    }
    m.w32(o + 0x1C8, stance);
    m.w16(o + 0x190, stance == 5 ? 0x10E : 0x100);
    m.w16(o + 0x192, stance == 5 ? 0x73 : 0x83);
    m.w16(o + 0x194, 0);
    m.w16(o + 0x196, 0);
    m.w32(o + 0x1CC, stance);
    m.w16(o + 0x1D0, 0);
    m.w16(o + 0x1D2, 0);
}

// PsyQ rand (0x8001552C) on the seed at 0x800CA2A4.
I Loader::rand() {
    U seed = U(m.u32(0x800CA2A4) * 0x41C64E6Du + 0x3039u);
    m.w32(0x800CA2A4, seed);
    rand_calls++;
    return (seed >> 16) & 0x7FFF;
}

// level_entry (0x80064374) up to level_load_all: level_read_parameters (0x8004A634) and
// func_80061598's state (the deferred frees, the character pools, the pads' tables).
void Loader::level_entry() {
    if (m.u32(G_GAME + 0xD8) & 8) m.w32(0x800AA174, 1);
    m.w8(G_PARAMS + 8, 0);
    m.w8(G_PARAMS + 0xE, 1);
    m.w8(G_PARAMS + 0x10, 3);
    m.w8(G_PARAMS + 0x11, 0);
    I gm = m.s32(G_GAME + 0x94), gl = m.s32(G_GAME + 0x98);
    m.w32(G_PARAMS, gm + 1);
    m.w32(G_PARAMS + 4, gl + 1);
    m.w8(G_PARAMS + 9, m.u8(G_GAME + 0x80));
    m.w8(G_PARAMS + 0xA, m.s32(G_GAME + 0x88) * 0x2A);
    m.w8(G_PARAMS + 0xB, m.u8(G_GAME + 0x110));
    m.w8(G_PARAMS + 0xC, m.u8(G_GAME + 0x9C));
    m.w8(G_PARAMS + 0xD, m.u8(G_GAME + 0x112));
    m.w8(G_PARAMS + 0xF, m.s32(G_GAME + 0x84) == 0);
    m.w8(G_PARAMS + 0x12, m.u8(G_GAME + 0xE0));
    constexpr U G_LEVEL_END = 0x80038D4C;
    for (I k = 0; k < 0xD; k++) {
        m.w16(G_WEAPONS + 0x44 * k + 6, m.u16(G_GAME + 0x18C + gm * 0x98 + gl * 0x24 + 2 * k));
        m.w16(G_LEVEL_END + 4 * k + 0x10, -1);
        m.w16(G_LEVEL_END + 4 * k + 0x12, 0);
    }
    m.w8(G_LEVEL_END + 1, 0);
    m.w8(G_LEVEL_END, 0);
    m.w32(G_LEVEL_END + 0xC, 0);
    m.w32(G_LEVEL_END + 8, 0);
    // func_80061598: func_8008A554, func_8008EA70, func_8004DDAC
    for (U k = 0; k <= 0x112; k++) m.w32(0x8009F88C + 8 * k, 0);
    m.w32(0x8009F888, 0);
    pools_reset();
    m.w32(0x80093B84, 0);
    m.w32(0x80093B88, 0);
    for (U k = 0; k < 0xA; k++) m.w32(0x80093C04 + 4 * k, 0), m.w32(0x80093C2C + 4 * k, 0);
    note("level_entry: sound_init, texts_clear, hud_init and func_8007B780 (the HUD, SPU, menus) not built");
}

void Loader::pools_reset() {
    m.w32(0x800AB8C8, 0);
    m.w32(0x800AB8CC, 0);
    m.w32(0x800AB8D0, 0);
    for (U k = 0; k < 5; k++) {
        m.w32(G_CHAR_POOL + 0x2814 * k + 0x280C, 0);
        m.w32(G_CHAR_POOL + 0x2814 * k + 0x2810, 1);
        m.w32(G_POSE_POOL + 0x1308 * k + 0x1300, 0);
        m.w32(G_POSE_POOL + 0x1308 * k + 0x1304, 1);
    }
}

// player_reset (0x80061D08) with player_reset_inventory (0x80061A2C).
void Loader::player_reset(U e) {
    U o = U(m.u32(e + 0x388));
    // stats_reset (0x8006192C)
    for (U a = 0x80039CA8; a <= 0x80039D10; a += 4) m.w32(a, 0);
    for (I k = 0; k < 0xD; k++) m.w16(G_WEAPONS + 0x44 * k + 0x30, 0);
    for (U k : {0x38u, 0x3Cu, 0x40u, 0x284u}) m.w32(e + k, 0);
    m.w32(e + 0xF4, m.u32(e + 0xF0));
    soldier_flags_reset(e, 1);
    I first = -1, pref = -1;
    I n = m.s32(G_SLOT_COUNT);
    for (I i = 0; i < n; i++) {
        U w = U(G_WEAPONS + 0x44 * m.u8(G_SLOT_WEAPON + i));
        if (m.u16(w + 0x2A) & 1) {
            m.w16(w + 6, 0);
            m.w16(w + 8, 0);
        }
        I res;
        if (m.u32(G_GAME + 0xD8) & 0x40)
            res = m.u16(w + 8), m.w16(o + 0x5E + 2 * i, res);
        else
            m.w16(o + 0x5E + 2 * i, m.u16(w + 8)), res = m.u16(w + 6);
        m.w16(o + 0x56 + 2 * i, res);
        m.w16(o + 0x66 + 2 * i, 0);
        if (m.s16(o + 0x56 + 2 * i) != -1) {
            if (first == -1) first = i;
            if (pref == -1 && (m.u16(w + 0x2A) & 8)) pref = i;
        }
    }
    if (pref == -1)
        for (I i = 0; i < n; i++)
            if (m.u16(G_WEAPONS + 0x44 * m.u8(G_SLOT_WEAPON + i) + 0x2A) & 8) {
                m.w16(o + 0x56 + 2 * i, 0);
                break;
            }
    if (first == -1) {
        m.w16(o + 0x56, 0);
        m.w8(o + 0x54, 0);
    } else
        m.w8(o + 0x54, first);
    // func_8007BD1C: the player's handlers
    m.w32(e + 0x36C, 0x8007A6B0);
    m.w32(e + 0x370, 0x8007B8F4);
    m.w32(e + 0x374, 0x8007C1D0);
    m.w32(e + 0x378, 0x8007B4B8);
    m.w32(e + 0x37C, 0x8007C030);
    m.w32(e + 0x384, 0x8007BA00);
    m.w32(e + 0x38, m.u32(e + 0x38) | 0x20000);
    {
        VM vm(eng, e);
        vm.run_b();
        vm.run_a();
    }
    m.w32(e + 0x288, 1);
    m.w32(e + 0x28C, 1);
    m.w32(e + 0x21C, 0);
    eng.swap_hook = [this](U ent, U model, I flags) {
        U inst = U(m.u32(ent + 0x100));
        instance_release(inst, ent, 0);
        instance_init(inst, model, flags, ent);
    };
    player_weapon_draw(eng, e);
    eng.swap_hook = nullptr;
    // weapon_draw's tail: the muzzle flash's light (func_8006DCAC) at inv+0x1D4
    U lt = o + 0x1D4;
    I w = m.u8(o + 0x55);
    m.w16(lt, m.u16(G_WEAPONS + 0x44 * w + 0x34));
    m.w16(lt + 2, 0);
    m.copy(lt + 4, G_WEAPONS + 0x44 * w + 0x36, 8);
    m.w32(lt + 0x18, m.u32(e));  // func_8006D84C: the soldier's handle
    m.w32(lt + 0x1C, 0);
    U f = lt + 0x70;
    m.w16(f, 0);
    m.w16(f + 0x10, 0x1F);
    m.w16(f + 2, 0x1F);
    m.w16(f + 6, 0x3F0);
    m.w16(f + 8, 0x80);
    m.w8(f + 0x16, 0);
    m.w8(f + 0x17, 0);
    m.w16(f + 0x12, 0x1F);
    m.w16(f + 4, 0x1F);
    m.w16(f + 0xA, 8);
    m.w16(f + 0xC, 0x3E0);
    m.w16(f + 0xE, 0xF9);
    m.w8(f + 0x14, 2);
    m.w8(f + 0x15, 1);
    // player_reset
    m.w32(0x8009A594, 0);
    m.w32(0x8009A590, 0);
    m.w16(o + 0x142, 0);
    for (U k : {0x144u, 0x148u, 0x14Cu}) m.w32(o + k, 0);
    m.w32(0x8009A598, 0);
    m.w16(o + 0x140, m.u16(o + 0x140) + 1);
    m.w32(0x8009A59C, 0);
    if (m.s32(G_GAME + 0x74))
        m.w32(e + 0xF4, m.u32(G_GAME + 0x1A8 + m.s32(G_GAME + 0x98) * 0x24 + m.s32(G_GAME + 0x94) * 0x98));
    else
        m.w32(e + 0xF4, 0x800000);
    start_position(e);
    matrix(e);
}

// 0x8007555C: at the player start record, looking along it.
void Loader::start_position(U e) {
    U o = U(m.u32(e + 0x388));
    U wobj = U(m.u32(m.u32(G_WORLD) + 0x388));
    U rec = 0;
    for (I i = 0; i < m.s32(wobj + 0x10); i++) {
        U r = U(m.u32(m.u32(wobj + 0x14) + 8 * i + 4));
        if (m.u32(r) == 0x8877B646 && m.s16(r + 0x34) == m.u8(o + 0x152)) {
            rec = r;
            break;
        }
    }
    if (!rec) return;
    for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, S32(m.s16(rec + 0x10 + 2 * k) << 16));
    m.w32(e + 0x228, S32(m.s16(rec + 0x18) << 16));
    m.w32(e + 0x22C, S32(m.s16(rec + 0x1A) << 16));
    for (int k = 0; k < 3; k++) m.w32(e + 0x258 + 4 * k, m.u32(e + 0x20C + 4 * k));
    m.w32(e + 0x264, m.u32(e + 0x228));
    m.w32(e + 0x268, m.u32(e + 0x22C));
    m.w32(e + 0x230, S32(m.s16(rec + 0x1C) << 16));
    m.w32(e + 0x26C, m.u32(e + 0x230));
    m.w32(o + 0x178, m.u32(e + 0x228));
    m.w32(o + 0x184, m.u32(e + 0x22C));
    m.w32(o + 0x180, S32(m.s32(e + 0x228) + m.s32(e + 0x234) - m.s32(o + 0x178)));
    m.w32(o + 0x18C, S32(m.s32(e + 0x22C) + m.s32(e + 0x238) - m.s32(o + 0x184)));
}

// tsp_load (0x8008C850): the chunk file into its slot's buffer (func_80049AC4: the whole sectors
// straight in, the last part sector through the scratch sector 0x80037D90, a 0 after the end),
// tsp_parse (0x8008C514) into the slot's model 0x8009898C + 0x100 * slot, its instance.
void Loader::tsp_load(I chunk_file, I slot) {
    std::string prefix;
    for (U a = L(); m.u8(a); a++) prefix += char(m.u8(a));
    std::string path = prefix + std::to_string(chunk_file) + ".tsp";
    std::string up;
    for (char c : path) up += char(c == '\\' ? '/' : std::toupper(c));
    if (!up.empty() && up[0] == '/') up = up.substr(1);
    std::vector<uint8_t> d;
    std::string local = eng.root + "/extracted/moh/" + up;
    if (!read(local, d)) {
        note("no " + local);
        return;
    }
    U buf = U(m.u32(0x8009904C + 4 * slot));
    I size = I(d.size());
    I whole = (size >> 11) << 11;
    std::copy(d.begin(), d.begin() + whole, m.d.begin() + (buf & Mem::kMask));
    if (size > whole) {
        m.zero(0x80037D90, 0x800);
        std::copy(d.begin() + whole, d.end(), m.d.begin() + (0x80037D90 & Mem::kMask));
        m.copy(buf + whole, 0x80037D90, size - whole);
    }
    m.w8(buf + size, 0);
    m.w32(0x80038590, buf + whole);
    m.w32(0x80038594, size >> 11);
    auto it = disc.find(up);
    if (it != disc.end()) {
        I lba = it->second.lba + (size >> 11) + 150;
        auto bcd = [](I v) { return I(((v / 10) << 4) | (v % 10)); };
        m.w8(0x8003859C, bcd(lba / 75 / 60));
        m.w8(0x8003859D, bcd(lba / 75 % 60));
        m.w8(0x8003859E, bcd(lba % 75));
    }
    U md = U(0x8009898C + (slot << 8));
    m.w32(md + 0x10, buf);
    // tsp_parse
    U t = buf;
    for (U k : {4u, 0xCu, 0x98u, 0x90u, 0xC4u, 0x88u, 0x34u, 0x30u, 0x2Cu, 0x28u, 0x24u, 0x20u, 0x18u}) m.w32(md + k, 0);
    m.w16(md + 0x8C, 0);
    m.w16(md + 0x8E, 0);
    m.w32(md + 0x14, t + m.u32(t + 8));
    m.w16(md + 0x8C, m.u16(t + 0x14));
    m.w32(md + 0x88, t + m.u32(t + 0x18));
    m.w16(md + 0x8E, m.u16(t + 0x1C));
    m.w32(md + 0x90, t + m.u32(t + 0x20));
    U faces = U(t + m.u32(t + 0x10));
    m.w32(md + 0x18, t + m.u32(t + 0x28));
    // tsp_dyn_parse (0x800545D8)
    {
        U p = U(t + m.u32(t + 0x38));
        I n = m.u32(t + 0x34);
        U s = U(0x80099098 + 0x288 * slot);
        if (n == 0) {
            m.w32(s + 0x284, 0);
            m.w32(s + 0x280, 0);
        } else {
            m.w32(s + 0x280, n);
            m.w32(s + 0x284, 1);
            for (I k = 0; k < n; k++) {
                U a = U(s + 0x14 * k);
                m.w32(a, p);
                m.w32(a + 8, p + m.u16(p + 0x16));
                m.w32(a + 0x10, 1);
                m.w32(a + 4, p + m.u16(p + 0x14));
                U fr = U(m.u32(p + 0xC));
                if (fr < 0x63 && m.u8(0x80099AB8 + fr) != 0xFF) {
                    m.w32(a + 0xC, m.u8(0x80099AB8 + fr));
                    note("tsp_dyn_apply at load not built");
                } else {
                    if (fr < 0x63) m.w8(0x80099AB8 + fr, 0);
                    m.w32(a + 0xC, 0);
                }
                p = U(p + m.u32(p));
            }
        }
    }
    // the render tree: nodes 0x24 (children at +0x18..+0x20) or leaves 0x1C (faces at +0x18)
    {
        U base = U(m.u32(md + 0x14));
        U a = base;
        I hi = 0;
        do {
            if (m.u32(a + 0xC) == 0) {
                I c0 = m.s32(a + 0x18), c1 = m.s32(a + 0x1C), c2 = m.s32(a + 0x20);
                hi = std::max({hi, c0, c1, c2});
                m.w32(a + 0x18, c0 >= 0 ? base + c0 : 0);
                m.w32(a + 0x20, c2 >= 0 ? base + c2 : 0);
                m.w32(a + 0x1C, c1 >= 0 ? base + c1 : 0);
                a += 0x24;
            } else {
                m.w32(a + 0x18, faces + m.u32(a + 0x18));
                a += 0x1C;
            }
        } while (hi >= I(a - base));
    }
    // func_8003F1B8 / chunk_collision_setup (0x8003EB44): the slot's collision header
    {
        U src = U(t + m.u32(t + 0x3C));
        U h = U(0x80090DE8 + 0x28 * slot);
        m.w32(0x800AB8F8, h);
        for (U k = 0; k <= 0xA; k += 2) m.w16(h + k, m.u16(src + k));
        m.w16(h + 0xE, m.u16(src + 0xC));
        m.w16(h + 0x10, m.u16(src + 0xE));
        U a1 = src + 0x12;
        U v1 = U(a1 + m.s16(h + 8) * 8);
        m.w32(h + 0x14, a1);
        m.w32(h + 0x18, v1);
        U a3 = U(v1 + m.s16(h + 0xA) * 2);
        m.w16(h + 0xC, m.u16(src + 0x10));
        if (a3 & 3) a3 += 2;
        m.w32(h + 0x1C, a3);
        U v0 = U(a3 + m.s16(h + 0xE) * 8);
        m.w32(h + 0x20, v0);
        m.w32(h + 0x24, v0 + m.s16(h + 0x10) * 8);
    }
    instance_init(U(0x80098D8C + slot * 0xB0), md, 0x16, 0);
}

// stars_init (0x80088744): 255 stars on a dome, their colours from a table read with a
// wandering stride.
void Loader::stars_init(U out, I n) {
    I s7 = (rand() >> 12) & 7;
    U s3 = 0x8009EC6C, s2 = 0x8009F48C, s6 = out, s4 = out + 4;
    for (I i = 0; i < (n & 0xFFFF); i++) {
        s3 += U(s7);
        for (int k = 0; k < 3; k++) m.w8(s2 + k, m.u8(s3 + k));
        s7 = (s7 + 1) & 7;
        I r = rand();
        I a0 = (m.u8(L() + 0x58F) << 8) + I(U32(r * 0x5FFF) >> 15);
        a0 = -a0;
        I v0 = S16(a0);
        I s1 = S32((0x6FFF - v0) * (v0 + 0x6FFF));
        m.w16(s4 - 2, a0);
        I s0 = (rand() >> 3) & 0xFFFF;
        I root = eng.gte.sqrt0(int32_t(s1));
        I c = eng.gte.rcos(int32_t(s0)), sn = eng.gte.rsin(int32_t(s0));
        m.w16(s6, S32(root * c) >> 12);
        m.w16(s4, S32(root * sn) >> 12);
        s2 += 4, s6 += 8, s4 += 8;
    }
}

void Loader::sky() {
    U Lh = L();
    if (m.u32(Lh + 0x594)) {
        m.w16(0x800B7AF0, 0);
        m.w16(0x800B7AF2, m.u32(Lh + 0x594));
        m.w32(0x8009EC68, 1);
        m.w16(0x800B7AF4, m.u16(Lh + 0x596));
        // func_80088170: the sun's two sprites
        I a3 = (m.s16(0x800B7AF4) << 5) / 200;
        for (U k = 0; k < 2; k++) {
            U a = 0x800B7AA0 + 0x28 * k;
            m.w8(a + 3, 9);
            m.w8(a + 7, 0x2C);
            m.w16(a + 0x16, 0xF);
            m.w16(a + 0xE, 0x3FBF);
            m.w8(a + 0xC, 0xC0), m.w8(a + 0xD, 0x60), m.w8(a + 0x14, 0xDF), m.w8(a + 0x15, 0x60);
            m.w8(a + 0x1C, 0xC0), m.w8(a + 0x1D, 0x7F), m.w8(a + 0x24, 0xDF), m.w8(a + 0x25, 0x7F);
            m.w8(a + 4, 0x80), m.w8(a + 5, 0x80), m.w8(a + 6, 0x80);
        }
        m.w16(0x800B7AF8, m.u16(0x800B7AF0) + (S32(a3 * 0xAAB) >> 12));
        m.w16(0x800B7AFA, m.u16(0x800B7AF2));
        m.w16(0x800B7AFC, m.u16(0x800B7AF4));
        m.w16(0x800B7B00, m.u16(0x800B7AF0));
        m.w16(0x800B7B02, m.u16(0x800B7AF2) + a3);
        m.w16(0x800B7B04, m.u16(0x800B7AF4));
    }
    stars_init(0x8009EC94, 0xFF);
    // sky_gradient_init (0x80088980)
    I c0 = m.u8(Lh + 0x58C), c2 = m.u8(Lh + 0x58E), c1 = m.u8(Lh + 0x58D);
    m.w8(0x800B7A91, c0);
    m.w8(0x800B7A92, c2);
    m.w8(0x800B7A94, c0 >> 1);
    m.w8(0x800B7A95, c2 >> 1);
    m.w8(0x800B7A93, c1);
    m.w8(0x800B7A96, c1 >> 1);
    U a2 = 0x800B7850;
    for (int j = 0; j < 2; j++) {
        I y = 0xD2;
        for (int i = 0; i < 8; i++, a2 += 0x24) {
            U v = a2 + 0x22;
            I y2 = y + 0x1E;
            m.w16(v - 0x18, y);
            m.w16(v - 0x10, y);
            y -= 0x1E;
            m.w8(v - 0x1F, 8);
            m.w8(v - 0x1B, 0x38);
            m.w16(v - 0x1A, 0);
            m.w16(v - 0x12, 0x200);
            m.w16(v - 0xA, 0);
            m.w16(v - 8, y2);
            m.w16(v - 2, 0x200);
            m.w16(v, y2);
        }
    }
}

// music_start (0x8008A838) as far as RAM goes: the file's CD entry, the stream slot 1 reading
// it 20 sectors at a time into the 0xA000-byte buffer at 0x800A015C, the ring's state.
void Loader::music_start() {
    constexpr U RING = 0x800A0134;
    char path[64];
    U fmt = m.u32(0x80038658) ? 0x8003C420 : 0x8003C440;
    std::string f;
    for (U a = fmt; m.u8(a); a++) f += char(m.u8(a));
    std::snprintf(path, sizeof path, f.c_str(), int(m.s32(G_PARAMS)), int(m.s32(G_PARAMS + 4)), int(m.s32(G_PARAMS)), int(m.s32(G_PARAMS + 4)));
    m.w8(RING + 0x1C, 0);
    if (!m.u32(RING + 0x10)) m.w32(RING + 0x10, 0x800A015C);
    U buf = U(m.u32(RING + 0x10));
    // func_800498B4 (path, 1, 0x14, buf)
    std::string name = path;
    name = name.substr(name.rfind('\\') + 1);
    for (char& c : name) c = char(std::toupper(c));
    name += ";1";
    U ent = 0;
    for (U e = U(m.u32(G_CD_FILES)); e; e = U(m.u32(e))) {
        std::string n;
        for (U a = e + 0xC; m.u8(a); a++) n += char(m.u8(a));
        if (n == name) {
            ent = e;
            break;
        }
    }
    if (!ent) ent = cd_entry(path);
    if (!ent) return;
    U s = 0x80093B14 + 0x20;
    m.w32(s, ent + 4);
    m.w32(s + 0x14, buf);
    m.w32(s + 0x18, buf);
    m.w32(s + 0xC, 0);
    I size = m.s32(ent + 8);
    m.w32(s + 8, (size + 0x7FF) >> 11);
    m.w32(s + 0x10, 0x14);
    m.w32(s + 4, m.u32(ent + 4));
    m.w8(s + 0x1C, 1);
    m.w32(0x800939F0, 0);
    std::vector<uint8_t> file;
    std::string up;
    for (char c : std::string(path + 1)) up += char(c == '\\' ? '/' : std::toupper(c));
    read(eng.root + "/extracted/moh/" + up, file);
    auto stream = [&]() {  // func_8004977C (1 slot, 20 sectors)
        I n = std::min<I>(0x14, m.s32(s + 8) - m.s32(s + 0xC));
        if (n == 0) {
            m.w8(s + 0x1C, 0);
            return;
        }
        size_t from = size_t(m.s32(s + 0xC)) << 11;
        U dst = U(m.u32(s + 0x14));
        for (I k = 0; k < (n << 11); k++) m.w8(dst + k, from + k < file.size() ? file[from + k] : 0);
        m.w32(s + 0x14, dst + (n << 11));
        m.w32(s + 4, m.s32(s + 4) + n);
        m.w32(s + 0xC, m.s32(s + 0xC) + n);
        m.w32(s + 0x10, m.s32(s + 0x10) - n);
    };
    stream();
    m.w32(RING + 0x14, buf);
    m.w32(RING + 0x18, size);
    m.w32(0x800A012C, size);
    m.w32(RING + 0x14, buf + 0x5000);
    m.w32(0x800A012C, size - 0x5000);
    m.w8(buf + 0x5000 + 0x4FF1, 3);
    m.w32(RING + 0x14, buf);
    m.w32(0x800A012C, size - 0xA000);
    // func_80049854 (1, 0x14)
    m.w8(s + 0x1D, 0);
    m.w32(s + 0x10, 0x14);
    m.w32(s + 0x14, m.u32(s + 0x18));
    stream();
    m.w32(0x800939F0, 1);
    note("music_start: the SPU side (the ring's two halves, voice 0) not modelled");
}

// fade_start (0x8004F748) (1, 0, 15, 0, 255, 255, 255, 0, 0, 0, NULL): the fade-in entity.
void Loader::fade_start() {
    U e = entity_create(0, 0x74, 0xFFFFFFFF, 0, 0, 0, 0, 0);
    if (!e) return;
    m.w32(e + 8, 0x32);
    m.w32(e + 0xC, 0x20000);
    m.w32(e + 0x10, 0x203);
    m.w32(e + 0x14, 0);
    m.w32(e + 0x36C, 0x8004F488);  // fade_think
    m.w32(e + 0x370, 0x8004F670);  // fade_draw (mode 1)
    U o = U(m.u32(e + 0x388));
    I d = 0xF;
    I r0 = 0xFF, g0 = 0xFF, b0 = 0xFF, r1 = 0, g1 = 0, b1 = 0;
    m.w32(e + 0x374, 0);
    m.w32(e + 0x378, 0);
    m.w32(e + 0x384, 0x8006A318);
    m.w32(o, 1);
    m.w32(o + 4, 0);
    m.w32(o + 8, d);
    m.w32(o + 0xC, 0);
    m.w8(o + 0x18, r0), m.w8(o + 0x19, g0), m.w8(o + 0x1A, b0);
    m.w16(o + 0x10, (r1 - r0) / d);
    m.w16(o + 0x12, (g1 - g0) / d);
    m.w16(o + 0x14, (b1 - b0) / d);
    m.w32(o + 0x6C, 0);
    m.w32(o + 0x70, 0);
    // SetPolyFT4 + the fields: two semi-transparent quads over the screen
    for (U q : {o + 0x1Cu, o + 0x44u}) {
        m.w8(q + 3, 9);
        m.w8(q + 7, 0x2C | 2);
    }
    for (U q : {o + 0x1Cu, o + 0x44u}) m.w8(q + 4, r0), m.w8(q + 5, g0), m.w8(q + 6, b0);
    static const U uv[][2] = {{0x28, 0xFC}, {0x29, 0}, {0x30, 0xFF}, {0x31, 0}, {0x38, 0xFC}, {0x39, 3}, {0x40, 0xFF}, {0x41, 3},
                              {0x50, 0xFC}, {0x51, 0}, {0x58, 0xFF}, {0x59, 0}, {0x60, 0xFC}, {0x61, 3}, {0x68, 0xFF}, {0x69, 3}};
    for (auto& p : uv) m.w8(o + p[0], p[1]);
    I tp = ((0x3C0 & 0x3FF) >> 6) | (1 << 7);  // GetTPage(0, 1, 0x3C0, 0)
    tp = (tp & 0xFF9F) | (2 << 5);
    m.w16(o + 0x32, tp);
    m.w16(o + 0x5A, tp);
    I clut = (0xFD << 6) | (0x3E0 >> 4);  // GetClut(0x3E0, 0xFD)
    m.w16(o + 0x2A, clut);
    m.w16(o + 0x52, clut);
    static const U xy[][2] = {{0x24, 0}, {0x26, 0}, {0x2C, 0}, {0x2E, 0xF0}, {0x34, 0x200}, {0x36, 0}, {0x3C, 0x200}, {0x3E, 0xF0},
                              {0x4C, 0}, {0x4E, 0}, {0x54, 0}, {0x56, 0xF0}, {0x5C, 0x200}, {0x5E, 0}, {0x64, 0x200}, {0x66, 0xF0}};
    for (auto& p : xy) m.w16(o + p[0], p[1]);
    link_front(e);
}

// level_start (0x80061E14).
void Loader::level_start() {
    for (int v = 1; v < 0x18; v++) voice_stop(v);
    m.w32(0x80090FC8, 0);  // g_keyon_pending
    m.w32(G_SUBDIV, 0xFA);
    m.w32(0x8009CC6C, 0);
    m.w32(0x8009CC68, 0);
    m.w32(0x8009CC70, 0);
    m.w32(0x8009A60C, 1);  // g_level_starting
    m.w32(0x8009A608, 0);
    m.w32(0x8009CC74, 0);
    m.w32(0x80098620, 0);
    note("music_stop (key off, CD callbacks): SPU only");
    // func_80066290
    m.w32(G_LEVEL_SCRIPTS, 0);
    if (m.u32(G_MSG_SUBS)) {
        heap.free(U(m.u32(G_MSG_SUBS)));
        m.w32(G_MSG_SUBS, 0);
    }
    m.w32(G_MSG_TAIL, 0);
    m.w32(G_MSG_HEAD, 0);
    // func_8008A634: the deferred frees
    for (I k = 0; k < m.s32(0x8009F888); k++) heap.free(U(m.u32(0x8009F88C + 8 * k)));
    m.w32(0x8009F888, 0);
    pools_reset();
    U sol = U(m.u32(G_SOLDIER));
    player_reset(sol);
    camera_entity_init(U(m.u32(G_PLAYER)));
    // func_80074EA8: the spawn queue
    m.w32(0x8009E280, 0);
    m.w32(0x8009E284, 0);
    m.w32(0x8009E288, 0);
    for (U k = 0; k < 0x40; k++) m.w32(0x8009E294 + 4 * k, 0);
    // func_800540AC
    m.w32(0x80099070, 0);
    m.w32(0x80098964, 0);
    m.w32(0x80098968, 0);
    scripts_init();
    // func_80054580
    for (I k = 0; k < 4; k++) {
        m.w32(0x80099098 + 0x288 * k + 0x284, 0);
        m.w32(0x80099098 + 0x288 * k + 0x280, 0);
    }
    for (U k = 0; k <= 0x62; k++) m.w8(0x80099AB8 + k, 0xFF);
    // the level logic over the scripts now in place
    eng.logic = std::make_unique<LevelLogic>(eng);
    eng.ents.clear();
    eng.logic->adopt();
    Ent* se = eng.ents.count(sol) ? eng.ents[sol] : nullptr;
    if (se) eng.logic->msg_start(se, 0);
    // records_clear_state
    U wobj = U(m.u32(m.u32(G_WORLD) + 0x388));
    for (I i = 0; i < m.s32(wobj + 0x10); i++) {
        U p = U(m.u32(wobj + 0x14) + 8 * i);
        m.w32(p, m.u32(p) & ~I(3));
    }
    // func_80075208: each record's start message
    for (I i = 0; i < m.s32(wobj + 0x10); i++) {
        U p = U(m.u32(wobj + 0x14) + 8 * i);
        I kind = m.s32(m.u32(p + 4) + 0xC);
        if (kind > 0 && kind < 7) eng.logic->msg_start(nullptr, p);
    }
    m.w32(0x8009C840, m.u32(0x8009C840) & ~I(2));
    if (m.s32(G_PARAMS) == 9 && m.s32(G_PARAMS + 4) == 4) note("9:4's start voice not built");
    // func_80052C18
    m.w32(0x80097FE0, -1);
    m.w32(0x80097FE4, 0);
    m.w8(0x80098184, 0);
    sky();
    // func_80061DE0: the decals
    m.w32(0x80093C58, -1);
    for (U k = 0; k <= 0x63; k++) m.w32(0x80093C5C + 0x80 * k, 0);
    // tsp_load_initial (0x80053DB0)
    I n = m.s32(L() + 0x84);
    m.w32(0x80098984, n);
    for (I k = 0; k < n; k++) {
        I file = m.s32(0x80099060 + 4 * k);
        tsp_load(file, k);
        m.w32(0x80098970 + 4 * k, file);
    }
    if (m.u32(0x8009A608)) {
        m.w32(0x8009CC70, 1);
        if (!m.u32(0x8009CC74)) {
            m.w32(0x8009CC74, 1);
            music_start();
        }
    }
    m.w32(0x8009A60C, 0);
    fade_start();
}

void Loader::voice_stop(I v) {
    U a = U(0x8009115C + 0x34 * v);
    m.w8(0x800910D0 + v, 0);
    m.w8(a + 0x12, 0);
    m.w8(a + 0xE, 0);
    m.w32(a + 4, 0);
    m.w16(a + 0x28, 0);
}

}  // namespace

std::vector<uint8_t> game_defaults(int mission, int level, int ammo) {
    std::vector<uint8_t> g(0x780, 0);
    auto w32 = [&](size_t o, U v) {
        for (int k = 0; k < 4; k++) g[o + k] = uint8_t(v >> (8 * k));
    };
    auto w16 = [&](size_t o, I v) { g[o] = uint8_t(v), g[o + 1] = uint8_t(v >> 8); };
    std::memcpy(&g[0], "MOH", 4);
    std::memcpy(&g[4], "noname", 7);
    std::memcpy(&g[0x77C], "MOH", 4);
    w32(0x78, 0x7F);
    w32(0x7C, 0x1F);
    w32(0x80, 4);
    w32(0x88, 4);
    w32(0x94, U(mission - 1));
    w32(0x98, U(level - 1));
    w32(0x9C, 5);
    for (size_t p : {0xDCu, 0xF0u}) w32(p + 4, 1), w32(p + 8, 1);  // auto aim, vibration
    w32(0x108, 1);
    w32(0x10C, 1);
    g[0x113] = 1;
    g[0x128] = 1;
    for (int ms = 0; ms < 10; ms++)
        for (int r = 0; r < 4; r++) {
            size_t row = 0x18C + size_t(ms) * 0x98 + size_t(r) * 0x24;
            for (int k = 0; k < 13; k++) w16(row + 2 * size_t(k), -1);
            w16(row + 0x1A, -1);
            if (r == 0) w32(row + 0x1C, 0x800000);
        }
    size_t row = 0x18C + size_t(mission - 1) * 0x98 + size_t(level - 1) * 0x24;
    for (int k = 0; k < 13; k++) w16(row + 2 * size_t(k), ammo);
    w32(row + 0x1C, 0x800000);
    w32(row + 0x20, 1);
    return g;
}

bool Engine::load_disc(const std::vector<uint8_t>& game, LevelBuild& out, std::string* err) {
    std::fill(m.d.begin(), m.d.end(), 0);
    std::vector<uint8_t> exe, ovl;
    if (!read(root + "/extracted/moh/SLUS_009.74", exe) || !read(root + "/extracted/moh/LEVEL.BIN", ovl)) {
        *err = "no SLUS_009.74 or LEVEL.BIN";
        return false;
    }
    auto rd32 = [&](size_t o) { return U(exe[o] | exe[o + 1] << 8 | exe[o + 2] << 16 | uint32_t(exe[o + 3]) << 24); };
    U taddr = rd32(0x18), tsize = rd32(0x1C);
    std::copy(exe.begin() + 0x800, exe.begin() + 0x800 + tsize, m.d.begin() + (taddr & Mem::kMask));
    std::copy(ovl.begin(), ovl.end(), m.d.begin() + (0x80039D90 & Mem::kMask));
    if (game.size() >= 0x780) std::copy(game.begin(), game.begin() + 0x780, m.d.begin() + (G_GAME & Mem::kMask));
    std::string data_dir = root + "/extracted/moh/DATA";
    if (!gte.load(data_dir, err)) return false;
    gte.set_projection(21.0 / 9.0, 110.0);
    audio = std::make_unique<Audio>(*this);
    Loader ld{*this, m, Heap{m}, out, mission_n, level_n, {}};
    if (!ld.load_manifest()) {
        *err = "no extracted/moh/manifest.json";
        return false;
    }
    U hbase = 0x800D17E8;
    U hsize = U((m.u32(0x80039C38) - 8) - m.u32(0x80039C3C) - (hbase & 0x1FFFFFFF));
    m.w32(0x8002F27C, hbase);
    m.w32(0x8002F280, hsize);
    ld.heap.init(hbase, hsize);
    m.w32(0x800A0130, 0xB010);  // sound_reset: the SPU allocator past the reserved area
    m.w32(0x800CA2A4, out.rand_seed);
    ld.level_entry();
    // level_load_all (0x8006240C)
    m.w32(G_FRAME_COUNT, 0);
    m.w32(G_TICK, 0);
    if (!ld.level_load(err)) return false;
    for (U o : {0x28u, 0xE0u, 0x26u, 0xDEu, 0x84u, 0x13Cu, 0x82u, 0x13Au}) m.w8(0x8009A620 + o, 1);
    m.w32(0x8009C840, m.u32(0x8009C840) & ~I(4) & ~I(0x100));
    ld.note("clear_colour_set (the draw environments' clear colour): drawing only");
    ld.level_start();
    out.rand_calls = ld.rand_calls;
    tick_no = m.u32(G_TICK);
    logic = std::make_unique<LevelLogic>(*this);
    ents.clear();
    logic->adopt();
    if (!world.load(*this, data_dir, root + "/build/engine/timing.log", mission_n, level_n)) {
        *err = "the level's collision";
        return false;
    }
    return true;
}

}  // namespace valor::sim
