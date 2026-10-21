// The player's soldier (docs/re/movement.md, player.md, combat.md Aiming): tools/sim/engine_player.py.
// The move is the core's entity_move (movement.cpp, move_sim.py's rules).
#include "valor/movement.h"
#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/scripts.h"

namespace valor::sim {

namespace {
enum { FIRE, JUMP, SWITCH, CROUCH, RELOAD, USE, A6, CENTER, AIM };
constexpr U G_WEAPONS = 0x80093640;
constexpr U G_SLOT_WEAPON = 0x8009E878;
constexpr U G_SLOT_COUNT = 0x8009E874;
constexpr U G_CHEATS = 0x800386A4;
constexpr U G_SUBDIV = 0x800AA168;
constexpr U ANALOG = 0x80093B78;
constexpr U ROLE_BUTTONS = 0x800B75F0;

I weapon_flags(Mem& m, I w) { return m.u16(G_WEAPONS + 0x44 * w + 0x2A); }
I get_var(Mem& m, U e, I v) { return m.s32(e + 0x4C + 4 * v); }
void set_var(Mem& m, U e, I v, I x) { m.w32(e + 0x4C + 4 * v, x); }

I player_stance(Mem& m, U inv) {
    I w = m.u8(inv + 0x55);
    if (m.u32(inv) & 0x20) {
        switch (w) {
            case 0: case 4: case 8: case 9: case 12: return 0;
            case 1: case 6: case 11: return 1;
            case 5: return 2;
            case 2: return 5;
            case 3: case 7: case 10: return 6;
            default: return 7;
        }
    }
    switch (w) {
        case 1: case 6: case 11: return 8;
        case 2: case 3: case 7: case 10: return 9;
        default: return 7;
    }
}

void crouch_target(Mem& m, U inv) {
    if (m.s32(0x800939F4) == 5 && m.s32(0x800939F8) == 4) {
        m.w32(inv + 0x3C, -0x96);
        return;
    }
    m.w32(inv + 0x3C, -0x50);
    if (m.s32(G_SUBDIV) < 0x12C) m.w32(G_SUBDIV, 0x12C);
    if (m.s32(0x8009E830) == 0) {
        if (m.s32(0x800AA164) < 2) m.w32(0x800AA164, 2);
        m.w32(0x8009E830, 1);
    }
}

void stand_target(Mem& m, U inv) {
    if (m.s32(G_SUBDIV) >= 0xFB) m.w32(G_SUBDIV, 0xFA);
    if (m.s32(0x8009E830)) {
        if (m.s32(0x800AA164) >= 2) m.w32(0x800AA164, 1);
        m.w32(0x8009E830, 0);
    }
    m.w32(inv + 0x3C, -0xF0);
}

void player_stand(Engine& eng, U e, U inv) {
    Mem& m = eng.m;
    m.w32(inv, m.u32(inv) & ~I(2) & ~I(0x10));
    m.w32(e + 0x2D4, m.s32(inv + 0x34));
    m.w16(e + 0x2DA, -0x64);
    if (m.u32(inv) & 2)
        crouch_target(m, inv);
    else
        stand_target(m, inv);
}

void player_crouch_done(Engine& eng, U e, U inv) {
    Mem& m = eng.m;
    m.w32(inv, (m.u32(inv) | 2) & ~I(0x10));
    if (eng.handle_dead(m.u32(e + 0x2C))) {
        m.w32(e + 0x2D4, 0x500000);
        m.w16(e + 0x2DA, 0);
    }
    if (m.u32(inv) & 2)
        crouch_target(m, inv);
    else
        stand_target(m, inv);
}

void crouch(Engine& eng, U e, U inv) {
    Mem& m = eng.m;
    U co = U(m.u32(m.u32(G_PLAYER) + 0x388));
    if (eng.action_pressed(CROUCH)) {
        I f = m.u32(inv);
        if (!(f & 2)) {
            m.w32(inv, f | 2);
            crouch_target(m, inv);
        } else if (!(m.u32(e + 0x10) & 0x800))
            player_stand(eng, e, inv);
    }
    I cur = m.s32(co + 0x94), tgt = m.s32(inv + 0x3C);
    if (cur != tgt) {
        I step;
        if (tgt + 0x12 < cur)
            step = -0x14;
        else {
            step = 0x14;
            if (cur >= tgt - 0x12) {
                if ((m.u32(inv) & 2) && (m.s32(e + 0x200) || m.s32(e + 0x218) || m.s32(e + 0x238)))
                    step = cur - tgt;
                else
                    step = tgt - cur;
            }
        }
        cur = S32(cur + step);
        m.w32(co + 0x94, cur);
        m.w32(co + 0x98, S32(m.s32(co + 0x98) + step));
        m.w32(co + 0x4C, S32(cur << 16));
        m.w32(co + 0x58, S32(cur << 16));
        m.w32(co + 0x1C, S32(m.s32(co + 0x1C) + (step << 16)));
        m.w32(co + 0x28, S32(m.s32(co + 0x28) + (step << 16)));
        if (cur == tgt && (m.u32(inv) & 2)) player_crouch_done(eng, e, inv);
    } else if ((m.u32(inv) & 2) && eng.handle_dead(m.u32(e + 0x2C))) {
        m.w32(e + 0x2D4, 0x500000);
        m.w16(e + 0x2DA, 0);
    }
}

I next_slot(Mem& m, U inv) {
    I cur = m.u8(inv + 0x54);
    I n = m.s32(G_SLOT_COUNT);
    I i = cur + 1 < n ? cur + 1 : 0;
    while (i != cur) {
        if (m.s16(inv + 0x56 + 2 * i) != -1) return i;
        i = i + 1 < n ? i + 1 : 0;
    }
    return -1;
}

void weapon_holster(Engine& eng, U e, U inv) {
    Mem& m = eng.m;
    if (next_slot(m, inv) == -1) return;
    m.w32(inv, m.u32(inv) | 0x800);
    U w = U(G_WEAPONS + 0x44 * m.u8(inv + 0x55));
    I snd = m.s16(w + 0x1C);
    if (snd >= 0) {
        I bank = m.s16(w + 0x1E);
        if (bank < 0) bank = m.u16(m.u32(m.u32(e + 0x100) + 8) + 0xC8);
        eng.audio->play(bank, snd, e, 0x10000, 0, 0x8000, 0x3334, 0, 0, 0, (m.u16(w + 0x2A) >> 3) & 2);
    }
    VM(eng, e).script_start(0xA, 0, 0);
    m.w32(inv + 0x13C, 1);
}

void weapon_draw(Engine& eng, U e, U inv) {
    Mem& m = eng.m;
    if (m.u32(inv) & 0x200040) {
        I v = (m.u32(inv + 0xA4) >> 2) & 0xFFFFFFFF;
        m.w32(inv + 0xA4, imax(v, 0x190));
        m.w32(0x800AA168, 0xFA);
        m.w32(inv, m.u32(inv) & ~I(0x40) & 0xFFDFFFFF);
    }
    m.w32(inv, m.u32(inv) & ~I(0x20));
    I slot = m.s8(inv + 0x54);
    I w = m.u8(G_SLOT_WEAPON + slot);
    m.w8(inv + 0x55, w);
    U wa = U(G_WEAPONS + 0x44 * w);
    m.w16(inv + 0x156, m.u16(wa + 0x28));
    m.w32(inv + 0x40, m.s16(wa + 4));
    U model = model_by_hash(eng, U(m.u32(wa)));
    if (model)
        eng.model_swap(e, model, 0x147);
    else
        eng.note("weapon model missing");
    VM vm(eng, e);
    vm.script_start(2, 1, 0);
    vm.run_a();
    bool empty = m.s16(inv + 0x66 + 2 * slot) == 0 && !(m.u16(G_WEAPONS + 0x44 * m.u8(G_SLOT_WEAPON + slot) + 0x2A) & 1);
    set_var(m, e, 0x10, empty ? 1 : 0);
    eng.audio->fade_entity(e, 10);
    I snd = m.s16(wa + 0x18);
    if (snd >= 0) {
        I bank = m.s16(wa + 0x1A);
        if (bank < 0) bank = m.u16(m.u32(m.u32(e + 0x100) + 8) + 0xC8);
        eng.audio->play(bank, snd, e, 0x10000, 0, 0x8000, 0x3334, 0, 0, 0, (m.u16(wa + 0x2A) >> 3) & 2);
    }
    set_var(m, e, 0x11, 0);
    m.w32(inv + 0x13C, 0);
    if (m.u32(inv) & 2) m.w32(e + 0x2D4, 0x500000);
    U pp = U(m.u32(e + 0x304));
    m.w32(pp + 0x10, 0x50000);
    m.w32(pp + 0x14, 0x50000);
}

I weapon_reload(Engine& eng, U e, U inv) {
    Mem& m = eng.m;
    I slot = m.s8(inv + 0x54);
    I w = m.u8(inv + 0x55);
    I clip = m.u16(G_WEAPONS + 0x44 * w + 0xA);
    U a = U(inv + 2 * slot);
    I have = m.u16(a + 0x66);
    I need = S16(clip - have);
    if (need <= 0) return 1;
    if (m.u32(G_CHEATS) & 0x40)
        m.w16(a + 0x66, clip);
    else {
        I res = m.s16(a + 0x56);
        if (need < res) {
            m.w16(a + 0x66, clip);
            m.w16(a + 0x56, res - need);
        } else if (res <= 0)
            return S16(have) > 0 ? 1 : 0;
        else {
            m.w16(a + 0x66, have + res);
            m.w16(a + 0x56, 0);
        }
    }
    m.w32(0x80039CBC, m.s32(0x80039CBC) + 1);
    if (w == 3 || w == 10) {
        if (m.s16(a + 0x66)) set_var(m, e, 0x10, 0);
    } else if (m.s32(inv + 0x13C) == 0) {
        VM(eng, e).script_start(9, 1, 0);
        I snd = m.s16(G_WEAPONS + 0x44 * w + 0x20);
        if (snd >= 0) {
            I bank = m.s16(G_WEAPONS + 0x44 * w + 0x22);
            if (bank < 0) bank = m.u16(m.u32(m.u32(e + 0x100) + 8) + 0xC8);
            eng.audio->play(bank, snd, e, 0x20000, 0, 0x7FFF, 0, 0, 0, 0, (weapon_flags(m, w) >> 3) & 2);
        }
    }
    return 1;
}

I scale_look(Mem& m, U inv, I v) {
    I f = m.u32(inv);
    if (f & 0x100) return v >> 3;
    if (m.u32(inv + 0x1C8) < 2) {
        v = (f & 0x80000) ? (v >> 1) - (v >> 3) : (v >> 1) + (v >> 4);
        if (f & 0x100000) v = (f & 0x80000000) ? v >> 2 : v >> 1;
        return v;
    }
    if (f & 0x100000) return (f & 0x80000000) ? v >> 2 : v >> 1;
    return v;
}

void aiming_controls(Engine& eng, U e, U inv, U co, I stance) {
    Mem& m = eng.m;
    if (stance != m.u32(inv + 0x1CC)) {
        m.w32(inv + 0x188, 0);
        m.w32(inv, m.u32(inv) & 0xFFEFFFFF & 0x7FFFFFFF);
        m.w32(e + 0x238, 0);
        m.w32(inv + 0x17C, 0);
        m.w32(e + 0x234, 0);
    }
    if (eng.action_held(CENTER)) {
        I v = (m.s32(inv + 0x188) * 3) >> 2;
        m.w32(inv + 0x188, v);
        I lo = cdiv(v << 8, m.s32(ROLE_BUTTONS + 3 * 0x34 + 0x1C));
        I box = m.s16(inv + 0xB4);
        m.w16(inv + 0x190, box >= iabs(S16(lo)) ? lo + 0x100 : (v > 0 ? box + 0x100 : 0x100 - box));
        v = (m.s32(inv + 0x17C) * 3) >> 2;
        m.w32(inv + 0x17C, v);
        lo = cdiv(v * 0x78, m.s32(ROLE_BUTTONS + 2 * 0x34 + 0x1C));
        box = m.s16(inv + 0xB6);
        m.w16(inv + 0x192, box >= iabs(S16(lo)) ? lo + 0x83 : (v > 0 ? box + 0x83 : 0x83 - box));
    } else {
        auto scaled = [&](I t) {
            I f = m.u32(inv);
            t = (f & 0x80000) ? (t >> 1) - (t >> 3) : (t >> 1) + (t >> 4);
            if (f & 0x100000) t = (f & 0x80000000) ? t >> 2 : t >> 1;
            return t;
        };
        I mx = m.s32(ROLE_BUTTONS + 3 * 0x34 + 0x1C);
        I t = scaled(eng.role(3));
        I v = S32(m.s32(inv + 0x188) + (t >> 3));
        I lo = cdiv(S32(v << 8), mx);
        m.w32(inv + 0x188, v);
        I box = m.s16(inv + 0xB4);
        if (box >= iabs(S16(lo)))
            m.w16(inv + 0x190, lo + 0x100);
        else {
            if (v > 0) {
                m.w16(inv + 0x190, box + 0x100);
                I x = box * mx;
                m.w32(inv + 0x188, (x < 0 ? x + 0xFF : x) >> 8);
            } else {
                m.w16(inv + 0x190, 0x100 - box);
                m.w32(inv + 0x188, cdiv(-(box * mx), 256));
            }
            m.w32(inv + 0x184, S32(m.s32(inv + 0x184) + t));
            m.w32(e + 0x22C, S32(m.s32(e + 0x22C) + t));
        }
        mx = m.s32(ROLE_BUTTONS + 2 * 0x34 + 0x1C);
        t = eng.role(2);
        if (t == 0 && m.u8(ANALOG + 0)) t = eng.role_alt(2);
        t = scaled(t);
        v = S32(m.s32(inv + 0x17C) + (t >> 3));
        m.w32(inv + 0x17C, v);
        lo = cdiv(v * 0x78, mx);
        t >>= 1;
        box = m.s16(inv + 0xB6);
        if (box >= iabs(S16(lo)))
            m.w16(inv + 0x192, lo + 0x83);
        else {
            I x;
            if (v > 0) {
                x = box * mx;
                m.w16(inv + 0x192, box + 0x83);
            } else {
                x = -(box * mx);
                m.w16(inv + 0x192, 0x83 - box);
            }
            m.w32(inv + 0x17C, cdiv(x, 120));
            m.w32(inv + 0x178, S32(m.s32(inv + 0x178) - t));
            m.w32(e + 0x228, S32(m.s32(e + 0x228) - t));
            I p = m.s32(inv + 0x178);
            if (p > 0x02AA0000) {
                m.w32(e + 0x228, S32(m.s32(e + 0x228) + 0x02AA0000 - p));
                m.w32(inv + 0x178, 0x02AA0000);
            }
            p = m.s32(inv + 0x178);
            if (p < -0x02AA0000) {
                m.w32(e + 0x228, S32(m.s32(e + 0x228) - 0x02AA0000 - p));
                m.w32(inv + 0x178, -0x02AA0000);
            }
            view_pitch_target(eng, inv, co);
        }
    }
    aim_update(eng, e, inv);
    m.w32(inv + 0x1CC, stance);
}

void controls(Engine& eng, U e, U inv) {
    Mem& m = eng.m;
    U co = U(m.u32(m.u32(G_PLAYER) + 0x388));
    I f0 = m.u32(inv);
    m.w32(inv, f0 & ~I(0xC));
    if (!(f0 & 0x20)) {
        if (m.u32(inv + 4) & 0x20) {
            for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, m.s32(inv + 0x90 + 4 * k));
            m.w32(inv + 0x9C, 0);
            m.w32(e + 0xC, m.u32(e + 0xC) | 0x20);
            m.w32(e + 0x10, m.u32(e + 0x10) & ~I(0x10));
        }
        if (m.s32(inv + 0x70)) eng.note("ladder_climb not implemented");
        m.w32(e + 0x200, -eng.role(0));
        m.w32(e + 0x218, eng.role(1));
    } else {
        if (!(m.u32(inv + 4) & 0x20)) {
            for (int k = 0; k < 3; k++) m.w32(inv + 0x90 + 4 * k, m.s32(e + 0x20C + 4 * k));
            m.w32(e + 0xC, m.u32(e + 0xC) & ~I(0x20));
            m.w32(e + 0x10, m.u32(e + 0x10) | 0x10);
        }
        m.w32(e + 0x200, 0);
        I r = eng.role(1);
        m.w32(e + 0x1F0, S32(m.s32(e + 0x1F0) + m.s32(inv + 0x90) - m.s32(e + 0x20C)));
        m.w32(e + 0x1F4, 0);
        m.w32(e + 0x210, m.s32(inv + 0x94));
        m.w32(e + 0x1F8, S32(m.s32(e + 0x1F8) + m.s32(inv + 0x98) - m.s32(e + 0x214)));
        I v = S32(r * 2 + m.s32(inv + 0x9C) * 3) >> 2;
        m.w32(e + 0x218, v);
        m.w32(inv + 0x9C, v);
    }
    I stance = m.u32(inv + 0x1C8);
    if (stance < 2) return aiming_controls(eng, e, inv, co, stance);
    if (stance != m.u32(inv + 0x1CC)) {
        m.w32(inv + 0x188, 0);
        m.w32(inv, m.u32(inv) & 0xFFEFFFFF & 0x7FFFFFFF);
        m.w32(e + 0x238, 0);
        m.w32(inv + 0x17C, 0);
        m.w32(e + 0x234, 0);
        if (stance == 5) {
            m.w16(inv + 0x190, 0x10E);
            m.w16(inv + 0x192, 0x73);
        } else {
            m.w16(inv + 0x190, 0x100);
            m.w16(inv + 0x192, 0x83);
        }
    }
    I t = scale_look(m, inv, eng.role(3));
    I r = ((t + 0x10000) & 0xFFFFFFFF) > 0x20000 ? (m.s32(inv + 0x188) * 3 + t) >> 2 : (m.s32(inv + 0x188) + 1) >> 4;
    m.w32(inv + 0x188, S32(r));
    m.w32(inv + 0x184, S32(m.s32(inv + 0x184) + t));
    I mode = m.u8(0x8009E3A0);
    if (mode == 1) {
        if (eng.action_held(CENTER)) {
            I p = m.s32(inv + 0x178);
            if (iabs(p >> 2) <= 0x27FFFF)
                m.w32(inv + 0x178, S32(p * 3 + 1) >> 2);
            else
                m.w32(inv + 0x178, S32(p * 7 + 1) >> 3);
        } else {
            I pin = eng.role(2);
            if (pin == 0 && (m.u32(inv) & 0x20) && m.u8(ANALOG + 0)) pin = eng.role_alt(2);
            pin = scale_look(m, inv, pin);
            if (pin) {
                I p = S32(m.s32(inv + 0x178) - (pin >> 1));
                p = imin(p, 0x02AA0000);
                p = imax(p, -0x02AA0000);
                m.w32(inv + 0x178, p);
            }
        }
    } else
        eng.note("look mode " + std::to_string(mode) + " not implemented");
    if (0 <= stance - 7 && stance - 7 < 2)
        aim_update(eng, e, inv);
    else {
        m.w32(e + 0x228, m.s32(inv + 0x178));
        m.w32(e + 0x22C, m.s32(inv + 0x184));
    }
    view_pitch_target(eng, inv, co);
    m.w32(inv + 0x1CC, stance);
}

}  // namespace

void fade_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U o = U(m.u32(e + 0x388));
    for (U off : {4u, 8u})
        if (m.s32(o + off) > 0) {
            m.w32(o + off, m.s32(o + off) - 1);
            return;
        }
    if (m.s32(o + 0xC) > 0) {
        m.w32(o + 0xC, m.s32(o + 0xC) - 1);
        return;
    }
    eng.remove(e, "fade done");
}

void player_update(Engine& eng, U e) {
    Mem& m = eng.m;
    U inv = U(m.u32(e + 0x388));
    if (m.s32(e + 0x210) > 0x13880000) eng.note("player fell out of the world (10000 damage) not implemented");
    if (m.u32(e + 0x38) & 0x4000)
        eng.note("player death not implemented");
    else {
        bool aimed = false;
        if (m.s32(e + 0x284)) {
            if (get_var(m, e, 3) & 2) m.w32(e + 0x38, m.u32(e + 0x38) | 2);
            if (m.s32(e + 0x284) && eng.action_held(AIM)) {
                aimed = true;
                I f = m.u32(inv) | 0x20;
                m.w32(inv, f);
                if (m.u8(inv + 0x55) == 5) {
                    m.w32(inv, (f & 0xFFDFFFFF) | 0x140);
                    m.w32(inv + 0xA8, 0x98F);
                } else if ((f & 0x400000) && m.s16(G_WEAPONS + 0x44 * m.u8(inv + 0x55) + 0x28)) {
                    m.w32(inv, (f & ~I(0x40)) | 0x200000);
                    m.w32(inv + 0xA8, 0x1DB);
                }
            }
        }
        if (!aimed) m.w32(inv, m.u32(inv) & ~I(0x20) & ~I(0x40) & 0xFFDFFFFF);
        m.w32(inv + 0x1C8, player_stance(m, inv));
        I sc = m.s16(e + 0x11E);
        if (sc == 2 || sc == 9 || sc == 10 || sc == 13 || sc == 16 || (m.s32(inv + 0x48) > 0 && sc == 8))
            m.w32(inv, m.u32(inv) | 0x800000);
        else
            m.w32(inv, m.u32(inv) & 0xFF7FFFFF);
        if (m.s32(inv + 0x13C) == 0 && m.s32(inv + 0x124) == 0 && eng.action_pressed(SWITCH)) weapon_holster(eng, e, inv);
        if (get_var(m, e, 0x11)) {
            I slot = next_slot(m, inv);
            set_var(m, e, 0x11, 0);
            m.w8(inv + 0x54, slot);
            I w = m.u8(G_SLOT_WEAPON + S8(slot));
            I s8 = S8(slot);
            if ((w == 3 || w == 10) && m.s16(inv + 0x66 + 2 * s8) == 0 && m.s16(inv + 0x56 + 2 * s8) == 0) m.w8(inv + 0x54, next_slot(m, inv));
            weapon_draw(eng, e, inv);
        } else {
            I slot = m.s8(inv + 0x54);
            I w = m.u8(G_SLOT_WEAPON + slot);
            if ((w == 3 || w == 10) && m.s16(inv + 0x66 + 2 * slot) == 0 && m.s16(inv + 0x56 + 2 * slot) == 0) {
                m.w8(inv + 0x54, next_slot(m, inv));
                weapon_draw(eng, e, inv);
            }
        }
        if (eng.action_pressed(USE)) {
            eng.zones_use(e);
            I slot = m.s8(inv + 0x54);
            if (m.s32(inv + 0x13C) == 0 && !(weapon_flags(m, m.u8(G_SLOT_WEAPON + slot)) & 1) && weapon_reload(eng, e, inv) == 0)
                set_var(m, e, 0x10, 1);
        }
        I k = S32(m.s32(inv + 0x4C) + m.s32(inv + 0x50));
        k = imin(k, 0xF0000);
        if (k > 0)
            k -= 0x7FFF;
        else if (k < -0x2000)
            k += 0x1FFF;
        else
            k = 0;
        m.w32(inv + 0x4C, k);
        I c = m.s32(inv + 0x50);
        m.w32(inv + 0x50, c >= 0x2001 ? c - 0x2000 : 0);
        m.w32(inv, m.u32(inv) & 0xFFFBFFFF);
        I w = m.u8(inv + 0x55);
        if (w == 3 || w == 10) {
            if (m.s32(inv + 0x48) > 0) {
                m.w32(inv + 0x48, m.s32(inv + 0x48) - 1);
                m.w32(inv + 0x128, 0);
            } else if (m.s32(inv + 0x124) == 0) {
                I held = eng.action_held(FIRE) ? 1 : 0;
                if (held) {
                    if (m.s32(inv + 0x128)) {
                        if (m.s32(inv + 0x11C) < 0x1E) m.w32(inv + 0x11C, m.s32(inv + 0x11C) + 1);
                    } else
                        m.w32(inv + 0x11C, 0);
                } else if (m.s32(inv + 0x128))
                    m.w32(e + 0x3C, m.u32(e + 0x3C) | 2);
                m.w32(inv + 0x128, held);
            }
        } else {
            if (m.s32(inv + 0x48) > 0)
                m.w32(inv + 0x48, m.s32(inv + 0x48) - 1);
            else {
                bool fire = m.s32(inv + 0x44) > 0 ? eng.action_held(FIRE) : eng.action_pressed(FIRE);
                if (fire) m.w32(e + 0x3C, m.u32(e + 0x3C) | 2);
            }
        }
        if (!(m.u32(inv) & 0x20) && m.s32(e + 0x284) && eng.action_pressed(JUMP) && !(m.u32(inv) & 2)) {
            m.w32(e + 0x284, 0);
            m.w32(e + 0x21C, (m.u32(inv) & 0x10000000) ? -0x68000 : -0x1A0000);
        }
        crouch(eng, e, inv);
        if (m.s32(inv + 0x70) && (m.u32(inv) & 2)) player_stand(eng, e, inv);
        controls(eng, e, inv);
        if (m.s32(inv + 0x70) == 0) m.w32(e + 0xC, m.u32(e + 0xC) | 0x20);
        if ((m.u32(inv) & 0x20) || (m.s32(e + 0x200) == 0 && m.s32(e + 0x218) == 0)) {
            set_var(m, e, 2, 0);
            m.w32(e + 0x3C, m.u32(e + 0x3C) | 0x80);
        } else {
            set_var(m, e, 2, 1);
            m.w32(e + 0x3C, m.u32(e + 0x3C) | 0x40);
        }
        I slot = m.s8(inv + 0x54);
        if (!(weapon_flags(m, m.u8(G_SLOT_WEAPON + slot)) & 1)) {
            if (m.s16(inv + 0x66 + 2 * slot) == 0 && m.s32(inv + 0x48) <= 0 && m.s16(inv + 0x56 + 2 * slot) != 0) weapon_reload(eng, e, inv);
        }
        I cc = m.s32(inv + 0x38);
        if (cc) {
            m.w32(inv + 0x38, cc - 1);
            eng.note("player melee part (inv+0x38) not implemented");
        } else if (m.u32(e + 0x114) ? (m.u32(m.u32(e + 0x114)) & 8) : false) {
            if (m.s16(m.u32(e + 0x100) + 0x5E) == 0) {
                m.w32(inv + 0x38, 4);
                eng.note("player melee part (0x8004C64C) not implemented");
            }
        }
        if (m.u16(inv + 0x154)) {
            if (m.u32(inv) & 2)
                m.w16(inv + 0x154, 0);
            else {
                for (int k2 = 0; k2 < 3; k2++)
                    m.w32(e + 0x1F0 + 4 * k2, S32(m.s32(e + 0x1F0 + 4 * k2) - (m.s32(e + 0x218 + 4 * k2) >> 2) + m.s32(inv + 0x80 + 4 * k2)));
                if (!(m.u16(inv + 0x154) & 1))
                    for (U o : {0x24Cu, 0x254u, 0x250u}) m.w32(e + o, m.s32(e + o) >> 1);
                m.w16(inv + 0x154, m.u16(inv + 0x154) - 1);
            }
        }
        if (m.u32(inv) & 0x200040) {
            if (m.u32(inv + 0xA4) < m.u32(inv + 0xA8)) {
                if (m.u32(inv + 0xAC) < 0x40) m.w32(inv + 0xAC, m.u32(inv + 0xAC) + 2);
                m.w32(inv + 0xA4, m.u32(inv + 0xA4) + m.u32(inv + 0xAC));
                m.w32(G_SUBDIV, m.s32(G_SUBDIV) + m.u32(inv + 0xAC));
            }
        } else {
            m.w32(inv + 0xA4, 0x190);
            m.w32(G_SUBDIV, (m.u32(inv) & 2) ? 0x12C : 0xFA);
            m.w32(inv, m.u32(inv) & ~I(0x100));
        }
        if (m.u32(inv + 0xA4) == 0x190) m.w32(inv + 0xAC, 1);
    }
    VM vm(eng, e);
    vm.run_b();
    if (m.s32(e + 0x288) < 0) {
        m.w32(e + 0x288, 1);
        m.w32(e + 0x28C, 1);
        m.w32(e + 0x22C, S32(m.s32(e + 0x22C) + 0x08000000));
    }
    vm.run_a();
    I lad = m.s32(inv + 0x70);
    m.w32(inv + 4, m.u32(inv));
    if (lad) {
        m.w32(inv + 0x74, lad);
        m.w32(inv + 0x70, 0);
    } else if (m.s32(inv + 0x74)) {
        m.w32(e + 0x21C, 0);
        m.w32(inv + 0x74, 0);
    }
    m.w32(inv + 0x8C, m.s32(e + 0x284));
    m.w32(inv, m.u32(inv) & ~I(0xE00));
}

void player_move(Engine& eng, U e) {
    Mem& m = eng.m;
    Walker w;
    for (int k = 0; k < 3; k++) {
        w.pos[k] = int32_t(m.s32(e + 0x20C + 4 * k));
        w.old[k] = int32_t(m.s32(e + 0x3BC + 4 * k));
        w.push[k] = int32_t(m.s32(e + 0x1F0 + 4 * k));
    }
    w.vy = int32_t(m.s32(e + 0x21C)), w.fwd = int32_t(m.s32(e + 0x200)), w.strafe = int32_t(m.s32(e + 0x218));
    w.yaw = int32_t(m.s32(e + 0x3CC + 0x184)), w.ground = int32_t(m.s32(e + 0x284)), w.floor = int32_t(m.s32(e + 0x208));
    w.radius = int32_t(m.s16(e + 0x2EC)), w.eye = int32_t(m.s16(e + 0x2D6)), w.flags_b = uint32_t(m.u32(e + 0x10));
    w.gravity = m.u32(e + 0xC) & 0x20;
    for (int k = 0; k < 3; k++) {
        m.w32(e + 0x258 + 4 * k, w.pos[k]);
        m.w32(e + 0x264 + 4 * k, m.s32(e + 0x228 + 4 * k));
        m.w32(e + 0x228 + 4 * k, S32(m.s32(e + 0x228 + 4 * k) + m.s32(e + 0x234 + 4 * k)));
    }
    m.w32(e + 0x220, m.u32(e + 0x200));
    w.roll = int32_t(m.s32(e + 0x230));
    w.zone = 0xFFFF;
    int32_t vy0 = w.vy;
    entity_move(eng.world.w, w);
    for (int k = 0; k < 3; k++) {
        m.w32(e + 0x20C + 4 * k, w.pos[k]);
        m.w32(e + 0x354 + 4 * k, S16(w.pos[k] >> 16));
        m.w32(e + 0x3BC + 4 * k, w.old[k]);
        m.w32(e + 0x1F0 + 4 * k, 0);
    }
    m.w32(e + 0x21C, w.vy);
    m.w32(e + 0x284, w.ground);
    m.w32(e + 0x208, w.floor);
    if (!(w.flags_b & 0x10)) m.w32(e + 0x390, w.zone);
    int n = w.slide_planes, res = w.slide_result;
    if (n > 0) {
        m.w32(e + 0x218, 0);
        m.w32(e + 0x220, 0);
    }
    if (res == 2 || n > 0) m.w32(e + 0x38, m.u32(e + 0x38) | 0x8000);
    if (n > 0)
        for (int k = 0; k < 3; k++) m.w16(e + 0x364 + 2 * k, w.delta[k]);
    (void)vy0;
    if (w.events & kEvLanding)
        for (U o : {0x200u, 0x218u, 0x21Cu, 0x220u}) m.w32(e + o, 0);
}

void player_weapon_update(Engine& eng, U e) {
    Mem& m = eng.m;
    U inv = U(m.u32(e + 0x388));
    if (!(m.u32(e + 0x38) & 0x4000)) {
        I w = m.u8(inv + 0x55);
        if (w == 3 || w == 10) {
            if ((get_var(m, e, 1) & 0xFF) || m.s32(inv + 0x124)) eng.note("throw_weapon not implemented");
        } else if (w == 7) {
            if (get_var(m, e, 1)) {
                I d = m.s32(inv + 0x40);
                m.w32(inv + 0x44, d);
                m.w32(inv + 0x48, iabs(d));
            }
        } else {
            I slot = m.s8(inv + 0x54);
            if (!(weapon_flags(m, m.u8(G_SLOT_WEAPON + slot)) & 1)) fire_gun(eng, e, inv, get_var(m, e, 1) & 0xFF);
        }
    }
    set_var(m, e, 1, 0);
}

}  // namespace valor::sim
