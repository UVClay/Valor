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
//    "ents":[[handle, type, flags A, x, y, z, pitch, yaw, roll, health, animation, frame], ...],
//    "texts":[[string, x, y, r, g, b, life, hold], ...], "hud":[hit wedge timers r, l, front, behind]}
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
    // the text list (0x80099F4C, count 0x80099ED0): [string, x, y, r, g, b, life, hold], and the
    // hit wedge timers (0x8009A590: right, left, front, behind) (docs/re/hud.md)
    fputs("],\"texts\":[", out);
    int nt = s32(0x80099ED0);
    for (int i = 0; i < nt && i < 50; i++) {
        uint32_t a = 0x80099F4C + 32 * i, str = r32(a);
        fputs(i ? ",[\"" : "[\"", out);
        for (int k = 0; in_ram(str & ~3u) && k < 64; k++) {
            char ch = char(psx_r8(c, str + k));
            if (!ch) break;
            if (ch == '"' || ch == '\\') fputc('\\', out);
            fputc(ch >= 32 && ch < 127 ? ch : '?', out);
        }
        fprintf(out, "\",%d,%d,%u,%u,%u,%d,%d]", s16(a + 4), s16(a + 6), psx_r8(c, a + 8), psx_r8(c, a + 9),
                psx_r8(c, a + 10), s32(a + 12), s32(a + 16));
    }
    fprintf(out, "],\"hud\":[%d,%d,%d,%d]}\n", s32(0x8009A590), s32(0x8009A594), s32(0x8009A598), s32(0x8009A59C));
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
// "input VBLANK TICK: " and the pad's four words at 0x80093B7C (now and before, per port), then
// the stick bytes 0x80093B70 (per port: rx, ry, lx, ly, as role_axis_curve reads them) and the
// analog-mode bytes 0x80093B78 (docs/re/movement.md). Then "| " and the soldier as the update
// finds it, which is how the last tick left it: position +0x20C, where entity_move left it
// +0x3BC, the push +0x1F0, the eye height +0x2D4, contact +0x38, flags B +0x10, and inv's flags,
// slot and weapon (+0x54), flinch (+0x154) and knockback (+0x80), in hex.
extern "C" void hook_role_button(Cpu *c, RecompFn body) {
    uint32_t port = c->r[4], role = c->r[5];
    body(c);
    trace_role(port, role, 0, int32_t(c->r[2]));
}

// VALOR_TRACE_AIM: what aim_update (0x8007E034, docs/re/combat.md) reads besides the player and
// the view, and what it left the tick before, as player_update finds them: "aim TICK: STATE TIMER
// ARMS NEAR FAR | OUT | ENTITIES". STATE is the soldier's +0x11E (the scripts' animation state,
// which sets inv 0x800000) and TIMER inv+0x48 (the weapon's); ARMS is the first-person model's hash; NEAR and FAR its bones +0xD4 and
// +0xD6 (index, joint 3 x s16 from the instance's +0x94, quaternion 4 x s16 from +0x88 or +0x8C);
// OUT the last aim's results: the aim point inv+0x1A4, the shot start inv+0x198 (3 x s32 each), the
// angular velocity +0x234/+0x238, inv+0x180/+0x18C, then the view (the camera object's +0x100..+0x11F)
// and the soldier's +0x340..+0x353 and +0x354..+0x35F; ENTITIES every entity's handle, flags A and B,
// position (3 x s32), sphere centre (3 x s16, +0x2E0) and radius (+0x2E8), +0x2EE, and for those
// with flags A & 0x10400 its hit parts: count, then centre (3 x s32), radius squared and kind each.
// All in hex.
static void trace_aim(Cpu *c, uint32_t e) {
    static bool on = getenv("VALOR_TRACE_AIM") != nullptr;
    if (!on || shadow_active()) return;
    uint32_t inv = psx_r32(c, e + 0x388), inst = psx_r32(c, e + 0x100);
    std::string s;
    auto add = [&](uint32_t v) {
        char buf[12];
        snprintf(buf, sizeof buf, " %X", v);
        s += buf;
    };
    uint32_t model = inst ? psx_r32(c, inst + 8) : 0;
    add(psx_r16(c, e + 0x11E)), add(psx_r32(c, inv + 0x48));
    add(model ? psx_r32(c, model) : 0);
    for (uint32_t bo : {0xD4u, 0xD6u}) {
        uint32_t bone = model ? psx_r16(c, model + bo) : 0;
        add(bone);
        uint32_t jp = inst ? psx_r32(c, inst + 0x94) : 0;
        uint32_t qp = inst ? (psx_r32(c, inst + 0x88) ? psx_r32(c, inst + 0x88) : psx_r32(c, inst + 0x8C)) : 0;
        for (int k = 0; k < 3; k++) add(jp ? psx_r16(c, jp + 8 * bone + 2 * k) : 0);
        for (int k = 0; k < 4; k++) add(qp ? psx_r16(c, qp + 8 * bone + 2 * k) : 0);
    }
    s += " |";
    for (uint32_t o : {0x1A4u, 0x1A8u, 0x1ACu, 0x198u, 0x19Cu, 0x1A0u}) add(psx_r32(c, inv + o));
    add(psx_r32(c, e + 0x234)), add(psx_r32(c, e + 0x238));
    add(psx_r32(c, inv + 0x180)), add(psx_r32(c, inv + 0x18C));
    // The view (camera object +0x100: matrix and translation, set by the camera's update this
    // tick) and the soldier's matrix +0x340 and whole position +0x354 (the last tick's).
    uint32_t co = psx_r32(c, psx_r32(c, 0x8009D638) + 0x388);
    for (uint32_t o = 0; o < 0x20; o += 4) add(psx_r32(c, co + 0x100 + o));
    for (uint32_t o = 0; o < 0x14; o += 4) add(psx_r32(c, e + 0x340 + o));
    for (uint32_t o = 0; o < 0xC; o += 4) add(psx_r32(c, e + 0x354 + o));
    s += " |";
    int n = 0;
    for (uint32_t x = psx_r32(c, 0x8009D64C); (x & 0xFFE00003) == 0x80000000 && n < 512; x = psx_r32(c, x + 4), n++) {
        uint32_t fa = psx_r32(c, x + 0xC);
        add(psx_r32(c, x)), add(fa), add(psx_r32(c, x + 0x10));
        for (int k = 0; k < 3; k++) add(psx_r32(c, x + 0x20C + 4 * k));
        for (int k = 0; k < 3; k++) add(psx_r16(c, x + 0x2E0 + 2 * k));
        add(psx_r16(c, x + 0x2E8)), add(psx_r16(c, x + 0x2EE));
        uint32_t np = (fa & 0x10400) ? psx_r16(c, x + 0x36) : 0, pp = psx_r32(c, x + 0x304);
        if (!pp) np = 0;
        add(np);
        for (uint32_t i = 0; i < np && i < 64; i++) {
            for (int k = 0; k < 3; k++) add(psx_r32(c, pp + 0x48 * i + 0x20 + 4 * k));
            add(psx_r32(c, pp + 0x48 * i + 0x38)), add(psx_r32(c, pp + 0x48 * i + 8));
        }
    }
    log("aim %u:%s", psx_r32(c, 0x8009A5DC), s.c_str());
}

extern "C" void hook_player_update(Cpu *c, RecompFn body) {
    trace_aim(c, c->r[4]);
    if (trace_input()) {
        uint32_t e = c->r[4], inv = psx_r32(c, e + 0x388);
        std::string st;
        for (uint32_t o : {0x20Cu, 0x210u, 0x214u, 0x3BCu, 0x3C0u, 0x3C4u, 0x1F0u, 0x1F4u, 0x1F8u, 0x2D4u, 0x38u, 0x10u}) {
            char buf[12];
            snprintf(buf, sizeof buf, " %X", psx_r32(c, e + o));
            st += buf;
        }
        for (uint32_t o : {0x0u, 0x54u, 0x154u, 0x80u, 0x84u, 0x88u}) {
            char buf[12];
            snprintf(buf, sizeof buf, " %X", psx_r32(c, inv + o));
            st += buf;
        }
        log("input %llu %u: %08X %08X %08X %08X %08X %08X %08X |%s", (unsigned long long)hw::gpu.frames,
            psx_r32(c, 0x8009A5DC), psx_r32(c, 0x80093B7C), psx_r32(c, 0x80093B80),
            psx_r32(c, 0x80093B84), psx_r32(c, 0x80093B88), psx_r32(c, 0x80093B70),
            psx_r32(c, 0x80093B74), psx_r32(c, 0x80093B78), st.c_str());
    }
    body(c);
}
