// coretest: runs the core's player movement on the inputs of a replay of the original and compares
// every tick's position with the original's, as tools/sim/move_sim.py check does.
//
//   coretest MOVES.txt [--data extracted/moh/DATA] [--free] [--all-chunks | --stream [--stream-log LOG]]
//            [-v N] [--trace OUT.jsonl]
//   coretest PAD.txt --ram DUMP.bin [--stream-log RUN.log] [-v N]   (pad_mode.cpp: from the pad itself)
//   coretest --pose M:L DUMP...   (pose_mode.cpp: models, poses, joints, hit parts against RAM)
//   coretest --engine M:L [--to TICK]   (engine_mode.cpp: the whole engine against the trace)
//
// MOVES.txt comes from engine/tools/export_moves.py (the original's per-tick state and the control
// function's outputs). With --free the core carries its own state from tick to tick (only object
// touches, which move the player after the move, are taken from the original); without it, each tick
// starts from the original's state of the tick before. --all-chunks keeps every chunk loaded instead
// of the slots the game had. --stream has the core fill the slots itself: the player's zone tracking
// and the streaming records (records.h), with instant loads, from the level's initial slots. With
// --stream-log, a replay's log of VALOR_WATCH="cs=80098970:10;cl=800AA184:4", the loads take as long
// as the original's did (each completes at the vblank its slot was released), and the slot numbers
// are compared with the game's.
#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "valor/fixed.h"
#include "valor/io.h"
#include "valor/level.h"
#include "valor/records.h"
#include "valor/movement.h"
#include "valor/trace.h"
#include "valor/world.h"

using namespace valor;

namespace {

struct Tick {
    int64_t tick, vblank;
    bool single;
    int32_t pos[3], yaw;
    uint32_t flags_b;
    int32_t health, fwd, strafe, vy, ground, floor, eye, radius;
    uint32_t look_flags;
    int32_t local_fwd, end_push[3], flinch, knock[3], pin[3], lean, ladder;
    bool has_old;
    int32_t old[3];
    uint32_t parent;
    bool has_slots;
    int16_t slots[4][4];
};

bool read_moves(const char* path, std::string& level, std::vector<Tick>& out) {
    std::ifstream f(path);
    std::string line;
    if (!std::getline(f, line)) return false;
    std::istringstream hs(line);
    std::string magic;
    int ver;
    hs >> magic >> ver >> level;
    if (magic != "valor-moves" || ver != 1) return false;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream s(line);
        std::vector<int64_t> v;
        int64_t x;
        while (s >> x) v.push_back(x);
        if (v.size() != 52) return false;
        Tick t{};
        size_t i = 0;
        auto nx = [&] { return v[i++]; };
        t.tick = nx(), t.vblank = nx(), t.single = nx();
        for (auto& p : t.pos) p = int32_t(nx());
        t.yaw = int32_t(nx()), t.flags_b = uint32_t(nx()), t.health = int32_t(nx());
        t.fwd = int32_t(nx()), t.strafe = int32_t(nx()), t.vy = int32_t(nx()), t.ground = int32_t(nx());
        t.floor = int32_t(nx()), t.eye = int32_t(nx()), t.radius = int32_t(nx());
        t.look_flags = uint32_t(nx()), t.local_fwd = int32_t(nx());
        for (auto& p : t.end_push) p = int32_t(nx());
        t.flinch = int32_t(nx());
        for (auto& p : t.knock) p = int32_t(nx());
        for (auto& p : t.pin) p = int32_t(nx());
        t.lean = int32_t(nx()), t.ladder = int32_t(nx()), t.has_old = nx();
        for (auto& p : t.old) p = int32_t(nx());
        t.parent = uint32_t(nx()), t.has_slots = nx();
        for (auto& r : t.slots)
            for (auto& c : r) c = int16_t(nx());
        out.push_back(t);
    }
    return true;
}

const char* classify(const Tick& p, const Tick& t, bool jump, bool walls, bool aiming) {
    if (p.health <= 0) return "dead";
    if (aiming) return "aim";
    if (p.flinch) return "flinch";
    if (t.ladder || p.ladder) return "ladder";
    if (jump) return "jump";
    if (walls) return "wall";
    if (!p.ground || !t.ground) return "air";
    if (t.eye < 200) return t.eye == 80 ? "crouch" : "eye188";
    if (t.pos[1] != p.pos[1]) return "step/slope";
    if (t.fwd || t.strafe) return "run";
    return "still";
}

struct Stat {
    long ticks = 0, exact = 0, sixteenth = 0, one = 0, eight = 0, fail = 0;
    int64_t worst = 0;
};

}  // namespace

int run_pad(int argc, char** argv);   // pad_mode.cpp: coretest PAD.txt (export_pad.py)
int run_pose(int argc, char** argv);  // pose_mode.cpp: coretest --pose M:L DUMP...
int run_engine(int argc, char** argv);
int run_build(int argc, char** argv);
int run_live(int argc, char** argv);
int run_campaign(int argc, char** argv);  // campaign_mode.cpp: coretest --campaign  // live_mode.cpp: coretest --live M:L (no recording)  // build_mode.cpp: coretest --build M:L --against DUMP  // engine_mode.cpp: coretest --engine M:L (the whole engine)

int main(int argc, char** argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "--pose")) return run_pose(argc, argv);
    if (argc >= 2 && !std::strcmp(argv[1], "--engine")) return run_engine(argc, argv);
    if (argc >= 2 && !std::strcmp(argv[1], "--build")) return run_build(argc, argv);
    if (argc >= 2 && !std::strcmp(argv[1], "--live")) return run_live(argc, argv);
    if (argc >= 2 && !std::strcmp(argv[1], "--campaign")) return run_campaign(argc, argv);
    if (argc >= 2) {
        std::ifstream probe(argv[1]);
        std::string magic;
        probe >> magic;
        if (magic == "valor-pad") return run_pad(argc, argv);
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: coretest MOVES.txt [--data DIR] [--free] [--all-chunks | --stream] [-v N] [--trace OUT.jsonl]\n");
        return 2;
    }
    std::string data = "extracted/moh/DATA";
    bool free_run = false, all_chunks = false, stream = false;
    int verbose = 0;
    const char* trace_path = nullptr;
    const char* stream_log = nullptr;
    for (int i = 2; i < argc; i++) {
        if (!std::strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!std::strcmp(argv[i], "--free")) free_run = true;
        else if (!std::strcmp(argv[i], "--all-chunks")) all_chunks = true;
        else if (!std::strcmp(argv[i], "--stream")) stream = true;
        else if (!std::strcmp(argv[i], "--stream-log") && i + 1 < argc) stream_log = argv[++i];
        else if (!std::strcmp(argv[i], "-v") && i + 1 < argc) verbose = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--trace") && i + 1 < argc) trace_path = argv[++i];
    }
    std::string level;
    std::vector<Tick> T;
    if (!read_moves(argv[1], level, T)) {
        std::fprintf(stderr, "coretest: can't read %s\n", argv[1]);
        return 2;
    }
    int m = 0, l = 0;
    std::sscanf(level.c_str(), "%d:%d", &m, &l);
    World world;
    std::string err;
    if (!world.load(data, m, l, &err)) {
        std::fprintf(stderr, "coretest: %s\n", err.c_str());
        return 2;
    }
    std::FILE* trace = trace_path ? std::fopen(trace_path, "w") : nullptr;

    // --stream: the level's records and zone lists, the slots from the initial chunks on.
    LevelInfo info;
    Records recs;
    Streaming streaming;
    ZoneTracker zones;
    long slot_differs = 0, stream_triggers = 0;
    if (stream) {
        std::vector<uint8_t> bsd;
        char rel[64];
        std::snprintf(rel, sizeof rel, "/MSN%d/LVL%d/%d_%d.BSD", m, l, m, l);
        if (!info.load(data, m, l, &err) || !read_file(data + rel, bsd) || !recs.load(bsd, &err)) {
            std::fprintf(stderr, "coretest: %s\n", err.c_str());
            return 2;
        }
        streaming.start(int(info.first_chunk), int(info.chunk_slots));
    }
    // --stream-log: the original's load completions and slot numbers by vblank.
    std::vector<int64_t> completions;
    std::map<int64_t, std::array<int, 4>> game_slots;
    if (stream_log) {
        std::ifstream f(stream_log);
        std::string line;
        // A watch logs changes only: the slots start as the level's initial ones.
        std::array<int, 4> cur;
        for (int i = 0; i < 4; i++) cur[i] = streaming.slot_chunk[i];
        while (std::getline(f, line)) {
            long long v;
            char name[8];
            unsigned off, from, to;
            if (std::sscanf(line.c_str(), "watch %lld %7[a-z]+%x: %x -> %x", &v, name, &off, &from, &to) != 5) continue;
            if (!std::strcmp(name, "cl") && to == 0xFFFFFFFF) completions.push_back(v);
            if (!std::strcmp(name, "cs") && off < 16) {
                cur[off / 4] = int(int32_t(to));
                game_slots[v] = cur;
            }
        }
        streaming.instant = false;
    }
    size_t next_completion = 0;
    long slot_number_differs = 0;
    // After a tick's move: the player's zones, which may stream a chunk in (and the idle job's
    // chunk_stream, once per tick).
    auto zones_after_move = [&](const int32_t pos[3], uint16_t zone) {
        if (!stream) return;
        int32_t ip[3] = {s16(pos[0] >> 16), s16(pos[1] >> 16), s16(pos[2] >> 16)};
        zones.update(recs, zone, ip, true, [&](int ri, bool enter) {
            const Record& r = recs.records[ri];
            if (enter && r.kind == 0 && r.cls == kClassStream) stream_triggers++, streaming.trigger(&r);
        });
        streaming.run();
    };

    std::map<std::string, Stat> stats;
    long inferred = 0, shown = 0;
    bool have = false;
    Walker e;
    int32_t last_fwd = 0, last_strafe = 0;
    const int32_t keys[5] = {0, 0x22DD00, -0x22DD00, 0x8B740, -0x8B740};
    for (size_t k = 1; k < T.size(); k++) {
        const Tick &p = T[k - 1], &t = T[k];
        if (stream) {
            if (t.tick < p.tick) {  // the level restarted
                streaming.start(int(info.first_chunk), int(info.chunk_slots));
                zones = ZoneTracker{};
            }
            while (next_completion < completions.size() && completions[next_completion] <= t.vblank) {
                if (completions[next_completion] > T[k - 1].vblank) streaming.finish();
                next_completion++;
            }
            int cs[4];
            streaming.collision_slots(cs);
            world.set_slots_by_number(cs);
            if (stream_log && !game_slots.empty()) {
                auto it = game_slots.upper_bound(t.vblank - 1);  // the game's slots as of the tick before
                if (it != game_slots.begin()) {
                    --it;
                    if (!std::equal(it->second.begin(), it->second.end(), streaming.slot_chunk)) {
                        if (verbose && slot_number_differs < verbose)
                            std::printf("vbl %" PRId64 ": slot numbers %d %d %d %d, the game's %d %d %d %d\n", t.vblank,
                                        streaming.slot_chunk[0], streaming.slot_chunk[1], streaming.slot_chunk[2],
                                        streaming.slot_chunk[3], it->second[0], it->second[1], it->second[2],
                                        it->second[3]);
                        slot_number_differs++;
                    }
                }
            }
            if (t.has_slots && !stream_log) {
                World game;
                game.chunks = world.chunks;
                game.set_slots_by_rect(t.slots);
                for (int i = 0; i < 4; i++) {
                    int n = game.slots[i] ? game.slots[i]->number : -1;
                    if (n != streaming.slot_chunk[i]) {
                        slot_differs++;
                        if (verbose && slot_differs <= verbose) {
                            std::printf("vbl %" PRId64 " tick %" PRId64 ": slots", t.vblank, t.tick);
                            for (int j = 0; j < 4; j++) std::printf(" %d", streaming.slot_chunk[j]);
                            std::printf(", the game's");
                            for (int j = 0; j < 4; j++) std::printf(" %d", game.slots[j] ? game.slots[j]->number : -1);
                            std::printf("\n");
                        }
                        break;
                    }
                }
            }
        }
        if (t.tick != p.tick + 1 || !(t.single && p.single)) {
            have = false;
            const int32_t* at = t.has_old ? t.old : t.pos;
            zones_after_move(at, world.floor_find(at).zone);
            continue;
        }
        if (stream)
            ;
        else if (t.has_slots && !all_chunks)
            world.set_slots_by_rect(t.slots);
        else
            world.set_all_chunks();
        if (!have || !free_run) {
            e = Walker{};
            std::copy(p.pos, p.pos + 3, e.pos);
            std::copy(p.pos, p.pos + 3, e.old);
            e.vy = p.vy, e.ground = p.ground, e.floor = p.floor;
            have = true;
        }
        // +0x3BC, the position entity_move ended with; an object's touch handler (objects.md,
        // Blocking) can move the player after that.
        if (!free_run) {
            if (p.has_old) std::copy(p.old, p.old + 3, e.old);
            else std::copy(e.pos, e.pos + 3, e.old);
        }
        bool moved_after = p.has_old && !std::equal(p.old, p.old + 3, p.pos);
        if (free_run && moved_after) std::copy(p.pos, p.pos + 3, e.pos);
        // The inputs of tick t as the control function produced them.
        e.fwd = t.fwd, e.strafe = t.strafe, e.yaw = t.yaw, e.eye = t.eye, e.radius = t.radius;
        e.flags_b = t.flags_b;
        bool jump = p.ground && t.vy == -0x180000;
        if (jump) e.ground = 0, e.vy = kJumpVy;
        e.gravity = true;
        if (t.ladder) {
            // ladder_climb set the vertical speed and turned gravity off.
            e.gravity = false;
            e.vy = t.vy;
            if (t.vy < 0) e.ground = 0;
        } else if (p.ladder) {
            e.vy = 0;  // the latch: leaving a ladder zeroes the vertical speed
        }
        bool aiming = t.look_flags & 0x20;
        int64_t push[3] = {0, 0, 0};
        if (aiming) {
            // Aim mode: pinned at inv+0x90, no forward speed, the strafe is the lean.
            push[0] = s32(int64_t(t.pin[0]) - e.pos[0]);
            push[2] = s32(int64_t(t.pin[2]) - e.pos[2]);
            e.pos[1] = t.pin[1];
            e.fwd = 0;
            e.strafe = t.lean;
        } else if (p.look_flags & 0x20) {
            std::copy(t.pin, t.pin + 3, e.pos);  // leaving aim mode: back to the pinned spot
        }
        if (p.flinch && !(t.look_flags & 2)) {
            // The hit flinch: the knockback, less a quarter of the velocity's local components
            // taken as world ones (+0x220 still last tick's).
            int32_t vel[3] = {e.strafe, e.vy, p.local_fwd};
            for (int i = 0; i < 3; i++) push[i] = s32(push[i] + p.knock[i] - (vel[i] >> 2));
        }
        // A push left at the end of the tick before (the touch handler against a soldier or dog).
        for (int i = 0; i < 3; i++) e.push[i] = s32(push[i] + p.end_push[i]);

        // +0x218 (and +0x200 after a soldier touch) read 0 at the end of a tick that cleared them:
        // then try the last tick's values and the keyboard ones (move_sim.py's inference).
        std::vector<std::pair<int32_t, int32_t>> cands = {{e.fwd, e.strafe}};
        if (!aiming && t.strafe == 0) {
            cands.push_back({e.fwd, last_strafe});
            for (int32_t v : keys) cands.push_back({e.fwd, v});
        }
        if (!aiming) {
            int32_t fs[6] = {last_fwd, keys[0], keys[1], keys[2], keys[3], keys[4]};
            int32_t ss[6] = {last_strafe, keys[0], keys[1], keys[2], keys[3], keys[4]};
            for (int32_t f : fs)
                for (int32_t s : ss) cands.push_back({f, s});
        }
        const int32_t* target = t.has_old ? t.old : t.pos;
        int64_t best_err = -1;
        Walker best;
        std::pair<int32_t, int32_t> best_in;
        for (auto [fv, sv] : cands) {
            Walker trial = e;
            trial.fwd = fv, trial.strafe = sv;
            entity_move(world, trial);
            int64_t err = 0;
            for (int i = 0; i < 3; i++) err = std::max(err, iabs(int64_t(trial.pos[i]) - target[i]));
            if (best_err < 0 || err < best_err) best_err = err, best = trial, best_in = {fv, sv};
            if (err == 0) break;
        }
        if (best_in != std::make_pair(e.fwd, e.strafe)) inferred++;
        e = best;
        zones_after_move(e.pos, e.zone);
        last_fwd = best_in.first, last_strafe = best_in.second;

        std::string kind = classify(p, t, jump, e.wall_pushes > 0, aiming);
        if (moved_after) kind = "touched:" + kind;
        if (p.parent || t.parent) kind = "riding";
        Stat& s = stats[kind];
        s.ticks++;
        if (best_err == 0) {
            s.exact++;
        } else {
            s.worst = std::max(s.worst, best_err);
            if (best_err <= 1 << 12) s.sixteenth++;
            else if (best_err <= 1 << 16) s.one++;
            else if (best_err <= 8 << 16) s.eight++;
            else s.fail++;
            if (shown < verbose) {
                shown++;
                std::printf("vbl %" PRId64 " tick %" PRId64 " [%s] core %.4f %.4f %.4f game %.4f %.4f %.4f\n", t.vblank,
                            t.tick, kind.c_str(), e.pos[0] / 65536.0, e.pos[1] / 65536.0, e.pos[2] / 65536.0,
                            target[0] / 65536.0, target[1] / 65536.0, target[2] / 65536.0);
            }
        }
        if (trace) {
            TracePlayer tp;
            tp.look_yaw = e.yaw;
            tp.flags_b = e.flags_b;
            TraceEnt te;
            te.type = 1;
            te.x = e.pos[0], te.y = e.pos[1], te.z = e.pos[2];
            te.yaw = e.yaw;
            te.health = t.health;
            write_trace_line(trace, uint32_t(t.tick), uint32_t(t.vblank), tp, {te});
        }
    }
    if (trace) std::fclose(trace);

    long total = 0, exact = 0;
    std::printf("%-22s %6s %6s %7s %5s %5s %5s  worst\n", "behaviour", "ticks", "exact", "<=1/16", "<=1", "<=8",
                "fail");
    for (auto& [kind, s] : stats) {
        std::printf("%-22s %6ld %6ld %7ld %5ld %5ld %5ld  %.4f\n", kind.c_str(), s.ticks, s.exact, s.sixteenth,
                    s.one, s.eight, s.fail, s.worst / 65536.0);
        total += s.ticks, exact += s.exact;
    }
    std::printf("%s %s: %ld ticks, %ld exact, %ld with the input inferred\n", level.c_str(),
                free_run ? "free-running" : "stepped", total, exact, inferred);
    if (stream)
        std::printf("streaming: %ld triggers, %d loads, %ld ticks with %s unlike the game's\n", stream_triggers,
                    streaming.loads, stream_log ? slot_number_differs : slot_differs,
                    stream_log ? "slot numbers" : "collision slots");
    return exact == total ? 0 : 1;
}
