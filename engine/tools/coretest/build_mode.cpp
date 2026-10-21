// coretest --build M:L --against DUMP.bin: the level's starting state built from the disc files
// (engine/sim load.cpp) compared with the original's RAM before the level's first tick, region by
// region: the exe's and the overlay's data, the exe's bss, and the heap block by block (both heaps
// walked; a block is matched by its allocation serial). The save data (g_game) is taken from the
// dump: it is the profile the shell hands over.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <string>

#include "valor/sim/engine.h"
#include "valor/sim/feed.h"
#include "valor/sim/load.h"

using namespace valor::sim;

namespace {

struct Names {
    std::map<U, std::string> by;
    void load(const std::string& path) {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            if (line.size() < 10 || line[0] == '#') continue;
            char* end = nullptr;
            unsigned long a = std::strtoul(line.c_str(), &end, 16);
            if (end != line.c_str() + 8) continue;
            size_t s = line.find_first_not_of(' ', 8), e = line.find_first_of(" \t", s);
            by[U(a)] = line.substr(s, e - s);
        }
    }
    std::string of(U a) const {
        auto it = by.upper_bound(a);
        if (it == by.begin()) return "";
        --it;
        if (a - it->first > 0x2000) return "";
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s+%X", it->second.c_str(), a - it->first);
        return buf;
    }
};

struct Block {
    U addr;
    I size, serial;
    bool used;
};

std::map<I, Block> heap_blocks(const Mem& m) {
    std::map<I, Block> out;
    U a = 0x800D17F0;
    I free_n = 0;
    for (int n = 0; n < 4000 && a < 0x80200000; n++) {
        I sz = m.u32(a), nx = m.u32(a + 4), fl = m.u32(a + 8), ck = m.u32(a + 0xC);
        if (U32(sz + nx + fl + ck) != 0xB562439F || sz == 0) break;
        bool used = fl & 0x80000000;
        I serial = used ? (fl >> 16) & 0x7FFF : -(++free_n);
        out[serial] = {a, sz * 16, serial, used};
        a = U(a + sz * 16);
    }
    return out;
}

void diff_range(const Mem& a, const Mem& b, U lo, U hi, const char* what, const Names& names, int show) {
    I n = 0, total = 0;
    std::vector<std::pair<U, U>> runs;
    for (U x = lo; x < hi; x += 4) {
        total++;
        if (a.u32(x) != b.u32(x)) {
            n++;
            if (!runs.empty() && x - runs.back().second <= 16)
                runs.back().second = x + 4;
            else
                runs.push_back({x, x + 4});
        }
    }
    std::printf("%-28s %08X..%08X  %7lld of %7lld words differ, %zu runs\n", what, lo, hi, (long long)n, (long long)total, runs.size());
    for (int i = 0; i < int(runs.size()) && i < show; i++)
        std::printf("    %08X +%-5X %-34s built %08llX  orig %08llX\n", runs[size_t(i)].first, runs[size_t(i)].second - runs[size_t(i)].first,
                    names.of(runs[size_t(i)].first).c_str(), (unsigned long long)a.u32(runs[size_t(i)].first),
                    (unsigned long long)b.u32(runs[size_t(i)].first));
}

}  // namespace

int run_build(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: coretest --build M:L --against DUMP.bin [--show N] [--out BUILT.bin]\n");
        return 2;
    }
    std::string level = argv[2], against, out_path;
    int show = 8;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--against") && i + 1 < argc) against = argv[++i];
        else if (!std::strcmp(argv[i], "--show") && i + 1 < argc) show = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out_path = argv[++i];
    }
    Mem orig;
    {
        std::ifstream f(against, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "coretest --build: no %s\n", against.c_str());
            return 2;
        }
        f.read(reinterpret_cast<char*>(orig.d.data()), 0x200000);
    }
    std::vector<uint8_t> game(orig.ptr(0x800385CC), orig.ptr(0x800385CC) + 0x780);
    Feed feed;
    if (!feed.load(".", "build/engine/orig.log")) {
        std::fprintf(stderr, "coretest --build: no build/engine/orig.log\n");
        return 2;
    }
    Engine eng(".", level, feed);
    LevelBuild lb;
    std::string err;
    // rand's seed at entry: the dump's seed stepped back over the build's draws (counted by a
    // first build)
    {
        Engine probe(".", level, feed);
        LevelBuild pb;
        std::string e2;
        probe.load_disc(game, pb, &e2);
        lb.rand_seed = rand_seed_back(U(orig.u32(0x800CA2A4)), pb.rand_calls);
    }
    bool ok = eng.load_disc(game, lb, &err);
    const Mem& built = eng.m;
    if (!ok) {
        std::fprintf(stderr, "coretest --build: %s\n", err.c_str());
        return 2;
    }
    if (!out_path.empty()) {
        std::ofstream f(out_path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(built.d.data()), 0x200000);
    }
    Names names;
    names.load("docs/re/names.txt");
    for (auto& n : lb.notes) std::printf("note: %s\n", n.c_str());
    diff_range(built, orig, 0x80010000, 0x80039D90, "exe", names, show);
    diff_range(built, orig, 0x80039D90, 0x800CA2A0, "LEVEL overlay", names, show);
    diff_range(built, orig, 0x800CA2A0, 0x800D17E8, "exe bss", names, show);
    auto hb = heap_blocks(built), ho = heap_blocks(orig);
    std::printf("heap: built %zu blocks, original %zu\n", hb.size(), ho.size());
    for (auto& [serial, ob] : ho) {
        if (!ob.used) continue;
        auto it = hb.find(serial);
        char what[64];
        std::snprintf(what, sizeof what, "block %lld (%llX bytes)", (long long)serial, (unsigned long long)ob.size);
        if (it == hb.end()) {
            std::printf("%-28s %08X  not built\n", what, ob.addr);
            continue;
        }
        if (it->second.addr != ob.addr || it->second.size != ob.size) {
            std::printf("%-28s %08X  built at %08X, %llX bytes\n", what, ob.addr, it->second.addr, (unsigned long long)it->second.size);
            continue;
        }
        diff_range(built, orig, ob.addr, U(ob.addr + ob.size), what, names, show);
    }
    for (auto& [serial, b] : hb)
        if (b.used && !ho.count(serial)) std::printf("block %lld built at %08X, not in the original\n", (long long)serial, b.addr);
    return 0;
}
