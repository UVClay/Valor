// coretest --pad: the core's player driven by the recorded pad itself (engine/tools/export_pad.py),
// free-running from a RAM dump a few vblanks into the level, compared tick by tick with the
// original: position (where entity_move left him), view yaw and pitch, every role_button value, the
// eye height, the crouch, aim and scope flags, the flinch.
//
//   coretest PAD.txt --ram DUMP.bin [--stream-log RUN.log] [--data DIR] [-v N]
//
// The roles' analog values come from the trace (the stick's curve plus the runtime's mouse and
// keyboard: an input); with --stick-only the core computes them from the stick bytes instead, which
// a recording without mouse and keyboard (a monkey run) can check.
#include <algorithm>
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
#include "valor/session.h"

using namespace valor;

namespace {

struct PadTick {
    int64_t tick, vblank;
    int32_t pos[3], yaw, pitch;
    uint32_t flags_b;
    int32_t health, roll;
    uint32_t pad_now, pad_prev, sticks, analog;
    // The soldier as this tick's update found it: the last tick's end.
    int32_t s_pos[3], s_old[3], s_push[3], s_eye;
    uint32_t s_contact, s_flags_b, s_inv, s_inv54, s_flinch;
    int32_t s_kn[3];
    bool end_only;  // the start of the tick after the last
    std::vector<std::pair<uint32_t, int32_t>> ents;  // the list after the tick: (handle, type)
    std::vector<Controls::Call> calls;
};

bool read_pad(const char* path, std::string& level, std::vector<PadTick>& out) {
    std::ifstream f(path);
    std::string line, magic;
    int ver;
    long long dv;
    if (!std::getline(f, line)) return false;
    std::istringstream hs(line);
    hs >> magic >> ver >> level >> dv;
    if (magic != "valor-pad" || ver != 4) return false;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream s(line);
        std::vector<int64_t> v;
        int64_t x;
        while (s >> x) v.push_back(x);
        if (v.size() < 33) return false;
        PadTick t{};
        size_t i = 0;
        auto nx = [&] { return v[i++]; };
        t.tick = nx(), t.vblank = nx();
        for (auto& p : t.pos) p = int32_t(nx());
        t.yaw = int32_t(nx()), t.pitch = int32_t(nx()), t.flags_b = uint32_t(nx()), t.health = int32_t(nx());
        t.roll = int32_t(nx());
        t.pad_now = uint32_t(nx()), t.pad_prev = uint32_t(nx()), t.sticks = uint32_t(nx()), t.analog = uint32_t(nx());
        for (auto& p : t.s_pos) p = int32_t(nx());
        for (auto& p : t.s_old) p = int32_t(nx());
        for (auto& p : t.s_push) p = int32_t(nx());
        t.s_eye = int32_t(nx()), t.s_contact = uint32_t(nx()), t.s_flags_b = uint32_t(nx());
        t.s_inv = uint32_t(nx()), t.s_inv54 = uint32_t(nx()), t.s_flinch = uint32_t(nx()) & 0xFFFF;
        for (auto& p : t.s_kn) p = int32_t(nx());
        int64_t ne = nx();
        for (int64_t k = 0; k < ne && i + 2 <= v.size(); k++) {
            uint32_t h = uint32_t(nx());
            t.ents.push_back({h, int32_t(nx())});
        }
        int64_t n = nx();
        t.end_only = n < 0;
        for (int64_t k = 0; k < n && i + 3 <= v.size(); k++) {
            int r = int(nx()), src = int(nx());
            int32_t val = int32_t(nx());
            t.calls.push_back({r, src, val});
        }
        out.push_back(std::move(t));
    }
    return true;
}

struct Ram {
    std::vector<uint8_t> m;
    uint32_t u32(uint32_t a) const {
        a &= 0x1FFFFF;
        return uint32_t(m[a]) | (uint32_t(m[a + 1]) << 8) | (uint32_t(m[a + 2]) << 16) | (uint32_t(m[a + 3]) << 24);
    }
    int32_t s32(uint32_t a) const { return int32_t(u32(a)); }
    int16_t s16(uint32_t a) const { return int16_t(u32(a) & 0xFFFF); }
    uint16_t u16(uint32_t a) const { return uint16_t(u32(a) & 0xFFFF); }
    uint8_t u8(uint32_t a) const { return m[a & 0x1FFFFF]; }
};

// The core's state from the original's RAM: the player, the controls, the streaming, the zones,
// the ladders.
void seed(Session& s, const Ram& r) {
    Player& p = s.player();
    p.controls.cfg.from_ram(r.m);
    p.controls.pad.now = r.u32(0x80093B7C), p.controls.pad.prev = r.u32(0x80093B84);
    p.controls.pad.analog = r.u8(0x80093B78);
    for (int i = 0; i < 4; i++) p.controls.pad.sticks[i] = r.u8(0x80093B70 + i);
    uint32_t e = r.u32(0x8009D654), inv = r.u32(e + 0x388);
    Walker& w = p.w;
    for (int k = 0; k < 3; k++) {
        w.pos[k] = r.s32(e + 0x20C + 4 * k), w.old[k] = r.s32(e + 0x3BC + 4 * k);
        w.push[k] = r.s32(e + 0x1F0 + 4 * k);
        p.ang_vel[k] = r.s32(e + 0x234 + 4 * k), p.body[k] = r.s32(e + 0x228 + 4 * k);
    }
    w.vy = r.s32(e + 0x21C), w.fwd = r.s32(e + 0x200), w.strafe = r.s32(e + 0x218);
    p.local_fwd = r.s32(e + 0x220);
    w.floor = r.s32(e + 0x208), w.ground = r.s32(e + 0x284);
    w.radius = r.s16(e + 0x2EC);
    p.eye = r.s32(e + 0x2D4), w.eye = p.eye >> 16;
    p.sphere_y = r.s16(e + 0x2DA);
    p.flags_a = r.u32(e + 0xC), w.flags_b = r.u32(e + 0x10);
    p.riding = r.u32(e + 0x2C) != 0;
    w.zone = r.u16(e + 0x390);
    Inv& v = p.inv;
    v.flags = r.u32(inv), v.prev_flags = r.u32(inv + 4), v.eye_stand = r.s32(inv + 0x34);
    v.eye_target = r.s32(inv + 0x3C), v.kick = r.s32(inv + 0x4C), v.climb = r.s32(inv + 0x50);
    v.slot = int8_t(r.u8(inv + 0x54)), v.weapon = r.u8(inv + 0x55);
    v.ladder = r.s32(inv + 0x70), v.ladder_latch = r.s32(inv + 0x74);
    for (int k = 0; k < 3; k++) v.knock[k] = r.s32(inv + 0x80 + 4 * k), v.pin[k] = r.s32(inv + 0x90 + 4 * k);
    v.was_on_ground = r.s32(inv + 0x8C), v.lean = r.s32(inv + 0x9C);
    v.zoom = r.u32(inv + 0xA4), v.zoom_target = r.u32(inv + 0xA8), v.zoom_step = r.u32(inv + 0xAC);
    v.box_x = r.s16(inv + 0xB4), v.box_y = r.s16(inv + 0xB6), v.flinch = r.u16(inv + 0x154);
    v.pitch = r.s32(inv + 0x178), v.cross_y = r.s32(inv + 0x17C), v.yaw = r.s32(inv + 0x184);
    v.turn_rate = r.s32(inv + 0x188), v.cross_sx = r.s16(inv + 0x190), v.cross_sy = r.s16(inv + 0x192);
    v.stance = r.u32(inv + 0x1C8), v.prev_stance = r.u32(inv + 0x1CC);
    p.eye_offset = r.s32(r.u32(r.u32(0x8009D638) + 0x388) + 0x94);

    // Records: g_level (0x8009CC64), its entity table's pairs {state, record}.
    uint32_t lvl = r.u32(0x8009CC64), et = lvl + r.u32(lvl + 0x53C), n = r.u32(et);
    std::map<uint32_t, int> by_pair, by_record;
    std::vector<uint32_t>& state = s.record_state();
    for (uint32_t i = 0; i < n && i < state.size(); i++) {
        uint32_t pair = et + 16 + 8 * i;
        state[i] = r.u32(pair);
        by_pair[pair] = int(i), by_record[r.u32(pair + 4)] = int(i);
    }
    Streaming& st = s.streaming_state();
    for (int i = 0; i < 4; i++) st.slot_chunk[i] = r.s32(0x80098970 + 4 * i);
    st.loading = r.s32(0x800AA184);
    auto rec = by_record.find(r.u32(0x80099070));
    st.record = rec != by_record.end() ? &s.records().records[rec->second] : nullptr;
    ZoneTracker& z = s.zone_state();
    z.zone = z.prev_zone = r.u16(e + 0x390);
    z.bits = z.prev_bits = r.u32(e + 0x394);
    // The entity list, handles and all: the core's behaviours for the types it runs, none for
    // the rest.
    Entities& ents = s.entities();
    ents = Entities{};
    std::vector<Entity*> order;
    for (uint32_t x = r.u32(0x8009D64C); x && (x & 0xFFE00003) == 0x80000000 && order.size() < 512; x = r.u32(x + 4)) {
        int32_t type = r.s32(x + 8);
        auto it = by_pair.find(r.u32(x + 0x1C));
        int rec_i = it != by_pair.end() ? it->second : -1;
        Ladder l{};
        if (type == kTypeLadder) {
            uint32_t o = r.u32(x + 0x388);
            l = Ladder{{r.s32(x + 0x20C), r.s32(x + 0x210), r.s32(x + 0x214)}, r.s32(o), r.s32(o + 4)};
        }
        Entity* e = s.adopt(r.u32(x), type, r.u32(x + 0xC), rec_i, type == kTypeLadder ? &l : nullptr);
        if (e) order.push_back(e);
    }
    ents.set_order(order);
    ents.serial = r.u32(0x8009D670), ents.slots_used = r.u32(0x8009D674);
    s.set_tick(r.u32(0x8009A5DC));
    s.set_vbl_timer(r.s32(0x8009A5C0));
    auto held = by_pair.find(r.u32(0x8009A5B8));
    s.held_ladder() = held != by_pair.end() ? held->second : -1;
}

const char* classify(const PadTick& t, const Player& p, bool aiming) {
    if (p.dead) return "dead";
    if (aiming) return "aim";
    if (p.inv.flinch) return "flinch";
    if (p.inv.ladder_latch) return "ladder";
    if (!p.w.ground) return "air";
    if (p.inv.flags & 2) return "crouch";
    if (p.w.wall_pushes) return "wall";
    if (t.pad_now & 0xFFFF) return "buttons";
    return "walk";
}

struct Stat {
    long ticks = 0, pos = 0, yaw = 0, pitch = 0, state = 0, state_checked = 0;
};

}  // namespace

int run_pad(int argc, char** argv) {
    std::string data = "extracted/moh/DATA";
    const char* ram_path = nullptr;
    const char* stream_log = nullptr;
    int verbose = 0;
    bool stick_only = false;
    for (int i = 2; i < argc; i++) {
        if (!std::strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!std::strcmp(argv[i], "--ram") && i + 1 < argc) ram_path = argv[++i];
        else if (!std::strcmp(argv[i], "--stream-log") && i + 1 < argc) stream_log = argv[++i];
        else if (!std::strcmp(argv[i], "-v") && i + 1 < argc) verbose = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--stick-only")) stick_only = true;
    }
    std::string level;
    std::vector<PadTick> T;
    if (!read_pad(argv[1], level, T) || T.empty() || !ram_path) {
        std::fprintf(stderr, "coretest --pad: can't read %s, or no --ram\n", argv[1]);
        return 2;
    }
    int m = 0, l = 0;
    std::sscanf(level.c_str(), "%d:%d", &m, &l);
    Session s;
    std::string err;
    Ram ram;
    if (!s.load(data, m, l, &err) || !read_file(ram_path, ram.m) || ram.m.size() < 0x200000) {
        std::fprintf(stderr, "coretest --pad: %s\n", err.empty() ? "bad RAM dump" : err.c_str());
        return 2;
    }
    seed(s, ram);
    s.player().mission = m, s.player().level = l;
    // The original's read times (capture.sh watches cl): each load completes when its slot was
    // released.
    std::vector<int64_t> completions;
    if (stream_log) {
        std::ifstream f(stream_log);
        std::string line;
        while (std::getline(f, line)) {
            long long v;
            char name[8];
            unsigned off, from, to;
            if (std::sscanf(line.c_str(), "watch %lld %7[a-z]+%x: %x -> %x", &v, name, &off, &from, &to) == 5 &&
                !std::strcmp(name, "cl") && to == 0xFFFFFFFF)
                completions.push_back(v);
        }
        s.streaming_state().instant = false;
    } else {
        s.streaming_state().instant = true;
    }
    size_t next_completion = 0;
    while (next_completion < completions.size() && completions[next_completion] <= T[0].vblank - 1) next_completion++;

    std::map<std::string, Stat> stats;
    long calls = 0, calls_ok = 0, axis = 0, axis_ok = 0, shown = 0;
    bool last_pos_ok = true;
    long teleports = 0;
    long ladders_made = 0, list_same = 0, foreign_spawns = 0, foreign_removals = 0, own_diff = 0, own_missed = 0;
    int64_t first_bad = -1;
    for (size_t k = 0; k + 1 < T.size(); k++) {
        const PadTick &t = T[k], &n = T[k + 1];  // n: the start of the next tick, this one's end
        if (t.end_only) break;
        Player& p = s.player();
        // Loads that finished before this tick.
        while (next_completion < completions.size() && completions[next_completion] <= t.vblank) {
            if (k == 0 || completions[next_completion] > T[k - 1].vblank) s.streaming_state().finish();
            next_completion++;
        }
        // Events of systems not ported, as this tick's update finds them.
        PlayerEvents ev;
        ev.set_weapon = true, ev.slot = int8_t(t.s_inv54 & 0xFF), ev.weapon = uint8_t(t.s_inv54 >> 8);
        ev.set_friction = true, ev.friction = t.s_inv;
        ev.set_roll = true, ev.roll = t.roll;  // the body's roll after this tick (aim_update)
        ev.set_flags_b = true, ev.flags_b = (p.w.flags_b & 0x10) | (t.s_flags_b & ~0x10u);
        if (t.s_flinch == 8) {
            ev.flinch = true;
            std::copy(t.s_kn, t.s_kn + 3, ev.knock);
        }
        ev.dies = t.s_contact & 0x4000;
        // The push as the update finds it: what touch handlers left (the core's own is 0 there).
        std::fill(p.w.push, p.w.push + 3, 0);
        std::copy(t.s_push, t.s_push + 3, ev.push);
        ev.touch_cleared = t.s_push[0] || t.s_push[1] || t.s_push[2];
        // The level logic (not ported) put him somewhere: a restart's script, or between the
        // dump and the first tick (the dump is taken before the frame's queued messages run).
        if (!std::equal(t.s_old, t.s_old + 3, p.w.old) && (k == 0 || last_pos_ok)) {
            ev.teleport = true;
            std::copy(t.s_old, t.s_old + 3, ev.teleport_pos);
            teleports++;
        }
        if (!std::equal(t.s_pos, t.s_pos + 3, t.s_old)) {
            ev.moved = true;
            std::copy(t.s_pos, t.s_pos + 3, ev.pos);
        }
        // weapon_draw this tick (the scripts' variable 0x11): the weapon in hand changes.
        if (((n.s_inv54 ^ t.s_inv54) & 0xFFFF) != 0) {
            ev.weapon_draw = true;
            ev.draw_slot = int8_t(n.s_inv54 & 0xFF), ev.draw_weapon = uint8_t(n.s_inv54 >> 8);
            ev.draw_eye = n.s_eye;
        }
        // The pad, and the analog roles' values the original's role_axis returned.
        Controls& c = p.controls;
        c.pad.now = t.pad_now, c.pad.prev = t.pad_prev, c.pad.analog = uint8_t(t.analog);
        for (int i = 0; i < 4; i++) c.pad.sticks[i] = uint8_t(t.sticks >> (8 * i));
        for (int r = 0; r < 4; r++) c.axis_override[r] = false;
        for (const Controls::Call& call : t.calls)
            if (call.src == 1 && !stick_only && !c.axis_override[call.role])
                c.axis_override[call.role] = true, c.axis_value[call.role] = call.value;
        uint32_t serial_before = s.entities().serial;
        s.tick(ev);
        if (s.entities().serial != serial_before) ladders_made += long((s.entities().serial - serial_before) & 0xFFFF);
        // The entity list: the core's own changes (ladders) against the original's, then the
        // original's list taken as it is (spawns and removals of systems not ported).
        {
            Entities& ents = s.entities();
            std::vector<Entity*> order;
            bool same = ents.list().size() == t.ents.size();
            for (size_t i = 0; same && i < t.ents.size(); i++)
                same = ents.list()[i]->handle == t.ents[i].first && ents.list()[i]->type == t.ents[i].second;
            list_same += same;
            for (const auto& [h, type] : t.ents) {
                Entity* e = ents.resolve(h);
                if (e && e->handle == h && !(e->flags_a & kFlagARemoved)) {
                    order.push_back(e);
                    continue;
                }
                e = s.adopt(h, type, 0, -1);
                if (!e) continue;
                foreign_spawns++;
                if (type == kTypeLadder || type == kTypeSoldierPlayer || type == kTypeWorld) own_missed++;
                if (((h >> 16) - ents.serial) & 0x8000 ? false : ((h >> 16) != ents.serial)) ents.serial = h >> 16;
                order.push_back(e);
            }
            for (Entity* e : std::vector<Entity*>(ents.list()))
                if (std::find(order.begin(), order.end(), e) == order.end()) {
                    // Gone in the original: removed by a system not ported, or a core removal it
                    // didn't make.
                    if (e->type == kTypeLadder || e->type == kTypeSoldierPlayer || e->type == kTypeWorld) own_diff++;
                    ents.remove(s, e);
                    foreign_removals++;
                }
            ents.set_order(order);
        }
        // The end of a drawn frame: the queue, the handles, the streaming check.
        if (n.vblank != t.vblank || n.end_only) {
            s.end_frame();
            if (!n.end_only) s.vblanks(int(n.vblank - t.vblank));
        }
        // Compare with the original's end of this tick.
        bool aiming = p.inv.flags & 0x20;
        std::string kind = classify(t, p, aiming);
        Stat& st = stats[kind];
        st.ticks++;
        bool pos_ok = std::equal(p.w.pos, p.w.pos + 3, n.s_old);
        last_pos_ok = pos_ok;
        bool yaw_ok = p.inv.yaw == t.yaw, pitch_ok = p.inv.pitch == t.pitch;
        st.pos += pos_ok, st.yaw += yaw_ok, st.pitch += pitch_ok;
        st.state_checked++;
        bool state_ok = p.eye == n.s_eye && ((p.inv.flags ^ n.s_inv) & 0x172) == 0 &&
                        (n.s_flinch == 8 || p.inv.flinch == n.s_flinch) && ((p.w.flags_b ^ n.s_flags_b) & 0x10) == 0;
        st.state += state_ok;
        // role_button's values, call by call.
        std::vector<Controls::Call> mine, theirs;
        for (const auto& x : c.calls)
            if (x.src == 0) mine.push_back(x);
        for (const auto& x : t.calls)
            if (x.src == 0) theirs.push_back(x);
        bool calls_same = mine.size() == theirs.size();
        for (size_t i = 0; calls_same && i < mine.size(); i++)
            calls_same = mine[i].role == theirs[i].role && mine[i].value == theirs[i].value;
        calls += long(theirs.size());
        if (calls_same) calls_ok += long(theirs.size());
        // role_axis's values, call by call (from the sticks with --stick-only; else they are the
        // original's, an input).
        std::vector<Controls::Call> my_axes, their_axes;
        for (const auto& x : c.calls)
            if (x.src == 1) my_axes.push_back(x);
        for (const auto& x : t.calls)
            if (x.src == 1) their_axes.push_back(x);
        for (size_t i = 0; i < their_axes.size(); i++) {
            axis++;
            bool same = i < my_axes.size() && my_axes[i].role == their_axes[i].role && my_axes[i].value == their_axes[i].value;
            axis_ok += same;
            if (!same && stick_only && verbose && axis - axis_ok <= 5)
                std::printf("tick %" PRId64 ": role_axis(%d) core %d, the original %d (sticks %08X, now %08X)\n", t.tick,
                            their_axes[i].role, i < my_axes.size() ? my_axes[i].value : 0, their_axes[i].value, t.sticks,
                            t.pad_now);
        }
        bool ok = pos_ok && yaw_ok && pitch_ok && state_ok && calls_same;
        if (!ok && p.dead) ok = true;  // the death camera carries him: not compared
        if (!ok) {
            if (first_bad < 0) first_bad = t.tick;
            if (shown < verbose) {
                shown++;
                std::printf("vbl %" PRId64 " tick %" PRId64 " [%s]%s%s%s%s%s\n", t.vblank, t.tick, kind.c_str(),
                            pos_ok ? "" : " pos", yaw_ok ? "" : " yaw", pitch_ok ? "" : " pitch",
                            state_ok ? "" : " state", calls_same ? "" : " roles");
                if (!pos_ok)
                    std::printf("   pos core %.4f %.4f %.4f game %.4f %.4f %.4f (core fwd %d strafe %d vy %d yaw %d)\n",
                                p.w.pos[0] / 65536.0, p.w.pos[1] / 65536.0, p.w.pos[2] / 65536.0, n.s_old[0] / 65536.0,
                                n.s_old[1] / 65536.0, n.s_old[2] / 65536.0, p.w.fwd, p.w.strafe, p.w.vy, p.w.yaw);
                if (!yaw_ok || !pitch_ok)
                    std::printf("   yaw core %.4f game %.4f, pitch core %.4f game %.4f\n", p.inv.yaw / 65536.0,
                                t.yaw / 65536.0, p.inv.pitch / 65536.0, t.pitch / 65536.0);
                if (!state_ok)
                    std::printf("   eye %d/%d inv %08X/%08X flinch %d/%d flags B %X/%X\n", p.eye >> 16, n.s_eye >> 16,
                                p.inv.flags, n.s_inv, p.inv.flinch, n.s_flinch, p.w.flags_b, n.s_flags_b);
                if (!calls_same) {
                    std::printf("   roles core");
                    for (auto& x : mine) std::printf(" %d:%d", x.role, x.value);
                    std::printf(" game");
                    for (auto& x : theirs) std::printf(" %d:%d", x.role, x.value);
                    std::printf("\n");
                }
            }
        }
    }
    std::printf("%-10s %6s %6s %6s %6s %8s\n", "behaviour", "ticks", "pos", "yaw", "pitch", "state");
    long total = 0, all_pos = 0, all_yaw = 0, all_pitch = 0, all_state = 0, all_checked = 0;
    for (auto& [kind, st] : stats) {
        std::printf("%-10s %6ld %6ld %6ld %6ld %4ld/%-4ld\n", kind.c_str(), st.ticks, st.pos, st.yaw, st.pitch, st.state,
                    st.state_checked);
        total += st.ticks, all_pos += st.pos, all_yaw += st.yaw, all_pitch += st.pitch;
        all_state += st.state, all_checked += st.state_checked;
    }
    std::printf("%s from the pad, free-running: %ld ticks; exact position %ld, yaw %ld, pitch %ld; state %ld of %ld "
                "ticks; role_button %ld of %ld calls; role_axis from the sticks %ld of %ld; first difference "
                "at tick %" PRId64 "\n",
                level.c_str(), total, all_pos, all_yaw, all_pitch, all_state, all_checked, calls_ok, calls, axis_ok,
                axis, first_bad);
    std::printf("entity list: the core's equal to the original's after %ld of %ld ticks; %ld spawns and %ld "
                "removals by systems not ported taken from it; the core's own entities (ladders): %ld made that the "
                "original didn't, %ld the original made that the core didn't, %ld made in all\n",
                list_same, total, foreign_spawns, foreign_removals, own_diff, own_missed, ladders_made);
    std::printf("the level logic placed the player %ld times\n", teleports);
    return first_bad < 0 ? 0 : 1;
}
