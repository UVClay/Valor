// The accessor layer (views.h).
#include "valor/sim/views.h"

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

std::vector<Voice> voices(const Mem& m) {
    std::vector<Voice> out;
    for (int v = 1; v < 24; v++) out.push_back({&m, U(0x8009115C + 0x34 * v), v});
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
            // particles_spawn: systems at +0xC (sparks) and +0x38 (smoke), 0x34-byte particles
            for (U sys : {o + 0xC, o + 0x38}) {
                U arr = U(m.u32(sys + 0x28));
                int n = int(m.s32(sys + 4));
                double scale = m.s32(sys + 8) / 65536.0;
                for (int i = 0; arr && i < n && i < 64; i++) {
                    U q = arr + 0x34 * U(i);
                    if (m.u8(q) != 1 || m.u8(q + 1)) continue;  // live, its delay over
                    Sprite sp;
                    sprite_texture(m, sys, sp);
                    for (int k = 0; k < 3; k++) sp.pos[size_t(k)] = m.s32(q + 4 + 4 * k) / 65536.0;
                    sp.size = m.s16(q + 0x30) * 2.0 * scale;
                    for (int k = 0; k < 3; k++) sp.rgb[k] = uint8_t(m.u8(q + 0x2C + k));
                    sp.rot = uint16_t(m.u16(q + 0x1C));
                    out.push_back(sp);
                }
            }
        } else if (think == 0x800715D4) {
            // explosion_spawn: fireballs at +0xC, puffs at +0x38, 0x3C-byte records
            for (U sys : {o + 0xC, o + 0x38}) {
                U arr = U(m.u32(sys + 0x28));
                int n = int(m.s16(sys + 4));
                double scale = m.s32(sys + 0xC) / 65536.0;
                for (int i = 0; arr && i < n && i < 64; i++) {
                    U r = arr + 0x3C * U(i);
                    int st = int(m.u8(r));
                    if (st < 3 || st > 5 || m.s16(r + 6)) continue;
                    Sprite sp;
                    sprite_texture(m, sys, sp);
                    sp.fireball = sys == o + 0xC;
                    for (int k = 0; k < 3; k++) sp.pos[size_t(k)] = m.s32(r + 0x24 + 4 * k) / 65536.0;
                    sp.size = m.s32(r + 0x20) / 65536.0 * 2.0 * scale;
                    sp.rot = uint16_t(m.u16(r + 0xA));
                    out.push_back(sp);
                }
            }
        }
    }
    return out;
}

}  // namespace valor::sim::view
