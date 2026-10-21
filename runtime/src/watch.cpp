// Reverse engineering traces.
//
// Memory: log what changes in chosen memory from one game tick to the next.
// VALOR_WATCH="name=EXPR:LEN;..." where EXPR is a hex address, [EXPR] (the word there), or
// EXPR+HEX / EXPR-HEX; e.g. "inv=[8009D654]+3CC:80". Each tick, every changed aligned word is
// logged as "watch VBLANK name+OFF: OLD -> NEW" (VALOR_WATCH_HALF=1: halfwords, signed decimal).
//
// State: VALOR_TRACE_STATE=FILE writes the game's state after every tick as a JSON line (below),
// a reference for checking a reimplementation tick by tick against the original.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game.h"
#include "gpu.h"
#include "hw.h"
#include "runtime.h"
#include "shadow.h"

namespace psx {
namespace {

struct Watch {
    std::string name, expr;
    uint32_t len;
    uint32_t base = 0;
    std::vector<uint8_t> last;
};

std::vector<Watch> watches;
bool half;

// Parses EXPR from s at i; reads memory for [..]. Returns 0 for an address outside RAM.
uint32_t eval(Cpu *c, const std::string &s, size_t &i) {
    uint32_t v;
    if (s[i] == '[') {
        i++;
        uint32_t a = eval(c, s, i);
        if (i < s.size() && s[i] == ']') i++;
        v = (a & 0xFFE00003) == 0x80000000 ? psx_r32(c, a) : 0;
    } else {
        size_t n;
        v = uint32_t(std::stoul(s.substr(i), &n, 16));
        i += n;
    }
    while (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        bool neg = s[i++] == '-';
        size_t n;
        uint32_t k = uint32_t(std::stoul(s.substr(i), &n, 16));
        i += n;
        v = neg ? v - k : v + k;
    }
    return v;
}

void parse() {
    static bool parsed;
    if (parsed) return;
    parsed = true;
    const char *env = getenv("VALOR_WATCH");
    half = getenv("VALOR_WATCH_HALF") != nullptr;
    if (!env) return;
    std::string all = env;
    size_t start = 0;
    while (start < all.size()) {
        size_t end = all.find(';', start);
        std::string item = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? all.size() : end + 1;
        size_t eq = item.find('='), colon = item.rfind(':');
        if (eq == std::string::npos || colon == std::string::npos || colon < eq) continue;
        Watch w;
        w.name = item.substr(0, eq);
        w.expr = item.substr(eq + 1, colon - eq - 1);
        w.len = uint32_t(std::stoul(item.substr(colon + 1), nullptr, 16)) & ~3u;
        watches.push_back(w);
        log("watch: %s = %s, 0x%X bytes", w.name.c_str(), w.expr.c_str(), w.len);
    }
}

}  // namespace

void watch_tick(Cpu *c) {
    parse();
    for (auto &w : watches) {
        size_t i = 0;
        uint32_t base = eval(c, w.expr, i);
        std::vector<uint8_t> now(w.len);
        bool ok = (base & 0xFFE00000) == 0x80000000 && (base & 0x1FFFFF) + w.len <= PSX_RAM_SIZE;
        if (ok) memcpy(now.data(), psx_ram + (base & 0x1FFFFF), w.len);
        if (base != w.base) {
            log("watch %llu %s at %08X", (unsigned long long)hw::gpu.frames, w.name.c_str(), base);
            w.base = base;
            w.last = ok ? now : std::vector<uint8_t>();
            continue;
        }
        if (!ok) continue;
        int step = half ? 2 : 4;
        for (uint32_t o = 0; o < w.len; o += step) {
            if (!memcmp(&now[o], &w.last[o], step)) continue;
            if (half) {
                int16_t a, b;
                memcpy(&a, &w.last[o], 2);
                memcpy(&b, &now[o], 2);
                log("watch %llu %s+%X: %d -> %d", (unsigned long long)hw::gpu.frames, w.name.c_str(), o, a, b);
            } else {
                uint32_t a, b;
                memcpy(&a, &w.last[o], 4);
                memcpy(&b, &now[o], 4);
                log("watch %llu %s+%X: %08X -> %08X", (unsigned long long)hw::gpu.frames, w.name.c_str(), o, a, b);
            }
        }
        w.last = now;
    }
}

static bool trace_input() {
    static bool on = getenv("VALOR_TRACE_INPUT") != nullptr;
    return on && !shadow_active();
}

void trace_role(uint32_t port, uint32_t role, int src, int32_t value) {
    if (trace_input())
        log("role %u: %u %u %d %d", psx_r32(hw::cpu, 0x8009A5DC), port, role, src, value);
}

}  // namespace psx

using namespace psx;

// 0x8004AC58 apply_damage(victim, damage, attacker, part) (docs/re/combat.md). VALOR_TRACE_DAMAGE
// logs each call: who, how much, the victim's health before and after.
extern "C" void hook_apply_damage(Cpu *c, RecompFn body) {
    static bool trace = getenv("VALOR_TRACE_DAMAGE") != nullptr;
    if (!trace) return body(c);
    uint32_t victim = c->r[4], attacker = c->r[6];
    int32_t damage = int32_t(c->r[5]), part = int32_t(c->r[7]);
    int32_t before = int32_t(psx_r32(c, victim + 0xF4));
    uint32_t vtype = psx_r32(c, victim + 0x8), atype = psx_r32(c, attacker + 0x8);
    int16_t weapon = int16_t(psx_r16(c, attacker + 0x3CA));
    body(c);
    int32_t after = int32_t(psx_r32(c, victim + 0xF4));
    log("damage %llu: victim %08X type %X part %d, attacker %08X type %X weapon %d, damage %.3f, "
        "health %.3f -> %.3f%s", (unsigned long long)hw::gpu.frames, victim, vtype, part, attacker,
        atype, weapon, damage / 65536.0, before / 65536.0, after / 65536.0,
        (psx_r32(c, victim + 0xC) & 0x20000000) ? " dead" : "");
}

// 0x80045A78 bullet_spawn(owner, ...) -> bullet (docs/re/combat.md, Aiming). VALOR_TRACE_SHOTS
// logs each of the player's bullets: its yaw and pitch against the game's view (the soldier's
// weapon block +0x184 yaw, +0x178 pitch), in degrees, and whether the scope was up.
extern "C" void hook_bullet_spawn(Cpu *c, RecompFn body) {
    static bool trace = getenv("VALOR_TRACE_SHOTS") != nullptr;
    if (!trace) return body(c);
    uint32_t owner = c->r[4];
    body(c);
    uint32_t bullet = c->r[2];
    if (!bullet || !(psx_r32(c, owner + 0xC) & 1)) return;
    uint32_t inv = psx_r32(c, owner + 0x388);
    // 16.16 angles, 4096 per turn, wrapped to +-half a turn.
    auto deg = [](int32_t a) {
        double d = std::fmod(a / 65536.0 * 360.0 / 4096.0, 360.0);
        return d > 180 ? d - 360 : d < -180 ? d + 360 : d;
    };
    int32_t byaw = int32_t(psx_r32(c, bullet + 0x22C)), bpitch = int32_t(psx_r32(c, bullet + 0x228));
    int32_t vyaw = int32_t(psx_r32(c, inv + 0x184)), vpitch = int32_t(psx_r32(c, inv + 0x178));
    auto pos = [&](uint32_t a, int i) { return int32_t(psx_r32(c, a + 4 * i)) / 65536.0; };
    uint32_t cam = psx_r32(c, 0x8009D638);
    log("shot %llu: yaw %.2f pitch %.2f, view yaw %.2f pitch %.2f, off by yaw %.2f pitch %.2f%s; "
        "bullet %.1f %.1f %.1f, aim %.1f %.1f %.1f, camera %.1f %.1f %.1f",
        (unsigned long long)hw::gpu.frames, deg(byaw), deg(bpitch), deg(vyaw), deg(vpitch),
        deg(byaw - vyaw), deg(bpitch - vpitch), (psx_r32(c, inv) & 0x100) ? " scoped" : "",
        pos(bullet + 0x20C, 0), pos(bullet + 0x20C, 1), pos(bullet + 0x20C, 2), pos(inv + 0x1A4, 0),
        pos(inv + 0x1A4, 1), pos(inv + 0x1A4, 2), pos(cam + 0x20C, 0), pos(cam + 0x20C, 1),
        pos(cam + 0x20C, 2));
}

// 0x8005F69C texts_update, called once per tick after the entities' moves and collisions
// (docs/re/objects.md, The frame and the tick). VALOR_TRACE_STATE=FILE writes, after it, one JSON
// line per tick, every value exact as the game holds it (16.16 fixed point, 4096 angle units per
// turn as 16.16):
//   {"tick":g_tick, "vblank":N,
//    "player":[look yaw, look pitch, slot, weapon, clip, reserve, flags B],
//    "ents":[[handle, type, flags A, x, y, z, pitch, yaw, roll, health, animation, frame], ...]}
// "ents" follows the entity list (g_entities, 0x8009D64C) in order; animation and frame are -1
// without a model instance (docs/re/entities.md, animation.md).
extern "C" void trace_drawn(Cpu *c);

extern "C" void hook_texts_update(Cpu *c, RecompFn body) {
    body(c);
    if (!shadow_active()) trace_drawn(c);
    static FILE *out = [] {
        const char *path = getenv("VALOR_TRACE_STATE");
        FILE *f = path ? fopen(path, "w") : nullptr;
        if (path) log(f ? "trace: state to %s" : "trace: can't write %s", path);
        return f;
    }();
    if (!out) return;
    auto r32 = [&](uint32_t a) { return psx_r32(c, a); };
    auto s32 = [&](uint32_t a) { return int32_t(psx_r32(c, a)); };
    auto s16 = [&](uint32_t a) { return int16_t(psx_r16(c, a)); };
    auto in_ram = [](uint32_t a) { return (a & 0xFFE00003) == 0x80000000; };
    fprintf(out, "{\"tick\":%u,\"vblank\":%llu", r32(0x8009A5DC), (unsigned long long)hw::gpu.frames);
    uint32_t soldier = r32(0x8009D654);
    if (in_ram(soldier)) {
        uint32_t look = r32(soldier + 0x388), inv = soldier + 0x3CC;
        int slot = psx_r8(c, inv + 0x54);
        fprintf(out, ",\"player\":[%d,%d,%d,%d,%d,%d,%u]", in_ram(look) ? s32(look + 0x184) : 0,
                in_ram(look) ? s32(look + 0x178) : 0, slot, int(psx_r8(c, inv + 0x55)),
                s16(inv + 0x66 + 2 * slot), s16(inv + 0x56 + 2 * slot), r32(soldier + 0x10));
    }
    fputs(",\"ents\":[", out);
    int n = 0;
    for (uint32_t e = r32(0x8009D64C); in_ram(e) && n < 1024; e = r32(e + 4), n++) {
        uint32_t inst = r32(e + 0x100);
        bool anim = in_ram(inst);
        fprintf(out, "%s[%u,%d,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d]", n ? "," : "", r32(e), s32(e + 8), r32(e + 0xC),
                s32(e + 0x20C), s32(e + 0x210), s32(e + 0x214), s32(e + 0x228), s32(e + 0x22C), s32(e + 0x230),
                s32(e + 0xF4), anim ? s16(inst + 0x5C) : -1, anim ? s16(inst + 0x5E) : -1);
    }
    fputs("]}\n", out);
}

// VALOR_TRACE_DRAWN: after every tick, each entity's drawn flags (+0x298 drawn once, +0x29C drawn
// in the last frame: what the drawing pass decided, docs/re/animation.md) and out-of-sight counter
// (+0x290), for the entities that have any: "drawn VBLANK TICK: HANDLE:298:29C:290 ...".
extern "C" void trace_drawn(Cpu *c) {
    static bool on = getenv("VALOR_TRACE_DRAWN") != nullptr;
    if (!on) return;
    std::string s;
    int n = 0;
    for (uint32_t e = psx_r32(c, 0x8009D64C); (e & 0xFFE00003) == 0x80000000 && n < 1024; e = psx_r32(c, e + 4), n++) {
        uint32_t a = psx_r32(c, e + 0x298), b = psx_r32(c, e + 0x29C);
        if (!a && !b) continue;
        char buf[64];
        snprintf(buf, sizeof buf, " %X:%X:%X:%d", psx_r32(c, e), a, b, int32_t(psx_r32(c, e + 0x290)));
        s += buf;
    }
    log("drawn %llu %u:%s", (unsigned long long)hw::gpu.frames, psx_r32(c, 0x8009A5DC), s.c_str());
}

// 0x8001552C rand (docs/re/objects.md, Randomness). VALOR_TRACE_RAND logs every draw of the game
// itself (not of a decoupled drawing pass) as "rand VBLANK TICK: CALLER VALUE", the caller being
// the return address, so a reimplementation can take the draws of the systems it owns in order.
extern "C" void hook_rand(Cpu *c, RecompFn body) {
    static bool trace = getenv("VALOR_TRACE_RAND") != nullptr;
    if (!trace || shadow_active()) return body(c);
    uint32_t ra = c->r[31];
    body(c);
    log("rand %llu %u: %08X %u", (unsigned long long)hw::gpu.frames, psx_r32(c, 0x8009A5DC), ra, c->r[2]);
}

// VALOR_TRACE_INPUT: what a reimplementation takes as the player's input each tick, as the game
// sees it. 0x80075A30 role_button(port, role): "role TICK: PORT ROLE 0 VALUE" (with the stick's
// "... 1 VALUE" from role_axis, game.cpp, when the buttons give 0); 0x8007A6B0 player_update:
// "input VBLANK TICK: " and the pad's four words at 0x80093B7C (docs/re/movement.md).
extern "C" void hook_role_button(Cpu *c, RecompFn body) {
    uint32_t port = c->r[4], role = c->r[5];
    body(c);
    trace_role(port, role, 0, int32_t(c->r[2]));
}

extern "C" void hook_player_update(Cpu *c, RecompFn body) {
    if (trace_input())
        log("input %llu %u: %08X %08X %08X %08X", (unsigned long long)hw::gpu.frames,
            psx_r32(c, 0x8009A5DC), psx_r32(c, 0x80093B7C), psx_r32(c, 0x80093B80),
            psx_r32(c, 0x80093B84), psx_r32(c, 0x80093B88));
    body(c);
}
