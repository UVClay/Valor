#include "valor/sim/audio.h"

#include "valor/sim/engine.h"

namespace valor::sim {

namespace {
constexpr U G_VOICES = 0x8009115C;
constexpr U G_ENABLED = 0x800910D0;
constexpr U G_BANK_SPU = 0x800910E8;  // per bank slot: SPU address (+0x40: loaded flag)
constexpr U G_PITCH = 0x8009163C;     // base pitch per sound and slot
}  // namespace

void Audio::adopt(I vb) {
    Mem& m = eng_.m;
    for (int v = 1; v < 24; v++) {
        if (m.u8(G_ENABLED + v) & 1) {
            U a = G_VOICES + 0x34 * v;
            I bank = m.u16(a + 0xC), sound = m.s32(a + 8);
            I tbl = bank < 8 ? m.u32(G_BANK_SPU + 4 * bank + 0x20) : 0;
            I size = (tbl && sound >= 0) ? m.u16(tbl + 2 * sound + 2) : 0;
            I p = bank < 8 ? S32(m.u16(G_PITCH + 2 * bank + 0x10 * sound) * m.s32(a + 0x20)) >> 16 : 0x800;
            double dur = double(size * 8 / 16 * 28) * 60.0 / (44100.0 * double(imax(p, 1)) / 4096.0);
            double st = double(vb - 2 * m.u16(a + 0x28));
            spu_[v] = {Voice::ON, st, st + dur};
        }
    }
}

I Audio::play(I bank, I sound, U ent, I volume, I vspread, I pitch, I pspread, I fade, I loop, I cls, I flags) {
    Mem& m = eng_.m;
    bank &= 0xFFFF;
    if (bank >= 8 || sound < 0 || !m.u32(G_BANK_SPU + 4 * S16(bank) + 0x40)) return -1;
    int v = alloc();
    if (v < 0) {
        eng_.note("sound dropped: no free voice");
        return -1;
    }
    U a = G_VOICES + 0x34 * v;
    m.w16(a + 0xC, bank);
    m.w32(a + 8, sound);
    I vol = (volume << 7) <= 0xFF0000 ? imin(volume << 7, 0xFF0000) : 0xFF0000;
    m.w32(a + 0x14, vol);
    m.w32(a + 0x18, volume);
    m.w32(a + 0x1C, vspread);
    m.w32(a + 0x20, pitch);
    m.w8(a + 0xE, loop);
    m.w16(a + 0x28, 0);
    m.w32(a + 0x24, pspread);
    m.w8(a + 0x13, cls);
    if (vspread) {
        I lo = S32(vol * vspread);
        I r = eng_.rand.draw(FN("sound"));
        m.w32(a + 0x14, S32(vol + (lo - (lo ? pmod(r, S32(lo * 2)) : 0))));
    }
    m.w8(a + 0xF, fade);
    m.w8(a + 0x12, 0);
    m.w8(a + 0x10, 0);
    m.w32(a + 4, ent ? m.u32(ent) : 0);
    m.w32(a, flags);
    if (ent && !(m.u32(ent + 0xC) & 0x40000000)) {
        for (int k = 0; k < 3; k++) m.w16(a + 0x2C + 2 * k, m.s16(ent + 0x20E + 4 * k));
    } else {
        for (int k = 0; k < 3; k++) m.w16(a + 0x2C + 2 * k, 0);
        m.w32(a, m.u32(a) | 1);
    }
    start(bank, sound, v, pitch, pspread);
    return v;
}

int Audio::alloc() {
    Mem& m = eng_.m;
    for (int v = 1; v < 24; v++) {
        I en = m.u8(G_ENABLED + v);
        if (m.u8(G_VOICES + 0x34 * v + 0xE) == 0 && (en == 0 || en == 3)) return v;
    }
    return -1;
}

void Audio::start(I bank, I sound, int v, I pitch, I pspread) {
    Mem& m = eng_.m;
    I tbl = m.u32(G_BANK_SPU + 4 * bank + 0x20);
    I size = tbl ? m.u16(tbl + 2 * sound + 2) : 0;
    if (size < 0xD) return;
    I p = S32(m.u16(G_PITCH + 2 * bank + 0x10 * sound) * pitch) >> 16;
    I lo = S32(p * pspread);
    I sp = lo >> 15;
    if (sp) p += (lo >> 16) - pmod(eng_.rand.draw(FN("sound")), sp);
    p &= 0xFFFF;
    keyon_ |= I(1) << v;
    m.w8(G_ENABLED + v, 1);
    I samples = size * 8 / 16 * 28;
    double dur = double(samples) * 60.0 / (44100.0 * double(imax(p, 1)) / 4096.0);
    spu_[v] = {Voice::PENDING, 0, dur};
}

void Audio::stop(int v) {
    spu_[v] = {Voice::OFF, 0, 0};
    eng_.m.zero(G_VOICES + 0x34 * v, 0x34);
}

void Audio::update() {
    Mem& m = eng_.m;
    double vb = double(eng_.vblank);
    // SpuGetAllKeysStatus: as the recording's SPU reported it (real time), else modelled
    const std::array<uint8_t, 24>* rec = eng_.spu_keys();
    for (int v = 1; v < 24; v++) {
        if (rec) {
            m.w8(G_ENABLED + v, (*rec)[size_t(v)]);
            continue;
        }
        auto it = spu_.find(v);
        if (it == spu_.end()) continue;
        Voice& st = it->second;
        I status;
        if (st.st == Voice::ON) {
            status = vb < st.end ? 1 : 3;
            if (status == 3) st = {Voice::REL, 0, 0};
        } else if (st.st == Voice::REL) {
            status = 0;
            st = {Voice::OFF, 0, 0};
        } else if (st.st == Voice::PENDING)
            status = m.u8(G_ENABLED + v);
        else
            status = 0;
        m.w8(G_ENABLED + v, status);
    }
    if (keyon_) {
        for (int v = 0; v < 24; v++) {
            if (keyon_ >> v & 1) {
                auto it = spu_.find(v);
                double dur = (it != spu_.end() && it->second.st == Voice::PENDING) ? it->second.end : 0;
                spu_[v] = {Voice::ON, vb, vb + dur};
                m.w8(G_ENABLED + v, 1);
            }
        }
        keyon_ = 0;
    }
    for (int v = 1; v < 24; v++) {
        U a = G_VOICES + 0x34 * v;
        I en = m.u8(G_ENABLED + v);
        if (en & 1) {
            I age = (m.u16(a + 0x28) + 1) & 0xFFFF;
            m.w16(a + 0x28, age);
            if (age >= 0x15F && m.u8(a + 0xE) == 0) {
                stop(v);
                continue;
            }
            I h = m.u32(a + 4);
            if (!eng_.handle_dead(h)) {
                U o = eng_.resolve(h);
                if (!(m.u32(o + 0xC) & 0x80000)) {
                    for (int k = 0; k < 3; k++) m.w16(a + 0x2C + 2 * k, m.s16(o + 0x20E + 4 * k));
                } else
                    m.w32(a + 4, 0);
            }
            if (en == 3 && m.u8(a + 0x12) == 0) continue;
            if (m.u8(a + 0x12)) {
                I n = m.u8(a + 0x12) - 1;
                m.w8(a + 0x12, n);
                if (n == 0) release(v);
            }
            continue;
        }
        if (m.u8(a + 0xE)) {
            m.w8(a + 0xE, 0);
            I h = m.u32(a + 4);
            restart(m.s16(a + 0xC), m.s32(a + 8), eng_.resolve(h), m.s32(a + 0x18), m.s32(a + 0x1C), m.s32(a + 0x20), m.s32(a + 0x24), 1, v, 0);
        } else {
            m.w32(a + 4, 0);
            m.w8(a + 0x12, 0);
            m.w16(a + 0x28, 0);
        }
    }
}

// sound_restart (0x80042250): a looping voice that ended is started again in place: the volume
// (with its spread, a draw by sound_restart), loop, age 0, class, the position from the entity
// (none or not positional: 0 and flag 1), voice_start.
I Audio::restart(I bank, I sound, U ent, I volume, I vspread, I pitch, I pspread, I loop, int v, I cls) {
    Mem& m = eng_.m;
    if ((bank & 0xFFFF) >= 8 || sound < 0 || !m.u32(G_BANK_SPU + 4 * S16(bank) + 0x40)) return -1;
    U a = G_VOICES + 0x34 * U(v);
    I vol = (volume << 7) <= 0xFF0000 ? imin(volume << 7, 0xFF0000) : 0xFF0000;
    m.w32(a + 0x14, vol);
    m.w8(a + 0xE, loop);
    m.w16(a + 0x28, 0);
    m.w8(a + 0x13, cls);
    if (vspread) {
        I lo = S32(vol * vspread);
        I r = eng_.rand.draw({0x80042250});
        m.w32(a + 0x14, S32(vol + (lo - (lo ? pmod(r, S32(lo * 2)) : 0))));
    }
    m.w8(a + 0x12, 0);
    m.w8(a + 0x10, 0);
    if (ent && !(m.u32(ent + 0xC) & 0x40000000)) {
        for (U k = 0; k < 3; k++) m.w16(a + 0x2C + 2 * k, m.s16(ent + 0x20E + 4 * k));
    } else {
        for (U k = 0; k < 3; k++) m.w16(a + 0x2C + 2 * k, 0);
        m.w32(a, m.u32(a) | 1);
    }
    start(bank & 0xFFFF, sound, v, pitch, pspread);
    return v;
}

void Audio::release(int v) {
    auto it = spu_.find(v);
    if (it != spu_.end() && it->second.st == Voice::ON) it->second = {Voice::REL, 0, 0};
}

void Audio::fade_entity(U e, I frames) {
    Mem& m = eng_.m;
    I h = m.u32(e);
    for (int v = 1; v < 24; v++) {
        U a = G_VOICES + 0x34 * v;
        if (m.u32(a + 4) == h && (m.u8(G_ENABLED + v) & 1)) {
            m.w8(a + 0x11, frames);
            m.w8(a + 0x12, frames);
        }
    }
}

}  // namespace valor::sim
