// coretest --engine M:L [--to TICK] [--max N] [--out TRACE.jsonl] [--dump TICK:PATH]... [--seed VBLANK]
//
// The whole engine (engine/sim, the port of tools/sim/engine.py) run free from the level's seed
// dump on the recording's outside inputs, its state trace compared with the original's every tick
// (tools/sim/engine_cmp.py): the player's look, weapon, ammo and flags B, and every entity's
// handle, type, flags A, position, angles, health, animation and frame, entities matched by order
// within their type. Inputs, as engine.py reads them: build/engine/orig.log (the replay's
// VALOR_TRACE_INPUT / RAND / idle work), orig.jsonl (its state trace), frames.log, timing.log,
// drawn.log and seed/dV.bin.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/load.h"

using namespace valor::sim;

namespace {

const char* kPlayer[] = {"look yaw", "look pitch", "slot", "weapon", "clip", "reserve", "flags B"};
const char* kEnt[] = {"handle", "type", "flags A", "x", "y", "z", "pitch", "yaw", "roll", "health", "animation", "frame"};

struct Compare {
    std::map<I, const StateLine*> orig;
    int maxn = 20, shown = 0;
    std::map<std::string, I> first, count;
    std::map<std::string, std::string> example;
    I ticks = 0, identical = 0, first_any = -1;

    void kind(const std::string& w, I tick, const std::string& detail) {
        if (!first.count(w)) first[w] = tick;
        count[w]++;
        if (!example.count(w)) example[w] = "tick " + std::to_string(tick) + ": " + detail;
    }

    void line(const StateLine& e) {
        auto it = orig.find(e.tick);
        if (it == orig.end()) return;
        const StateLine& o = *it->second;
        ticks++;
        std::set<std::string> kinds;
        std::vector<std::string> diffs;
        for (size_t k = 0; k < e.player.size() && k < o.player.size() && k < 7; k++)
            if (e.player[k] != o.player[k]) {
                std::string w = std::string("player ") + kPlayer[k];
                kinds.insert(w);
                diffs.push_back(w + ": " + std::to_string(e.player[k]) + " / " + std::to_string(o.player[k]));
            }
        // the on-screen texts and the hit wedge timers, when the original's trace has them
        if (o.has_texts) {
            std::vector<std::string> sa, sb;
            for (auto& t : e.texts) sa.push_back(t.first);
            for (auto& t : o.texts) sb.push_back(t.first);
            auto join = [](const std::vector<std::string>& v) {
                std::string r;
                for (auto& x : v) r += (r.empty() ? "'" : ", '") + x + "'";
                return "[" + r + "]";
            };
            if (sa != sb) {
                kinds.insert("texts (strings)");
                diffs.push_back("texts (strings): " + join(sa) + " / " + join(sb));
            } else if (e.texts != o.texts) {
                kinds.insert("texts (fields)");
                std::string d = "texts (fields):";
                for (size_t i = 0; i < e.texts.size(); i++)
                    if (e.texts[i] != o.texts[i]) {
                        d += " '" + sa[i] + "'";
                        for (size_t k = 0; k < e.texts[i].second.size() && k < o.texts[i].second.size(); k++)
                            d += " " + std::to_string(e.texts[i].second[k]) + "/" + std::to_string(o.texts[i].second[k]);
                        break;
                    }
                diffs.push_back(d);
            }
        }
        if (o.has_hud && e.hud != o.hud) {
            kinds.insert("hit wedge timers");
            std::string d = "hit wedge timers:";
            for (size_t k = 0; k < e.hud.size() && k < o.hud.size(); k++) d += " " + std::to_string(e.hud[k]) + "/" + std::to_string(o.hud[k]);
            diffs.push_back(d);
        }
        std::vector<I> ta, tb;
        for (auto& x : e.ents) ta.push_back(x[1]);
        for (auto& x : o.ents) tb.push_back(x[1]);
        if (ta != tb) {
            kinds.insert("entity list (types in order)");
            std::map<I, I> ca, cb;
            for (I t : ta) ca[t]++;
            for (I t : tb) cb[t]++;
            std::string d = "list: engine only {";
            for (auto& [t, n] : ca)
                if (n > cb[t]) d += std::to_string(t) + ":" + std::to_string(n - cb[t]) + " ";
            d += "}, original only {";
            for (auto& [t, n] : cb)
                if (n > ca[t]) d += std::to_string(t) + ":" + std::to_string(n - ca[t]) + " ";
            diffs.push_back(d + "}");
        }
        std::map<I, std::vector<const std::vector<I>*>> ba, bb;
        for (auto& x : e.ents) ba[x[1]].push_back(&x);
        for (auto& x : o.ents) bb[x[1]].push_back(&x);
        for (auto& [typ, vb] : bb) {
            auto& va = ba[typ];
            for (size_t i = 0; i < va.size() && i < vb.size(); i++) {
                if (typ == 0x32 || typ == 0x23 || typ == 0x66) continue;
                const auto& u = *va[i];
                const auto& v = *vb[i];
                for (int k = 1; k < 12; k++)
                    if (u[size_t(k)] != v[size_t(k)]) {
                        char w[64];
                        std::snprintf(w, sizeof w, "type %llX %s", (unsigned long long)typ, kEnt[k]);
                        kinds.insert(w);
                        diffs.push_back(std::string(w) + " #" + std::to_string(i) + ": " + std::to_string(u[size_t(k)]) + " / " +
                                        std::to_string(v[size_t(k)]));
                    }
                if (u[0] != v[0]) kinds.insert("handles");
            }
        }
        for (const std::string& w : kinds) {
            std::string d;
            for (auto& x : diffs)
                if (x.rfind(w, 0) == 0 || w.rfind("entity list", 0) == 0) {
                    d = x;
                    break;
                }
            kind(w, e.tick, d);
        }
        kinds.erase("handles");
        if (kinds.empty())
            identical++;
        else if (first_any < 0)
            first_any = e.tick;
        if (!diffs.empty() && shown < maxn) {
            shown++;
            std::printf("tick %lld:", (long long)e.tick);
            for (size_t i = 0; i < diffs.size() && i < 8; i++) std::printf("%s %s", i ? ";" : "", diffs[i].c_str());
            std::printf("%s\n", diffs.size() > 8 ? " ..." : "");
        }
    }

    void report() {
        std::printf("\n%lld ticks compared, %lld identical (handles aside); first difference at tick %lld\n", (long long)ticks,
                    (long long)identical, (long long)first_any);
        std::vector<std::pair<I, std::string>> order;
        for (auto& [w, t] : first) order.push_back({t, w});
        std::sort(order.begin(), order.end());
        for (auto& [t, w] : order)
            std::printf("  %-36s first at tick %6lld, %6lld ticks   %s\n", w.c_str(), (long long)t, (long long)count[w], example[w].c_str());
    }
};

// engine.py frames_of: the frames of the level's ticks from the frame-counter watch.
// The ticks that end a frame (the tick counter's value at each drawing pass).
std::set<I> frame_ends(const std::string& path) {
    std::set<I> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        long long vb;
        unsigned a, b;
        if (std::sscanf(line.c_str(), "watch %lld fc+4: %X -> %X", &vb, &a, &b) == 3) out.insert(I(b));
    }
    return out;
}

// A dump of the recording near a start: seed/d{V}.bin with V from vb + 1 on, the first holding a
// whole frame (its tick the last of a frame, or the start's own tick when that ran alone); vb + 1
// if there is none (engine.py's pick_seed).
I pick_seed(const std::string& inputs, I vb, I first_tick, const std::set<I>& ends) {
    for (I v = vb + 1; v <= vb + 60; v++) {
        std::ifstream f(inputs + "/seed/d" + std::to_string(v) + ".bin", std::ios::binary);
        if (!f) continue;
        uint8_t w[4];
        f.seekg(G_TICK & 0x1FFFFF);
        if (!f.read(reinterpret_cast<char*>(w), 4)) continue;
        I t = I(w[0] | w[1] << 8 | w[2] << 16 | uint32_t(w[3]) << 24);
        if (t >= first_tick && (ends.count(t) || (t == first_tick && v == vb + 1))) return v;
    }
    return vb + 1;
}

std::vector<std::pair<I, std::vector<I>>> frames_of(const std::string& path, const std::vector<StateLine>& lines) {
    std::vector<std::pair<I, std::vector<I>>> out;
    if (lines.empty()) return out;
    std::set<I> want;
    for (auto& d : lines) want.insert(d.tick);
    I lo = lines.front().vblank - 2, hi = lines.back().vblank + 2;
    std::ifstream f(path);
    std::string line;
    size_t total = 0;
    while (std::getline(f, line)) {
        long long vb;
        unsigned a, b;
        if (std::sscanf(line.c_str(), "watch %lld fc+4: %X -> %X", &vb, &a, &b) != 3) continue;
        if (!(lo <= vb && vb <= hi)) continue;
        std::vector<I> ticks;
        // (a level's first frame: the counter starts again from 0)
        for (I t = I(b) < I(a) ? 1 : I(a) + 1; t <= I(b); t++)
            if (want.count(t)) ticks.push_back(t);
        if (!ticks.empty()) {
            total += ticks.size();
            out.push_back({vb, ticks});
        }
    }
    if (!out.empty() && total == want.size()) {
        // frames with no tick between the level's first and last (paused): the frame counter
        // (fc+0) moved, the tick counter did not
        std::set<I> ticked;
        for (auto& [vb, t] : out) ticked.insert(vb);
        I first = out.front().first, last = out.back().first;
        std::ifstream g(path);
        while (std::getline(g, line)) {
            long long vb;
            if (std::sscanf(line.c_str(), "watch %lld fc+0: ", &vb) != 1 || line.find(" fc+0: ") == std::string::npos) continue;
            if (first < vb && vb < last && !ticked.count(vb)) {
                out.push_back({vb, {}});
                ticked.insert(vb);
            }
        }
        std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.first < b.first; });
        return out;
    }
    // ticks the watch missed (a level built from the disc starts before it): by their lines' vblanks
    std::set<I> have;
    for (auto& [vb, ticks] : out) have.insert(ticks.begin(), ticks.end());
    std::vector<std::pair<I, std::vector<I>>> extra;
    for (auto& d : lines) {
        if (have.count(d.tick)) continue;
        if (!extra.empty() && extra.back().first == d.vblank)
            extra.back().second.push_back(d.tick);
        else
            extra.push_back({d.vblank, {d.tick}});
    }
    out.insert(out.end(), extra.begin(), extra.end());
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.first < b.first; });
    return out;
}

}  // namespace

int run_engine(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: coretest --engine M:L [--to TICK] [--max N] [--out TRACE.jsonl] [--dump TICK:PATH]...\n");
        return 2;
    }
    std::string level = argv[2];
    I to = 0;
    int maxn = 20;
    std::string out_path;
    I seed_arg = 0;
    bool disc = false;
    std::vector<std::pair<std::string, std::string>> heaps;
    std::map<std::string, std::string> dumps;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--to") && i + 1 < argc) to = std::atoll(argv[++i]);
        else if (!std::strcmp(argv[i], "--max") && i + 1 < argc) maxn = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out_path = argv[++i];
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed_arg = std::atoll(argv[++i]);
        else if (!std::strcmp(argv[i], "--disc")) disc = true;
        else if (!std::strcmp(argv[i], "--heap") && i + 1 < argc) {
            // --heap TICK:DUMP: the game's heap at the end of TICK's frame against a dump's, block
            // by block (address, size, allocation serial)
            std::string a = argv[++i];
            size_t c = a.find(':');
            heaps.push_back({a.substr(0, c), a.substr(c + 1)});
            dumps[a.substr(0, c)] = "build/gd/heap_" + a.substr(0, c) + ".bin";
        }
        else if (!std::strcmp(argv[i], "--dump") && i + 1 < argc) {
            std::string a = argv[++i];
            size_t c = a.find(':');
            dumps[a.substr(0, c)] = a.substr(c + 1);
        }
    }
    // the recording's inputs: build/engine (the session), or ENG_INPUTS=DIR (Engine::inputs)
    const char* env_inputs = std::getenv("ENG_INPUTS");
    std::string root = ".";
    std::string inputs = env_inputs && *env_inputs ? std::string(env_inputs) : root + "/build/engine";
    static const std::map<std::string, I> session_seeds = {{"1:3", 1714}, {"2:1", 20496}, {"2:2", 40737}};
    std::map<std::string, I> seeds = env_inputs && *env_inputs ? std::map<std::string, I>{} : session_seeds;
    Feed feed;
    if (!feed.load(root, inputs + "/orig.log")) {
        std::fprintf(stderr, "coretest --engine: %s/orig.log or the recompiler's tables missing\n", inputs.c_str());
        return 2;
    }
    std::vector<StateLine> trace = read_trace(inputs + "/orig.jsonl");
    // level_lines: the original's lines of this level (the marks split them)
    I lo = -1, hi = I(1) << 40;
    for (size_t k = 0; k < feed.marks.size(); k++)
        if (feed.marks[k].second == level) {
            // the mark (the level's first 3D drawn) can come a vblank after its first tick
            lo = feed.marks[k].first - 4;
            if (k + 1 < feed.marks.size()) hi = feed.marks[k + 1].first - 4;
            break;
        }
    std::vector<StateLine> lines;
    for (auto& d : trace)
        if (lo <= d.vblank && d.vblank < hi) lines.push_back(d);
    for (size_t i = 1; i < lines.size(); i++)
        if (lines[i].tick < lines[i - 1].tick) {
            lines.resize(i);
            break;
        }
    // the seed: the dump just after the level's first tick was stamped (its vblank + 1)
    if (!seed_arg && !seeds.count(level) && lines.empty()) {
        std::fprintf(stderr, "coretest --engine: no seed for %s\n", level.c_str());
        return 2;
    }
    std::string frames_log = std::ifstream(inputs + "/frames.log").good() ? inputs + "/frames.log" : inputs + "/orig.log";
    I seed_v = seed_arg ? seed_arg
               : seeds.count(level) ? seeds.at(level)
               // a disc-built start wants the dump right after the first tick (its save data,
               // rand's seed and the timer)
               : disc ? lines[0].vblank + 1
                      : pick_seed(inputs, lines[0].vblank, lines[0].tick, frame_ends(frames_log));
    std::string seed = inputs + "/seed/d" + std::to_string(seed_v) + ".bin";
    if (const char* sf = std::getenv("VALOR_SEED_FILE")) seed = sf;
    Engine eng(root, level, feed);
    std::string err;
    bool ok;
    I pre_v = 0;  // a disc-built start: the dump its save data, seed and timer come from
    if (disc) {
        // the level built from the disc files (load.cpp); the save data, rand's seed and the
        // timer from the last dump before the first tick (level_start done, the tick counter
        // still before it: d1713 for 1:3, d20494 for 2:1, d40734 for 2:2, whose first tick ran
        // two vblanks)
        Mem pre;
        for (I v = seed_v - 1; v >= seed_v - 8 && !pre_v; v--) {
            std::ifstream f(inputs + "/seed/d" + std::to_string(v) + ".bin", std::ios::binary);
            if (f && f.read(reinterpret_cast<char*>(pre.d.data()), 0x200000) && pre.s32(G_TICK) < lines[0].tick) pre_v = v;
        }
        if (!pre_v) {
            // (or, when there's none, the seed itself: the save data is the same; the timer and
            // the seed then a tick late)
            std::ifstream f(seed, std::ios::binary);
            f.read(reinterpret_cast<char*>(pre.d.data()), 0x200000);
            pre_v = seed_v - 1;
        }
        std::vector<uint8_t> game(pre.ptr(0x800385CC), pre.ptr(0x800385CC) + 0x780);
        LevelBuild lb;
        // the level's load began the load's vblanks (0x8009A5C0) before the dump
        lb.rand_from_vblank = pre_v + 1 - I(pre.u32(0x8009A5C0));
        {
            Engine probe(root, level, feed);
            probe.load_disc(game, lb, &err);
        }
        lb.rand_seed = rand_seed_back(U(pre.u32(0x800CA2A4)), lb.rand_calls);
        ok = eng.load_disc(game, lb, &err);
        // the pad as the first frame's read finds it (the recording's: analog), which player_update
        // answers with the controller configuration
        if (feed.analog.count(level)) eng.m.w8(0x80093B78, 1);
        // the vblanks the loading took, counted by the vblank handler (its 20-vblank timer message)
        eng.m.w32(0x8009A5C0, pre.u32(0x8009A5C0));
    } else
        ok = eng.load(seed, &err);
    if (!ok) {
        std::fprintf(stderr, "coretest --engine: %s\n", err.c_str());
        return 2;
    }
    I t0 = eng.tick_no;
    std::vector<StateLine> before, after;
    for (auto& d : lines) (d.tick <= t0 ? before : after).push_back(d);
    eng.frames = frames_of(eng.input_log("frames.log"), after);
    eng.dumps = dumps;
    {
        // ticks with no line in the trace (a disc-built start's tick 1): their own frame first
        I vbs = before.empty() ? seed_v - 1 : before.back().vblank;
        std::vector<I> missing;
        for (I t = t0 + 1; !eng.frames.empty() && t < eng.frames.front().second.front(); t++) missing.push_back(t);
        if (!missing.empty()) eng.frames.insert(eng.frames.begin(), {vbs + 1, missing});
    }
    if (std::getenv("VALOR_FRAMES"))
        for (auto& [vb, ticks] : eng.frames)
            if (!ticks.empty() && ticks.front() < 120) {
                std::fprintf(stderr, "frame %lld:", (long long)vb);
                for (I t : ticks) std::fprintf(stderr, " %lld", (long long)t);
                std::fprintf(stderr, "\n");
            }
    // (a disc-built start counts the timer on from its dump's vblank)
    I vb0 = pre_v ? pre_v : before.empty() ? seed_v - 1 : before.back().vblank;
    eng.vblank_prev = vb0;
    eng.world.skip_to(vb0);
    eng.audio->adopt(vb0);
    // the seed frame's own wait (its idle work as recorded)
    eng.tick_no = t0;
    eng.idle_work();
    // its sound_update: in the seed already, or (a dump in the frame's wait) run now
    eng.seed_sound_update();
    Compare cmp;
    cmp.maxn = maxn;
    for (auto& d : after) cmp.orig[d.tick] = &d;
    std::ofstream out;
    if (!out_path.empty()) out.open(out_path);
    eng.run(to, [&](const StateLine& l) {
        if (out) out << state_json(l) << "\n";
        cmp.line(l);
    });
    std::printf("ran to tick %lld\n", (long long)eng.tick_no);
    for (auto& [k, v] : eng.stats)
        if (k.rfind("think", 0) == 0 || k.find("not") != std::string::npos || k.rfind("view test", 0) == 0) {
            std::printf("  %s: %lld [", k.c_str(), (long long)v);
            for (auto& [t, d] : eng.notes[k]) std::printf(" (%lld %s)", (long long)t, d.c_str());
            std::printf(" ]\n");
        }
    cmp.report();
    for (auto& [t, path] : heaps) {
        auto blocks = [](const std::string& f) {
            std::vector<std::array<long long, 3>> out;
            std::ifstream in(f, std::ios::binary);
            std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            auto w = [&](size_t o) { return uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24); };
            for (uint32_t a = 0x800D17F0; a < 0x80200000 && d.size() >= 0x200000;) {
                size_t o = a & 0x1FFFFF;
                uint32_t sz = w(o), nx = w(o + 4), fl = w(o + 8), ck = w(o + 0xC);
                if (uint32_t(sz + nx + fl + ck) != 0xB562439F || !sz) break;
                out.push_back({a, sz * 16, (fl & 0x80000000) ? (fl >> 16) & 0x7FFF : -1});
                a += sz * 16;
            }
            return out;
        };
        auto a = blocks("build/gd/heap_" + t + ".bin"), b = blocks(path);
        size_t same = 0;
        for (auto& x : a)
            for (auto& y : b)
                if (x == y) same++;
        std::printf("heap at tick %s: %zu blocks built, %zu in %s, %zu identical (address, size, serial)\n", t.c_str(), a.size(),
                    b.size(), path.c_str(), same);
    }
    return 0;
}
