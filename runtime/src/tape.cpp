// Input recording and replay (see tape.h). A recording is a folder: input.tape (text, one event
// per line, each tagged with the vblank it happened in), settings.ini and card/ or card.mcd (the
// memory card as it was at the start; import.mcd if a new save folder would import one). Events:
//   poke CALL ADDR VALUE   --poke-on-call (--warp)
//   window W H             the window's size (replays capture at it)
//   b F BUTTONS            pad 1 buttons as the game read them (active low)
//   s F LX LY RX RY        pad 1 sticks
//   m F FORWARD STRAFE     keyboard movement
//   t F DEGREES / u F ...  mouse turn / pitch taken by the game
//   v F TIME               a frame presented at TIME (vblanks)
//   l F YAW PITCH          the mouse look it showed ahead of the game
//   a F                    the analog button (F7)
//   h F HASH               RAM hash once a second: a replay checks it runs the same game
//   mark F mission M L     a mission started (1-based)
//   mark F user N          Tab (or F9) was pressed
// The first line is "valor-tape VERSION": 2 since drawing copies (shadow.cpp) are kept off the
// devices; a version 1 recording is replayed with copies that use them, as they did then.
// It also keeps log.txt, everything logged while recording, and on a crash ram.bin and
// scratch.bin (main RAM and the scratchpad).
#include "tape.h"

#include <algorithm>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

#include "game.h"
#include "gpu.h"
#include "hw.h"
#include "pad.h"
#include "runtime.h"
#include "settings.h"

namespace psx {

namespace {

enum Mode { OFF, RECORD, REPLAY } mode;
FILE *out;

struct Event { uint64_t frame; double v[4]; };
// Replay: the recorded events of each kind, and how far each has been read.
struct Track { std::vector<Event> events; size_t next = 0; };
Track buttons, sticks, movement, turn, pitch, presents, looks, analog, hashes;
uint64_t last_frame;
bool diverged;
int version = 2;        // replay: the recording's tape version
bool old_copies;        // replay: drawing copies as before 2026-10-20 (tape_copies_touch_devices)

std::filesystem::path record_dir;
FILE *log_file;
bool session;          // a debug session (tape_session_start): named when it closes
std::string started;   // its start time, the start of its name
int first_mission, first_level;  // the first mission played (0: none)
bool was_in_level, mission_pending;
uint64_t level_projections;  // gte_projections when the level overlay came in
int user_marks;
// Replay: the marks, to announce as they pass.
struct Mark { uint64_t frame; std::string text; };
std::vector<Mark> marks;
size_t next_mark;

constexpr uint32_t MISSION = 0x80038660, LEVEL = 0x80038664;  // 0-based, the level being played
constexpr int SESSIONS_KEPT = 20;  // debug sessions kept besides crashes

// Debug sessions beyond the newest SESSIONS_KEPT are removed; crashes are kept.
void prune_sessions() {
    std::vector<std::filesystem::path> dirs;
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator("recordings", ec)) {
        std::string name = e.path().filename().string();
        bool ours = name.size() > 20 && name[4] == '-' && name[10] == '_';
        if (e.is_directory() && ours && name.find("_crash") == std::string::npos) dirs.push_back(e.path());
    }
    std::sort(dirs.begin(), dirs.end());  // by start time
    for (size_t i = 0; i + SESSIONS_KEPT < dirs.size(); i++) std::filesystem::remove_all(dirs[i], ec);
}

Track *track(char kind) {
    switch (kind) {
    case 'b': return &buttons;
    case 's': return &sticks;
    case 'm': return &movement;
    case 't': return &turn;
    case 'u': return &pitch;
    case 'v': return &presents;
    case 'l': return &looks;
    case 'a': return &analog;
    case 'h': return &hashes;
    default: return nullptr;
    }
}

// Once for each kind: what the game reads (controls, RAM) apart from what only the display shows.
void report(const char *what, uint64_t frame) {
    static std::vector<std::string> reported;
    if (std::find(reported.begin(), reported.end(), what) != reported.end()) return;
    reported.push_back(what);
    diverged = true;
    log("replay: the game differs from the recording from vblank %llu (%s): the controls follow in "
        "the order it reads them, the frames shown at the recorded moments", (unsigned long long)frame, what);
}

// The next recorded event of a kind, in the order the game read them: a game changed since the
// recording (its timing shifts with the work it does) still gets the same controls in the same
// sequence, so a fix can be compared on the same play. Null once they run out.
const Event *take(Track &t, const char *what) {
    if (t.next >= t.events.size()) {
        if (hw::gpu.frames < last_frame) report(what, hw::gpu.frames);
        return nullptr;
    }
    if (t.events[t.next].frame != hw::gpu.frames) report(what, hw::gpu.frames);
    return &t.events[t.next++];
}

void write(char kind, int n, const double *v) {
    fprintf(out, "%c %llu", kind, (unsigned long long)hw::gpu.frames);
    for (int i = 0; i < n; i++) fprintf(out, " %.17g", v[i]);
    fputc('\n', out);
}

uint64_t ram_hash() {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < PSX_RAM_SIZE; i += 8) {
        uint64_t w;
        memcpy(&w, psx_ram + i, 8);
        h = (h ^ w) * 0x100000001B3ull;
    }
    return h;
}

void copy_card(const std::filesystem::path &from, const std::filesystem::path &to) {
    std::filesystem::remove_all(to);
    if (std::filesystem::is_directory(from)) {
        std::filesystem::create_directories(to);
        for (const auto &e : std::filesystem::directory_iterator(from))
            if (e.is_regular_file()) std::filesystem::copy_file(e.path(), to / e.path().filename());
    } else if (std::filesystem::exists(from)) {
        std::filesystem::copy_file(from, to);
    }
}

}  // namespace

bool tape_recording() { return mode == RECORD; }
bool tape_replaying() { return mode == REPLAY; }
bool tape_copies_touch_devices() { return mode == REPLAY && old_copies; }

void tape_record(const std::filesystem::path &dir) {
    std::filesystem::create_directories(dir);
    settings_write(dir / "settings.ini");
    // The card as the game will first read it: the save folder, or a new one's import.
    if (std::filesystem::is_directory(config.memcard)) copy_card(config.memcard, dir / "card");
    else if (std::filesystem::exists(config.memcard)) copy_card(config.memcard, dir / "card.mcd");
    else if (std::filesystem::exists(config.memcard_import)) copy_card(config.memcard_import, dir / "import.mcd");
    out = fopen((dir / "input.tape").c_str(), "w");
    if (!out) fatal("can't write %s", (dir / "input.tape").c_str());
    fprintf(out, "valor-tape 2\n");
    record_dir = dir;
    log_file = fopen((dir / "log.txt").c_str(), "w");
    log_to(log_file);
    for (const auto &p : config.pokes) fprintf(out, "poke %X %X %u\n", p.call, p.addr, p.value);
    mode = RECORD;
    log("record: %s", dir.c_str());
}

void tape_session_start() {
    char stamp[32];
    time_t now = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", localtime(&now));
    started = stamp;
    prune_sessions();
    session = true;
    tape_record(std::filesystem::path("recordings") / (started + "_session"));
}

void tape_mark() {
    if (mode != RECORD) return;
    fprintf(out, "mark %llu user %d\n", (unsigned long long)hw::gpu.frames, ++user_marks);
    fflush(out);
    log("record: mark %d at vblank %llu", user_marks, (unsigned long long)hw::gpu.frames);
}

int tape_marks() { return user_marks; }

void tape_replay(const std::filesystem::path &dir) {
    FILE *f = fopen((dir / "input.tape").c_str(), "r");
    if (!f) fatal("no recording at %s", dir.c_str());
    settings_load(dir / "settings.ini");
    config.fullscreen = false;
    // A copy of the card, so the recording stays as it was: one per replay (replays run side by
    // side would otherwise share it, and one's saves would change another's game).
    std::filesystem::path copies = std::filesystem::temp_directory_path() / ("valor-replay-" + std::to_string(getpid()));
    std::filesystem::create_directories(copies);
    config.memcard = copies / (std::filesystem::exists(dir / "card.mcd") ? "card.mcd" : "card");
    config.memcard_import = copies / "import.mcd";
    std::filesystem::remove_all(config.memcard);
    std::filesystem::remove_all(config.memcard_import);
    if (std::filesystem::exists(dir / "card.mcd")) copy_card(dir / "card.mcd", config.memcard);
    else if (std::filesystem::exists(dir / "card")) copy_card(dir / "card", config.memcard);
    else copy_card(dir / "import.mcd", config.memcard_import);
    config.pokes.clear();
    char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned call, addr, value;
        int w, h, v;
        if (sscanf(line, "valor-tape %d", &v) == 1) {
            version = v;
        } else if (sscanf(line, "poke %X %X %u", &call, &addr, &value) == 3) {
            config.pokes.push_back({call, addr, value, false});
        } else if (!strncmp(line, "mark ", 5)) {
            unsigned long long frame;
            int n = 0;
            if (sscanf(line + 5, "%llu %n", &frame, &n) >= 1) {
                std::string text = line + 5 + n;
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
                marks.push_back({frame, text});
            }
        } else if (sscanf(line, "window %d %d", &w, &h) == 2) {
            config.window_w = w;
            config.window_h = h;
        } else if (Track *t = line[0] && line[1] == ' ' ? track(line[0]) : nullptr) {
            Event e{};
            unsigned long long frame;
            if (sscanf(line + 2, "%llu %lf %lf %lf %lf", &frame, &e.v[0], &e.v[1], &e.v[2], &e.v[3]) < 1) continue;
            e.frame = frame;
            t->events.push_back(e);
            last_frame = std::max(last_frame, e.frame);
        }
    }
    fclose(f);
    mode = REPLAY;
    log("replay: %s, vblanks to %llu, %zu frames presented", dir.c_str(), (unsigned long long)last_frame,
        presents.events.size());
    // Version 1 tapes: every one known was recorded before copies were kept off the devices
    // (9629c58, 2026-10-20); none were recorded between that and version 2. VALOR_REPLAY_OLD_COPIES=0/1
    // decides for a tape that isn't so.
    const char *force = getenv("VALOR_REPLAY_OLD_COPIES");
    old_copies = force ? atoi(force) != 0 : version < 2;
    if (old_copies)
        log("replay: a recording made before 2026-10-20: drawing copies use the devices as they did then (it "
            "holds only with a display, which draws them)");
}

void tape_window(int w, int h) {
    if (mode == RECORD) fprintf(out, "window %d %d\n", w, h);
}

uint16_t tape_pad_buttons(uint16_t live) {
    if (mode == RECORD) {
        double v = live;
        write('b', 1, &v);
    } else if (mode == REPLAY) {
        const Event *e = take(buttons, "pad buttons");
        return e ? uint16_t(e->v[0]) : 0xFFFF;
    }
    return live;
}

hw::PadSticks tape_pad_sticks(hw::PadSticks live) {
    if (mode == RECORD) {
        double v[4] = {double(live.lx), double(live.ly), double(live.rx), double(live.ry)};
        write('s', 4, v);
    } else if (mode == REPLAY) {
        const Event *e = take(sticks, "pad sticks");
        if (!e) return {0x80, 0x80, 0x80, 0x80};
        return {uint8_t(e->v[0]), uint8_t(e->v[1]), uint8_t(e->v[2]), uint8_t(e->v[3])};
    }
    return live;
}

void tape_movement(double &forward, double &strafe) {
    if (mode == RECORD) {
        double v[2] = {forward, strafe};
        write('m', 2, v);
    } else if (mode == REPLAY) {
        const Event *e = take(movement, "movement");
        forward = e ? e->v[0] : 0;
        strafe = e ? e->v[1] : 0;
    }
}

double tape_turn(double live) {
    if (mode == RECORD) write('t', 1, &live);
    else if (mode == REPLAY) {
        const Event *e = take(turn, "mouse turn");
        return e ? e->v[0] : 0;
    }
    return live;
}

double tape_pitch(double live) {
    if (mode == RECORD) write('u', 1, &live);
    else if (mode == REPLAY) {
        const Event *e = take(pitch, "mouse pitch");
        return e ? e->v[0] : 0;
    }
    return live;
}

void tape_pending_look(double &yaw, double &pitch_) {
    if (mode == RECORD) {
        double v[2] = {yaw, pitch_};
        write('l', 2, v);
    } else if (mode == REPLAY) {
        const Event *e = take(looks, "mouse look shown");
        yaw = e ? e->v[0] : 0;
        pitch_ = e ? e->v[1] : 0;
    }
}

void tape_analog_toggled() {
    if (mode == RECORD) write('a', 0, nullptr);
}

void tape_presented(double vblank_time) {
    if (mode == RECORD) write('v', 1, &vblank_time);
}

void tape_vblank() {
    uint64_t now = hw::gpu.frames;
    if (mode == RECORD) {
        // A mission starting: the level overlay came in and has drawn 3D (its mission and level
        // are set by then).
        bool in_level = game_in_level();
        if (in_level && !was_in_level) mission_pending = true, level_projections = gte_projections;
        if (!in_level) mission_pending = false;
        if (mission_pending && gte_projections != level_projections) {
            mission_pending = false;
            int m = int(psx_r32(hw::cpu, MISSION)) + 1, l = int(psx_r32(hw::cpu, LEVEL)) + 1;
            fprintf(out, "mark %llu mission %d %d\n", (unsigned long long)now, m, l);
            if (!first_mission) first_mission = m, first_level = l;
            log("record: mission %d:%d at vblank %llu", m, l, (unsigned long long)now);
        }
        was_in_level = in_level;
        if (now % 60 == 0) {
            double v = double(ram_hash() >> 12);  // exact in a double
            write('h', 1, &v);
        }
        if (now % 60 == 0) fflush(out);
    } else if (mode == REPLAY) {
        for (; next_mark < marks.size() && marks[next_mark].frame <= now; next_mark++)
            log("replay: mark %s at vblank %llu", marks[next_mark].text.c_str(), (unsigned long long)marks[next_mark].frame);
        while (analog.next < analog.events.size() && analog.events[analog.next].frame <= now) {
            hw::pad_toggle_analog();
            analog.next++;
        }
        while (hashes.next < hashes.events.size() && hashes.events[hashes.next].frame < now) hashes.next++;
        if (hashes.next < hashes.events.size() && hashes.events[hashes.next].frame == now &&
            hashes.events[hashes.next++].v[0] != double(ram_hash() >> 12))
            report("RAM", now);
    }
}

bool tape_replay_presents(void (*present)(double vblank_time)) {
    uint64_t now = hw::gpu.frames;
    while (presents.next < presents.events.size() && presents.events[presents.next].frame <= now) {
        const Event &e = presents.events[presents.next++];
        if (e.frame == now) present(e.v[0]);
    }
    return now <= last_frame;
}

std::string tape_close(bool crashed) {
    if (mode != RECORD || !out) return "";
    fclose(out);
    out = nullptr;
    std::error_code ec;
    if (crashed) {
        FILE *f = fopen((record_dir / "ram.bin").c_str(), "wb");
        if (f) fwrite(psx_ram, 1, PSX_RAM_SIZE, f), fclose(f);
        f = fopen((record_dir / "scratch.bin").c_str(), "wb");
        if (f) fwrite(psx_scratch, 1, sizeof psx_scratch, f), fclose(f);
    }
    log_to(nullptr);
    if (log_file) fclose(log_file);
    log_file = nullptr;
    if (!session) return record_dir.string();
    // Named after the first mission played; a session that never got to one isn't kept.
    if (!first_mission && !crashed) {
        std::filesystem::remove_all(record_dir, ec);
        return "";
    }
    std::string name = started;
    if (first_mission) name += "_m" + std::to_string(first_mission) + "l" + std::to_string(first_level);
    if (crashed) name += "_crash";
    std::filesystem::path final_dir = record_dir.parent_path() / name;
    std::filesystem::rename(record_dir, final_dir, ec);
    return (ec ? record_dir : final_dir).string();
}

}  // namespace psx
