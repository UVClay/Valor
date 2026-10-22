// coretest --live M:L [--ticks N] [--ammo N] [--hold MASK[@FROM:TO]]... [--turn V] [--view DIST]
//
// The engine played live, with no recording: the level built from the disc and a fresh campaign's
// save data (game_defaults), PsyQ's rand, every chunk's collision, the host's input (pad buttons
// held over tick ranges, a constant turn) and a view that draws whatever is within DIST units of
// the camera. Prints what happened: the shots, the characters alive, the player's health, the
// entity count, every 100 ticks; then the engine's notes. A smoke test for hosts (Godot) that
// drive the engine the same way.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "valor/sim/engine.h"
#include "valor/sim/feed.h"
#include "valor/sim/load.h"
#include "valor/sim/views.h"

using namespace valor::sim;

int run_live(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: coretest --live M:L [--ticks N] [--ammo N] [--hold MASK[@FROM:TO]]... [--turn V] [--forward V] [--view DIST] [--replay] [--aimbot]\n");
        return 2;
    }
    std::string level = argv[2];
    I ticks = 600, ammo = 99, turn = 0, forward = 0, view_dist = 3000;
    struct Hold {
        U mask;
        I from, to;
    };
    std::vector<Hold> holds;
    bool replay = false, aimbot = false;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = std::strtoll(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--ammo") && i + 1 < argc) ammo = std::strtoll(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--turn") && i + 1 < argc) turn = std::strtoll(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--replay")) replay = true;
        else if (!std::strcmp(argv[i], "--aimbot")) aimbot = true;
        else if (!std::strcmp(argv[i], "--forward") && i + 1 < argc) forward = std::strtoll(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--view") && i + 1 < argc) view_dist = std::strtoll(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--hold") && i + 1 < argc) {
            Hold h{0, 1, I(1) << 40};
            std::string a = argv[++i];
            h.mask = U(std::strtoul(a.c_str(), nullptr, 0));
            size_t at = a.find('@');
            if (at != std::string::npos) std::sscanf(a.c_str() + at + 1, "%lld:%lld", (long long*)&h.from, (long long*)&h.to);
            holds.push_back(h);
        }
    }
    Feed feed, rec;
    feed.live = true;
    // --replay: the recording's own input (pad words, roles) tick by tick, the rest live
    if (replay && !rec.load(".", "build/engine/orig.log")) {
        std::fprintf(stderr, "coretest --live: no build/engine/orig.log\n");
        return 2;
    }
    Engine eng(".", level, feed);
    LevelBuild lb;
    lb.rand_seed = 0x12345678;
    std::string err;
    if (!eng.load_disc(game_defaults(level[0] - '0', level[2] - '0', ammo), lb, &err)) {
        std::fprintf(stderr, "coretest --live: %s\n", err.c_str());
        return 2;
    }
    eng.m.w8(0x80093B78, 1);  // an analog pad
    Mem& m = eng.m;
    eng.start_live([&](U e) {
        view::Entity cam = view::camera(m), ent{&m, e};
        double d = 0;
        for (int k = 0; k < 3; k++) {
            double x = ent.position()[size_t(k)] - cam.position()[size_t(k)];
            d += x * x;
        }
        return d < double(view_dist) * double(view_dist);
    });
    U prev = 0;
    for (I t = 1; t <= ticks; t++) {
        U now = 0;
        for (const Hold& h : holds)
            if (h.from <= t && t <= h.to) now |= h.mask;
        feed.live_pad = {now, 0, prev, 0};
        feed.live_role = {forward, 0, 0, turn};
        U held_prev = prev;
        prev = now;
        if (replay) {
            auto it = rec.pad.find({level, t});
            if (it != rec.pad.end()) {
                feed.live_pad = it->second;
                feed.live_pad[0] |= now, feed.live_pad[2] |= held_prev;  // the holds on top
            }
            for (int r = 0; r < 4; r++) {
                feed.live_role[size_t(r)] = rec.role(level, t, 0, r);
                feed.live_role_alt[size_t(r)] = rec.role_alt(level, t, 0, r);
            }
        }
        if (aimbot) {
            // the nearest living character within 1500 units: turn to it and fire every 4th tick
            view::Entity cam = view::camera(m);
            view::Player p = view::player(m);
            double best = 1500, off = 0;
            bool found = false;
            for (const view::Entity& e : view::entities(m)) {
                if ((e.type() != SOLDIER && e.type() != DOG) || e.dead()) continue;
                double dx = e.position()[0] - cam.position()[0], dz = e.position()[2] - cam.position()[2];
                double d = std::sqrt(dx * dx + dz * dz);
                if (d >= best) continue;
                double a = std::atan2(dx, dz) * 4096 / (2 * M_PI);
                double yaw = (p.look_yaw() >> 16) & 0xFFF;
                best = d, off = std::fmod(a - yaw + 4096 * 2.5, 4096) - 2048, found = true;
            }
            if (found) {
                feed.live_role = {I(0), I(0), I(0), I(std::max(-3145728.0, std::min(3145728.0, off * 65536.0)))};
                bool fire = (t % 4) < 2;
                feed.live_pad[0] = (feed.live_pad[0] & ~U(0x40)) | (fire ? 0x40 : 0);
                feed.live_pad[2] = (feed.live_pad[2] & ~U(0x40)) | ((t - 1) % 4 < 2 ? 0x40 : 0);
            }
        }
        I ld0 = m.s32(0x800AA184), s00 = m.s32(0x80098970);
        eng.live_frame();
        if (std::getenv("VALOR_LIVE_SLOTS") && m.s32(0x80098970) != s00) std::printf("tick %lld: slot 0 %lld -> %lld\n", (long long)eng.tick_no, (long long)s00, (long long)m.s32(0x80098970));
        if (m.s32(0x800AA184) != ld0)
            std::printf("tick %5lld: chunk slot %lld %s (slots %lld %lld %lld %lld)\n", (long long)eng.tick_no,
                        (long long)(ld0 == -1 ? m.s32(0x800AA184) : ld0), ld0 == -1 ? "read starts" : "read ends",
                        (long long)m.s32(0x80098970), (long long)m.s32(0x80098974), (long long)m.s32(0x80098978), (long long)m.s32(0x8009897C));
        if (std::getenv("VALOR_LIVE_SEEN")) {
            // characters in front of the view within 1500 units (where a screenshot shows them)
            view::Entity cam = view::camera(m);
            view::Player p = view::player(m);
            for (const view::Entity& e : view::entities(m)) {
                if (e.type() != SOLDIER && e.type() != DOG) continue;
                double dx = e.position()[0] - cam.position()[0], dz = e.position()[2] - cam.position()[2];
                double d = std::sqrt(dx * dx + dz * dz);
                double a = std::atan2(dx, dz) * 4096 / (2 * M_PI);
                double yaw = (p.look_yaw() >> 16) & 0xFFF;
                double off = std::fmod(a - yaw + 4096 * 2.5, 4096) - 2048;
                if (d < 1500 && std::fabs(off) < 300)
                    std::printf("seen %lld: %X at %.0f units, %.0f off, health %.1f%s\n", (long long)eng.tick_no, e.handle(), d, off,
                                e.health() / 65536.0, e.dead() ? " dead" : "");
            }
        }
        if (t % 100 == 0 || t == ticks) {
            int chars = 0, dead = 0, ents = 0;
            for (const view::Entity& e : view::entities(m)) {
                ents++;
                if (e.type() == SOLDIER || e.type() == DOG) (e.dead() ? dead : chars)++;
            }
            view::Entity sol = view::soldier(m);
            view::Player p = view::player(m);
            std::printf("tick %5lld: %3d entities, %d characters alive, %d dead; shots %lld, kills %lld; health %.1f; weapon %d clip %d reserve %d; at (%.0f, %.0f, %.0f)\n",
                        (long long)eng.tick_no, ents, chars, dead, (long long)m.s32(0x80039CA8), (long long)m.s32(0x80039CB8),
                        sol.a ? sol.health() / 65536.0 : 0.0, p.a ? p.weapon() : -1, p.a ? p.clip() : 0, p.a ? p.reserve() : 0,
                        sol.a ? sol.position()[0] : 0.0, sol.a ? sol.position()[1] : 0.0, sol.a ? sol.position()[2] : 0.0);
        }
    }
    for (auto& [k, v] : eng.stats)
        if (k.find("not") != std::string::npos) std::printf("  %s: %lld\n", k.c_str(), (long long)v);
    return 0;
}
