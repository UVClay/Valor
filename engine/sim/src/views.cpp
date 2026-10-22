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

}  // namespace valor::sim::view
