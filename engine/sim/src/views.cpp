// The accessor layer (views.h).
#include "valor/sim/views.h"

#include <algorithm>

namespace valor::sim::view {

uint32_t Entity::model_hash() const {
    U inst = instance_addr();
    if (!inst) return 0;
    U model = U(m->u32(inst + 8));
    return model ? uint32_t(m->u32(model)) : 0;
}

std::vector<std::array<int16_t, 4>> Instance::quaternions() const {
    std::vector<std::array<int16_t, 4>> out;
    if (!a) return out;
    U q = posed_addr() ? posed_addr() : saved_addr();
    if (!q) return out;
    for (int i = 0; i < bones(); i++)
        out.push_back({int16_t(m->s16(q + 8 * i)), int16_t(m->s16(q + 8 * i + 2)), int16_t(m->s16(q + 8 * i + 4)),
                       int16_t(m->s16(q + 8 * i + 6))});
    return out;
}

std::vector<std::array<int16_t, 3>> Instance::joints() const {
    std::vector<std::array<int16_t, 3>> out;
    if (!a || !joints_addr()) return out;
    U j = joints_addr();
    for (int i = 0; i < bones(); i++)
        out.push_back({int16_t(m->s16(j + 8 * i)), int16_t(m->s16(j + 8 * i + 2)), int16_t(m->s16(j + 8 * i + 4))});
    return out;
}

std::vector<Entity> entities(const Mem& m) {
    std::vector<Entity> out;
    for (U e : m.entities()) out.push_back({&m, e});
    return out;
}

Instance instance(const Entity& e) { return {e.m, e.instance_addr()}; }
Entity soldier(const Mem& m) { return {&m, U(m.u32(G_SOLDIER))}; }
Entity camera(const Mem& m) { return {&m, U(m.u32(G_PLAYER))}; }

std::array<double, 3> Camera::eye() const {
    if (death()) return {eye_x() / 65536.0, eye_y() / 65536.0, eye_z() / 65536.0};
    return {m->s32(e + 0x20C) / 65536.0, m->s32(e + 0x210) / 65536.0, m->s32(e + 0x214) / 65536.0};
}

Camera camera_view(const Mem& m) {
    U c = U(m.u32(G_PLAYER));
    return {&m, c, c ? U(m.u32(c + 0x388)) : 0};
}

Player player(const Mem& m) {
    U s = U(m.u32(G_SOLDIER));
    return {&m, s ? U(m.u32(s + 0x388)) : 0};
}

uint32_t tick(const Mem& m) { return uint32_t(m.u32(G_TICK)); }

std::array<int, 3> fade(const Mem& m) {
    std::array<int, 3> c{0, 0, 0};
    for (U e : m.entities()) {
        if (m.s32(e + 8) != 0x32 || (m.u32(e + 0xC) & 0x80000)) continue;
        U o = U(m.u32(e + 0x388));
        if (!(m.u32(o) & 1) || m.s32(o + 4) > 0) continue;
        U col = m.s32(o + 8) > 0 ? o + 0x18 : o + 0x24;
        for (int k = 0; k < 3; k++) c[size_t(k)] = std::max(c[size_t(k)], int(m.u8(col + k)));
    }
    return c;
}

std::vector<Voice> voices(const Mem& m) {
    std::vector<Voice> out;
    for (int v = 1; v < 24; v++) out.push_back({&m, U(0x8009115C + 0x34 * v), v});
    return out;
}

std::vector<Text> texts(const Mem& m) {
    std::vector<Text> out;
    for (I i = 0; i < std::max<I>(0, std::min<I>(m.s32(0x80099ED0), 0x32)); i++) {
        U a = 0x80099F4C + 0x20 * U(i);
        Text t;
        for (U s = U(m.u32(a)); s && t.text.size() < 64 && m.u8(s + U(t.text.size()));) t.text += char(m.u8(s + U(t.text.size())));
        t.x = m.s16(a + 4), t.y = m.s16(a + 6);
        for (U k = 0; k < 3; k++) t.rgb[k] = uint8_t(m.u8(a + 8 + k));
        t.additive = m.u8(a + 0xB) != 0;
        t.life = m.s32(a + 0xC);
        out.push_back(t);
    }
    return out;
}

std::array<int32_t, 4> hit_timers(const Mem& m) {
    std::array<int32_t, 4> out{};
    for (U k = 0; k < 4; k++) out[k] = m.s32(0x8009A590 + 4 * k);
    return out;
}

std::vector<Objective> objectives(const Mem& m) {
    // levels.md, Objectives: the count 0x80098620, done flags 0x800988DC, texts 0x80098904, ids
    // 0x8009892C (10 slots).
    std::vector<Objective> out;
    int n = int(m.s32(0x80098620));
    for (int i = 0; i < n && i < 10; i++)
        out.push_back({uint32_t(m.u32(0x8009892C + 4 * i)), m.u32(0x800988DC + 4 * i) != 0, string_at(m, U(m.u32(0x80098904 + 4 * i)))});
    return out;
}

bool music_started(const Mem& m) { return m.u32(0x8009CC74) != 0; }

std::string string_at(const Mem& m, U a) {
    std::string s;
    for (U i = 0; a && i < 256; i++) {
        int c = int(m.u8(a + i));
        if (!c) break;
        s.push_back(char(c));
    }
    return s;
}

}  // namespace valor::sim::view

namespace valor::sim::view {

namespace {
void sprite_texture(const Mem& m, U sys, Sprite& sp) {
    sp.tex_x = uint16_t(m.u16(sys + 0x18)), sp.tex_y = uint16_t(m.u16(sys + 0x1A));
    sp.clut_x = uint16_t(m.u16(sys + 0x1C)), sp.clut_y = uint16_t(m.u16(sys + 0x1E));
    sp.w = uint16_t(m.u16(sys + 0x20)), sp.h = uint16_t(m.u16(sys + 0x22));
    sp.additive = m.u16(sys + 0x26) != 0;
}
}  // namespace

std::vector<Sprite> sprites(const Mem& m) {
    std::vector<Sprite> out;
    for (const Entity& e : entities(m)) {
        if (e.removed() || e.type() != 0x31) continue;
        U think = U(m.u32(e.a + 0x36C)), o = e.object_addr();
        if (!o) continue;
        if (think == 0x80073B08) {
            // particles_spawn: systems at +0xC (sparks) and +0x38 (smoke), 0x34-byte particles,
            // drawn by 0x800732F8 while the system's +0 flag is clear
            for (U sys : {o + 0xC, o + 0x38}) {
                U arr = U(m.u32(sys + 0x28));
                int n = int(m.s32(sys + 4));
                double scale = m.s32(sys + 8) / 65536.0;
                for (int i = 0; arr && i < n && i < 64; i++) {
                    U q = arr + 0x34 * U(i);
                    if (!(m.u8(q) & 1) || m.u8(q + 1)) continue;  // live, its delay over
                    Sprite sp;
                    sprite_texture(m, sys, sp);
                    for (int k = 0; k < 3; k++) sp.pos[size_t(k)] = m.s32(q + 4 + 4 * k) / 65536.0;
                    sp.half_w = m.s16(q + 0x30) * scale;
                    sp.half_h = m.s16(q + 0x32) * scale;
                    for (int k = 0; k < 3; k++) sp.rgb[k] = uint8_t(m.u8(q + 0x2C + k));
                    for (int k = 0; k < 3; k++) sp.rot[k] = uint16_t(m.u16(q + 0x1C + 2 * k));
                    sp.triangle = m.u8(q + 3) != 0;
                    out.push_back(sp);
                }
            }
        } else if (think == 0x800715D4) {
            // explosion_spawn: fireballs at +0xC, puffs at +0x38, 0x3C-byte records (0x80070F24)
            for (U sys : {o + 0xC, o + 0x38}) {
                U arr = U(m.u32(sys + 0x28));
                int n = int(m.s16(sys + 4));
                double scale = (m.s32(sys + 0xC) >> 4) / 4096.0;
                for (int i = 0; arr && i < n && i < 64; i++) {
                    U r = arr + 0x3C * U(i);
                    int st = int(m.u8(r));
                    if (st == 6 || m.s16(r + 6) > 0) continue;
                    Sprite sp;
                    sprite_texture(m, sys, sp);
                    sp.billboard = true;
                    for (int k = 0; k < 3; k++) sp.pos[size_t(k)] = m.s32(r + 0x24 + 4 * k) / 65536.0;
                    sp.half_w = 2.0 * (m.u16(r + 0xC) >> 1) * scale;
                    sp.half_h = 2.0 * (m.u16(r + 0xE) >> 1) * scale;
                    sp.rot[0] = uint16_t(m.u16(r + 0xA));
                    // the colour is the size's whole part (r +0x22): brighter as it grows; shrinking
                    // fireballs go red (size, size - 10, size - 15, none below 0)
                    int c = int(m.s16(r + 0x22));
                    int g = c, b = c;
                    if (st == 5) g = c - 10, b = c - 15;
                    sp.rgb[0] = uint8_t(c & 0xFF);
                    sp.rgb[1] = uint8_t((g & 0xFF) >= 0x81 ? 0 : g);
                    sp.rgb[2] = uint8_t((b & 0xFF) >= 0x81 ? 0 : b);
                    out.push_back(sp);
                }
            }
        }
    }
    return out;
}

}  // namespace valor::sim::view

namespace valor::sim::view {

namespace {
std::array<double, 3> whole(const Mem& m, U a) { return {m.s32(a) / 65536.0, m.s32(a + 4) / 65536.0, m.s32(a + 8) / 65536.0}; }
}  // namespace

std::vector<Streak> streaks(const Mem& m) {
    std::vector<Streak> out;
    for (const Entity& e : entities(m)) {
        if (e.removed() || e.type() != 0x23) continue;
        U think = U(m.u32(e.a + 0x36C)), o = e.object_addr();
        if (!o) continue;
        if (think == 0x800701DC) {
            // impact_effect_spawn: count o+0, header o+2.. (w, h, VRAM x, y, CLUT x, y, mode),
            // particles of 0x24 from o+0x10: +4 p0, +0x10 p1 (16.16), +0x1C c0, +0x20 c1
            int n = int(m.u8(o));
            for (int i = 0; i < n && i < 16; i++) {
                U q = o + 0x10 + 0x24 * U(i);
                Streak s;
                s.p0 = whole(m, q + 4), s.p1 = whole(m, q + 0x10);
                for (int k = 0; k < 3; k++) s.c0[k] = uint8_t(m.u8(q + 0x1C + k)), s.c1[k] = uint8_t(m.u8(q + 0x20 + k));
                s.w = uint16_t(m.u16(o + 2)), s.h = uint16_t(m.u16(o + 4));
                s.tex_x = uint16_t(m.u16(o + 6)), s.tex_y = uint16_t(m.u16(o + 8));
                s.clut_x = uint16_t(m.u16(o + 0xA)), s.clut_y = uint16_t(m.u16(o + 0xC));
                s.mode = int(m.u16(o + 0xE));
                out.push_back(s);
            }
        } else if (think == 0x800702A4) {
            // the burst (0x800708F0): count o+0, particles of 0x24 at [o+0x2C], +0 kind 1 or 2; the
            // two kinds' templates as 0x800708F0 sets them (+0xC.., +0x1C..; constants: the engine
            // doesn't keep them)
            int n = int(m.s32(o));
            U arr = U(m.u32(o + 0x2C));
            for (int i = 0; arr && i < n && i < 16; i++) {
                U q = arr + 0x24 * U(i);
                int kind = int(m.u32(q) & 3);
                if (!kind) continue;
                Streak s;
                s.quad = true;
                s.p0 = whole(m, q + 4), s.p1 = whole(m, q + 0x10);
                for (int k = 0; k < 3; k++) s.c0[k] = uint8_t(m.u8(q + 0x1C + k)), s.c1[k] = uint8_t(m.u8(q + 0x20 + k));
                s.w = s.h = 0x1F;
                s.tex_x = 0x3F0, s.tex_y = kind == 1 ? 0x20 : 0x40;
                s.clut_x = 0x3F0, s.clut_y = kind == 1 ? 0xFC : 0xFD;
                s.mode = 2;
                out.push_back(s);
            }
        }
    }
    return out;
}

std::vector<Puff> puffs(const Mem& m) {
    std::vector<Puff> out;
    for (const Entity& e : entities(m)) {
        if (e.removed() || (e.type() != 0x1402 && e.type() != 0x1405)) continue;
        U o = e.object_addr();
        if (!o) continue;
        for (int i = 0; i < 5; i++) {
            U p = o + 0x3E8 + 0x24 * U(i);
            if (!(m.u32(p) & 1)) continue;
            Puff f;
            f.pos = whole(m, p + 0x10);
            f.scale = (m.s32(p + 0x1C) >> 4) / 4096.0;
            f.angle = uint16_t(m.u32(p + 8));
            for (int k = 0; k < 3; k++) f.rgb[k] = uint8_t(m.u8(p + 0x20 + k));
            out.push_back(f);
        }
        for (int i = 0; i < 15; i++) {
            U p = o + 0x49C + 0x74 * U(i);
            if (!(m.u32(p) & 1)) continue;
            Puff f;
            f.drop = true;
            f.on_floor = m.u32(p) & 8;
            f.pos = whole(m, p + 0x34);
            f.scale = 1.0;
            f.angle = uint16_t(m.u32(p + 8));
            for (int k = 0; k < 3; k++) f.rgb[k] = uint8_t(m.u8(p + 0x4C + k));
            out.push_back(f);
        }
    }
    return out;
}

std::vector<Shot> enemy_shots(const Mem& m) {
    std::vector<Shot> out;
    for (const Entity& e : entities(m))
        if (!e.removed() && e.type() == 0x13F4) out.push_back({e.handle(), whole(m, e.a + 0x258)});
    return out;
}

ExplosionLight explosion_light(const Mem& m) {
    ExplosionLight l;
    U src = U(m.u32(0x8009D650));  // g_last_explosion
    if (!src) return l;
    l.on = true;
    l.pos = {double(m.s16(src + 0x20E)), double(m.s16(src + 0x212)), double(m.s16(src + 0x216))};
    for (int k = 0; k < 3; k++) l.rgb[k] = uint8_t(m.u8(0x800AA188 + k));
    l.radius = int32_t(m.u16(0x800AA18C));
    l.radius2 = int32_t(m.u32(0x800AA190));
    l.scorch = m.u32(0x800AA160) != 0;
    return l;
}

}  // namespace valor::sim::view
