// Reverse engineering traces.
//
// Memory: log what changes in chosen memory from one game tick to the next.
// VALOR_WATCH="name=EXPR:LEN;..." where EXPR is a hex address, [EXPR] (the word there), or
// EXPR+HEX / EXPR-HEX; e.g. "inv=[8009D654]+3CC:80". Each tick, every changed aligned word is
// logged as "watch VBLANK name+OFF: OLD -> NEW" (VALOR_WATCH_HALF=1: halfwords, signed decimal).
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gpu.h"
#include "hw.h"
#include "runtime.h"

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
