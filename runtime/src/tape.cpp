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
#include "tape.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

void report(const char *what, uint64_t frame) {
    if (diverged) return;
    diverged = true;
    log("replay: no longer the recorded game at vblank %llu (%s); frames after this may differ",
        (unsigned long long)frame, what);
}

// The next recorded event of a kind for this vblank, or null (the game read it more often than
// recorded: it isn't the same game any more).
const Event *take(Track &t, const char *what) {
    uint64_t now = hw::gpu.frames;
    while (t.next < t.events.size() && t.events[t.next].frame < now) {
        report(what, t.events[t.next].frame);  // recorded, never read
        t.next++;
    }
    if (t.next < t.events.size() && t.events[t.next].frame == now) return &t.events[t.next++];
    report(what, now);
    return nullptr;
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

void tape_record(const std::filesystem::path &dir) {
    std::filesystem::create_directories(dir);
    settings_write(dir / "settings.ini");
    // The card as the game will first read it: the save folder, or a new one's import.
    if (std::filesystem::is_directory(config.memcard)) copy_card(config.memcard, dir / "card");
    else if (std::filesystem::exists(config.memcard)) copy_card(config.memcard, dir / "card.mcd");
    else if (std::filesystem::exists(config.memcard_import)) copy_card(config.memcard_import, dir / "import.mcd");
    out = fopen((dir / "input.tape").c_str(), "w");
    if (!out) fatal("can't write %s", (dir / "input.tape").c_str());
    fprintf(out, "valor-tape 1\n");
    for (const auto &p : config.pokes) fprintf(out, "poke %X %X %u\n", p.call, p.addr, p.value);
    mode = RECORD;
    log("record: %s", dir.c_str());
}

void tape_replay(const std::filesystem::path &dir) {
    FILE *f = fopen((dir / "input.tape").c_str(), "r");
    if (!f) fatal("no recording at %s", dir.c_str());
    settings_load(dir / "settings.ini");
    config.fullscreen = false;
    // A copy of the card, so the recording stays as it was.
    config.memcard = std::filesystem::exists(dir / "card.mcd") ? "build/replay-card.mcd" : "build/replay-card";
    config.memcard_import = "build/replay-import.mcd";
    std::filesystem::remove_all(config.memcard);
    std::filesystem::remove_all(config.memcard_import);
    if (std::filesystem::exists(dir / "card.mcd")) copy_card(dir / "card.mcd", config.memcard);
    else if (std::filesystem::exists(dir / "card")) copy_card(dir / "card", config.memcard);
    else copy_card(dir / "import.mcd", config.memcard_import);
    config.pokes.clear();
    char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned call, addr, value;
        int w, h;
        if (sscanf(line, "poke %X %X %u", &call, &addr, &value) == 3) {
            config.pokes.push_back({call, addr, value, false});
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
        if (now % 60 == 0) {
            double v = double(ram_hash() >> 12);  // exact in a double
            write('h', 1, &v);
        }
        if (now % 60 == 0) fflush(out);
    } else if (mode == REPLAY) {
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

void tape_close() {
    if (mode == RECORD && out) {
        fclose(out);
        out = nullptr;
    }
}

}  // namespace psx
