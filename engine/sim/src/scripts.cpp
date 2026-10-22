// The model-script VM: tools/sim/sst_vm.py (VM) with tools/sim/engine_scripts.py (EVM),
// engine_world_ops.py and engine_effects.py.
#include "valor/sim/scripts.h"

#include <cstring>

#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"

namespace valor::sim {

namespace {
constexpr U STORED_LENGTH = 0x80090EA0;
constexpr U G_ALARM_ = 0x80097FE0;
constexpr U G_PARAMS = 0x800939F4;
constexpr U G_WITNESS_SPOT = 0x8009817C;
constexpr U G_PATHS = 0x8009E290;
}  // namespace

VM::VM(Engine& en, U ent) : eng(en), m(en.m), e(ent) {
    inst = U(m.u32(e + 0x100));
    model = U(m.u32(inst + 0x8));
    type = m.s32(e + 0x8);
    ctx = e + 0x104;
}

I VM::rand() { return eng.rand.draw(FN("scripts")); }

I VM::world(I op, I var, const I* computed) {
    if (computed) return *computed;
    return world_value(*this, op, var);
}

// --- variables (get_var 0x8005CBA8, set_var 0x8005C97C)
I VM::get_var(I vid) {
    vid = S32(vid);
    if (0x60 <= vid && vid < 0x80) return sensor(vid);
    if (vid & 0x1C0) {
        if (vid & 0x100) {
            I n = vid ^ 0x100;
            return (m.u32(e + 0xEC + 4 * (n >> 5)) & (I(1) << (n & 31))) ? 1 : 0;
        }
        if (vid & 0x40) return (m.u32(e + 0xC) & (I(1) << ((vid - 0x40) & 63))) ? 1 : 0;
        if (vid & 0x80) return (m.u32(e + 0x10) & (I(1) << ((vid - 0x80) & 63))) ? 1 : 0;
        return 0;
    }
    return m.s32(e + 0x4C + 4 * vid);
}

void VM::set_var(I vid, I value) {
    vid = S32(vid);
    if (0x60 <= vid && vid < 0x80) {
        if (vid == 0x6A) {
            I a[1] = {value};
            effect("set_var_6A", a, 1);
        }
        return;
    }
    if (vid & 0x1C0) {
        U a;
        I bit;
        if (vid & 0x100) {
            I n = vid & ~I(0x100);
            a = U(e + 0xEC + 4 * (n >> 5)), bit = I(1) << (n & 31);
        } else if (vid & 0x40)
            a = e + 0xC, bit = I(1) << ((vid - 0x40) & 63);
        else if (vid & 0x80)
            a = e + 0x10, bit = I(1) << ((vid - 0x80) & 63);
        else
            return;
        m.w32(a, value ? (m.u32(a) | bit) : (m.u32(a) & ~bit));
        return;
    }
    m.w32(e + 0x4C + 4 * vid, value);
}

I VM::sensor(I vid) {
    if (vid == 0x67 || vid == 0x71) return sense_sensor(*this, vid);
    U p = U(m.u32(G_SOLDIER));
    if (vid == 0x60) return m.u16(m.u32(m.u32(p + 0x100) + 8) + 0xC8);
    if (vid == 0x61) return m.u32(p + 0x10) & 0x400;
    if (vid == 0x62) return e + 0x3A4;
    if (vid == 0x63) {
        I dx = S16((m.s32(p + 0x20C) >> 16) - (m.s32(e + 0x20C) >> 16));
        I dz = S16((m.s32(p + 0x214) >> 16) - (m.s32(e + 0x214) >> 16));
        return dx * m.s16(e + 0x344) + dz * m.s16(e + 0x350) < 0 ? 1 : 0;
    }
    if (vid == 0x64) return m.s32(e + 0x29C);
    if (vid == 0x65) return m.u8(m.u32(p + 0x388) + 0x55);
    if (vid == 0x66) return m.s16(p + 0x11E);
    if (vid == 0x69) return m.s32(G_ALARM_);
    if (vid == 0x6B) return m.u32(p + 0x10) & 0x800;
    if (vid == 0x6C) return (m.u32(m.u32(p + 0x388)) >> 1) & 1;
    if (vid == 0x6D) {
        I n = 0;
        for (U o : m.entities()) {
            if (m.s32(o + 8) == SOLDIER && o != e && !(m.u32(o + 0xC) & 0x20080000)) {
                I d[3];
                for (int i = 0; i < 3; i++) d[i] = S16((m.s32(e + 0x20C + 4 * i) - m.s32(o + 0x20C + 4 * i)) >> 16);
                if (dist_approx(d[0], d[1], d[2]) < 3000 && iabs(d[1]) < 1000) n++;
            }
        }
        return n;
    }
    if (vid == 0x6E) return G_WITNESS_SPOT;
    if (vid == 0x6F) return S16(m.s32(e + 0x210) >> 16) - S16(m.s32(p + 0x210) >> 16);
    if (vid == 0x70) return m.s32(m.u32(m.u32(e + 0x100) + 8));
    if (vid == 0x72) return m.s32(G_PARAMS);
    if (vid == 0x73) return m.s32(0x800939F8);
    if (vid == 0x74) {
        I d[3];
        for (int i = 0; i < 3; i++) d[i] = S16((m.s32(p + 0x20C + 4 * i) >> 16) - m.u16(e + 0x3A4 + 2 * i));
        return dist_approx(d[0], d[1], d[2]);
    }
    if (vid == 0x68) {
        I v = m.s32(e + 0xFC);
        if (!(1 <= v && v <= 23)) return 0;
        U voice = U(0x8009115C + 0x34 * v);
        return (m.u8(voice + 0xE) == 0 && (m.u8(0x800910D0 + v) & 1) == 1 && m.u16(voice + 0x28) < 120 && !(m.u32(voice) & 1)) ? 1 : 0;
    }
    return 0;
}

// --- scripts
U VM::script_entry(I idx) { return U(m.u32(model + 0xB8) + 16 * idx); }

I VM::script_of(U code) {
    U lst = U(m.u32(model + 0xB8));
    I n = m.u16(model + 0xBC);
    for (I i = 0; i < n; i++)
        if (m.u32(lst + 16 * i + 0xC) == code) return i;
    return -1;
}

U VM::code_base(U c) {
    I d = m.s16(c + 0x1E);
    if (d) return U(m.u32(c + 0x24 + 16 * (d - 1) + 8));
    return U(m.u32(m.u32(c + 0x10) + 0xC));
}

I VM::code_len(U c) {
    I d = m.s16(c + 0x1E);
    if (d) return m.u16(c + 0x24 + 16 * (d - 1) + 0xC);
    return m.u16(m.u32(c + 0x10) + 0xA);
}

I VM::script_start(I idx, I force, I chn) {
    idx = S16(idx);
    if (idx < 0) return 0;
    U s = script_entry(idx);
    U c = U(e + 0x104 + 0x74 * chn);
    I cls = m.u32(s);
    bool started = false;
    I blend = 0;
    if (force)
        started = true, blend = 0;
    else if (cls & m.u32(c + 4))
        started = true, blend = 4;
    else if ((cls & m.u32(c)) || (!(cls & m.u32(c + 8)) && !(cls & m.u32(c + 0xC)) && m.u32(c + 0x10) == 0))
        started = true, blend = 0;
    if (!started) return 0;
    m.w32(c + 0x10, s);
    m.w16(c + 0x1A, idx);
    m.w32(c + 0x14, m.u32(s + 0xC));
    m.w16(inst + 0x3E, blend);
    if (blend) {
        m.w16(inst + 0x40, -1);
        I a[1] = {idx};
        I save = ch;
        ch = chn;
        effect("pose_blend", a, 1);
        ch = save;
    }
    m.w32(e + 0x1FC, 0);
    m.w32(c, 0);
    m.w32(c + 4, 0xFFFFFFFF);
    m.w32(c + 8, 0);
    m.w32(c + 0xC, 0);
    m.w32(e + 0x28C, m.u32(e + 0x288));
    I count = m.u32(e + 0x1EC);
    U table = U(m.u32(c + 0x20));
    for (I i = 0; i < count; i++) m.w32(table + 0x3C * i, 0);
    if (chn == 0) {
        effect("hit_part_reset", nullptr, 0);
        I n = m.u32(model + 0xB0);
        U src = U(m.u32(model + 0xB4));
        m.copy(table, src, imin(n, count) * 0x3C);
    }
    m.w16(c + 0x18, -1);
    m.w16(c + 0x1C, 0);
    m.w16(c + 0x1E, 0);
    return 1;
}

U VM::sop_event(U pc, I op) {
    U c = ctx;
    I w = m.u32(pc);
    I cond = w & 0xFF;
    static const I bits[8] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80};
    static const U slots[8] = {8, 0xC, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24};
    I vals[8] = {0};
    I k = 1;
    for (int i = 0; i < 8; i++)
        if (w & bits[i]) vals[i] = m.u32(pc + 4 * k), k++;
    I count = m.u32(e + 0x1EC);
    U table;
    if (m.s16(c + 0x1A) == m.s16(model + 0x94))
        table = U(m.u32(model + 0xB4)), count = m.u32(model + 0xB0);
    else if (op == 0x0E)
        table = U(m.u32(e + 0x124));
    else
        table = U(m.u32(c + 0x20));
    I slot = -1;
    for (I i = 0; i < count; i++) {
        U r = U(table + 0x3C * i);
        I cc = m.u32(r);
        if (cc == 0 && slot == -1) {
            slot = i;
            continue;
        }
        bool all = cc == cond;
        for (int j = 0; j < 8 && all; j++) all = m.u32(r + slots[j]) == vals[j];
        if (all) {
            slot = i;
            break;
        }
    }
    I stored = m.u8(pc + 4 * k + 3);
    I n = m.s32(STORED_LENGTH + 4 * stored);
    if (slot >= 0) {
        U r = U(table + 0x3C * slot);
        m.w32(r, stored == 0x0F ? 0 : cond);
        for (int j = 0; j < 8; j++) m.w32(r + slots[j], vals[j]);
        m.w32(r + 4, (w >> 8) & 0xFF);
        m.copy(r + 0x28, pc + 4 * k, 4 * n);
    }
    return U(pc + 4 * (k + n));
}

U VM::find_type_group_vm(I typ, I grp) {
    for (U o : m.entities())
        if (m.s32(o + 8) == S32(typ) && (S32(m.u8(o + 0x35) << 24) >> 24) == grp && !(m.u32(o + 0xC) & 0x20080000)) return o;
    return 0;
}

U VM::nearest_vm(I typ) {
    U best = 0;
    I bd = 0x7FFF;
    for (U o : m.entities()) {
        if (m.s32(o + 8) == S32(typ) && o != e && !(m.u32(o + 0xC) & 0x20080000)) {
            I d[3];
            for (int i = 0; i < 3; i++) d[i] = S16((m.s32(e + 0x20C + 4 * i) - m.s32(o + 0x20C + 4 * i)) >> 16);
            I dd = dist_approx(d[0], d[1], d[2]) & 0xFFFF;
            if (dd < bd && iabs(d[1]) < 1000) best = o, bd = dd;
        }
    }
    return best;
}

U VM::op_query(U pc, I mask, I w) {
    // engine_scripts.EVM.op_query: the target as the original finds it
    {
        I typ = operand(2, mask, m.s32(pc + 8));
        I grp = operand(3, mask, (w >> 8) & 0xFF);
        I tgt = operand(4, mask, m.s32(pc + 12)) & 0xFFFFFFFF;
        I typ8 = S32(typ << 24) >> 24, grp8 = S32(grp << 24) >> 24;
        if (tgt == 0) {
            if (typ8 != -1)
                tgt = grp8 != -1 ? find_type_group(m, typ8, grp8) : sense_nearest(m, e, typ8);
            else if (grp8 == typ8)
                tgt = e;
        }
        query_target = U(tgt);
    }
    I fact = w & 0xFF;
    I var = operand(1, mask, m.s32(pc + 4));
    I typ = operand(2, mask, m.s32(pc + 8));
    I grp = operand(3, mask, (w >> 8) & 0xFF);
    I tgt = operand(4, mask, m.s32(pc + 12));
    tgt &= 0xFFFFFFFF;
    grp = S32(grp << 24) >> 24;
    if (tgt == 0) {
        if (S32(typ) != -1)
            tgt = grp != -1 ? find_type_group_vm(typ, grp) : nearest_vm(typ);
        else if (grp == -1)
            tgt = e;
    }
    if ((fact == 1 || fact == 3 || fact == 4 || fact == 5 || fact == 6) && tgt) {
        U off = fact == 1 ? 0xF4 : fact == 3 ? 0x2D4 : fact == 4 ? 0x290 : fact == 5 ? 0x44 : 0x48;
        set_var(var, m.s32(tgt + off));
    } else if (fact == 8 && tgt)
        set_var(var, tgt);
    else if (fact == 8)
        set_var(var, world(0x2E, var));
    else if (fact == 9 && tgt) {
        I d = dist_approx((m.s32(e + 0x20C) - m.s32(tgt + 0x20C)) >> 16, (m.s32(e + 0x210) - m.s32(tgt + 0x210)) >> 16,
                          (m.s32(e + 0x214) - m.s32(tgt + 0x214)) >> 16);
        set_var(var, world(0x2E9, var, &d));
    } else if (fact == 0xD)
        set_var(var, e);
    else if (fact == 0xE)
        set_var(var, m.s16(inst + 0x5C));
    else if (fact == 0xA) {
        if (typ) {
            U t = U(typ & 0xFFFFFFFF);
            I d = dist_approx((m.s32(e + 0x20C) - m.s32(t + 0x20C)) >> 16, (m.s32(e + 0x210) - m.s32(t + 0x210)) >> 16,
                              (m.s32(e + 0x214) - m.s32(t + 0x214)) >> 16);
            set_var(var, world(0x2E9, var, &d));
        }
    } else if (fact == 0xC && tgt)
        set_var(var, world(0x2EC, var));
    else if (fact == 0xB || fact == 0xF || fact == 0x10 || fact == 0x11 || fact == 0x12)
        set_var(var, world(0x2E00 | fact, var));
    else if (fact == 1 || fact == 3 || fact == 4 || fact == 5 || fact == 6 || fact == 9 || fact == 0xC)
        set_var(var, world(0x2E00 | fact, var));
    return pc + 0x10;
}

// ---------------------------------------------------------------- the effects (engine_effects.py)

namespace {

void brain_clear_moves(Mem& m, U b) { m.w32(b + 4, m.u32(b + 4) & ~I(0x5C03)); }

void brain_release_node(Engine& eng, U e) {
    Mem& m = eng.m;
    U b = U(m.u32(e + 0x38C));
    I n = m.s16(b + 0xB6);
    if (n >= 0) {
        U wo = U(m.u32(m.u32(0x8009D648) + 0x388));
        if (n < m.u32(wo + 0xC)) {
            U nd = U(m.u32(wo + 8) + 0x34 * n);
            if (m.u32(nd + 0x30) == m.u32(e)) m.w32(nd + 0x30, 0);
        }
        m.w16(b + 0xB6, -1);
    }
}

void brain_reset_gait(Engine& eng, U e) {
    Mem& m = eng.m;
    U b = U(m.u32(e + 0x38C));
    m.w16(e + 0x2EC, m.s16(b + 0xC4));
    m.w16(e + 0x2E8, m.u16(b + 0xC6));
    m.w16(e + 0x3C8, m.s16(b + 0xCA));
    m.w16(b + 0xC0, 0);
    m.w16(b + 0xC2, 0);
    m.w32(e + 0x224, 0x10000);
    m.w32(b + 0xD8, 0);
    m.w16(b + 0xCC, m.u16(b + 0xD0));
}

I path_find(Mem& m, I pid) {
    U base = U(m.u32(G_PATHS));
    for (I i = 0; i < m.s32(0x8009E28C); i++)
        if (m.s16(base + 8 * i) == S16(pid)) return i;
    return -1;
}

void behaviour(VM& vm, I goal, I a, I b_, I c, I d) {
    Engine& eng = vm.eng;
    Mem& m = vm.m;
    U e = vm.e;
    U b = U(m.u32(e + 0x38C));
    if (!b) {
        eng.note("behaviour without a brain");
        return;
    }
    goal = S32(goal);
    I goals = m.u32(b + 4);
    if (goal == -1) {
        brain_reset_gait(eng, e);
        m.w32(b + 4, goals & ~I(1));
    } else if (goal == -0x4000 || goal == -0x1000 || goal == -2) {
        brain_reset_gait(eng, e);
        m.w32(b + 4, goals & ~(-goal));
    } else if (goal == -0x800) {
        brain_reset_gait(eng, e);
        brain_release_node(eng, e);
        m.w32(b + 4, goals & ~I(0x800));
    } else if (goal == -4 || goal == -0x400) {
        m.w32(b + 4, goals & ~(-goal));
    } else if (goal == -0x2000) {
        m.w32(b + 4, goals | 2);
    } else if (goal == 0x2000) {
        m.w32(b + 4, goals & ~I(2));
    } else if (goal == 1) {
        brain_clear_moves(m, b);
        brain_release_node(eng, e);
        brain_reset_gait(eng, e);
        m.w32(b + 4, m.u32(b + 4) | 1);
        m.w32(b + 0x10, a);
        m.w16(b + 0xC2, b_);
        m.w16(b + 0xD4, c);
        if (S16(c) <= 0) m.w16(b + 0xD4, 0x100);
    } else if (goal == 4) {
        I g = goals | 4;
        m.w32(b + 4, g);
        if (g == 4) brain_reset_gait(eng, e);
        m.w32(b + 0x18, a);
        m.w32(b + 0xDC, a);
        m.w32(b + 0, -0xA);
        m.w16(b + 0xC0, b_);
        m.w32(b + 0xD8, c);
    } else if (goal == 2) {
        brain_clear_moves(m, b);
        brain_release_node(eng, e);
        brain_reset_gait(eng, e);
        m.w32(b + 4, m.u32(b + 4) | 2);
        I path = path_find(m, a);
        m.w16(b + 0xB8, path);
        m.w16(b + 0xBA, b_);
        I mode = S16(b_);
        m.w16(b + 0xBC, path_nearest_point(m, e, path));
        if (mode == 0 || mode == 2)
            m.w16(b + 0xBE, 1);
        else if (mode == 1)
            m.w16(b + 0xBE, -1);
        else if (mode == -1)
            m.w16(b + 0xBA, goal);
    } else if (goal == 0x1000) {
        brain_clear_moves(m, b);
        brain_release_node(eng, e);
        brain_reset_gait(eng, e);
        m.w32(b + 4, m.u32(b + 4) | 0x1000);
        m.w32(b + 0x40, a);
        m.w32(b + 0x80, b_);
        m.w16(b + 0xA8, c);
        m.w16(b + 0xC2, d);
    } else if (goal == 0x800) {
        brain_clear_moves(m, b);
        brain_release_node(eng, e);
        brain_reset_gait(eng, e);
        m.w32(b + 4, m.u32(b + 4) | 0x800);
        m.w32(b + 0x3C, a);
        m.w32(b + 0x7C, b_);
        m.w16(b + 0xA8, c);
        NodeTarget got;  // brain_node_target (0x80057474) into the brain
        brain_node_target(m, e, got, S32(a));
        for (int k = 0; k < 3; k++) m.w16(b + 0xA0 + 2 * U(k), got.node_point[size_t(k)]);
        m.w16(b + 0xAC, got.side[0]), m.w16(b + 0xAE, got.side[1]);
        m.w32(b + 0x8C, got.via);
        m.w16(b + 0xB6, got.claimed);
        m.w16(b + 0xC2, d);
    } else if (goal == 0x4000) {
        brain_clear_moves(m, b);
        brain_release_node(eng, e);
        brain_reset_gait(eng, e);
        m.w32(b + 4, m.u32(b + 4) | 0x4000);
        m.w32(b + 0x10, a);
        m.w32(b + 0x50, b_);
    } else if (goal == 0x8000) {
        m.w32(b + 4, 0);
        brain_release_node(eng, e);
        brain_reset_gait(eng, e);
    }
}

void save_pose(VM& vm) {
    Mem& m = vm.m;
    U inst = vm.inst;
    U p = U(m.u32(inst + 0x88));
    if (p) {
        I nb = m.u16(inst + 0x72);
        m.copy(m.u32(inst + 0x8C), p, 8 * nb);
        m.w32(inst + 0x88, m.u32(inst + 0x8C));
    }
    for (int k = 0; k < 3; k++) m.w16(inst + 0xA0 + 2 * k, m.s16(inst + 0x98 + 2 * k));
}

void sound(VM& vm, I snd, I pitch, I pspread, I vol, I vspread, I bank, I flags) {
    Engine& eng = vm.eng;
    Mem& m = vm.m;
    U e = vm.e;
    snd = S32(snd);
    if (snd < 0) return;
    I loop = (flags & 0x20) ? 1 : 0;
    if (flags & 3) {
        I v = eng.audio->play(0, snd, e, vol, vspread, pitch, pspread, 0, loop, 0, 0);
        if (v >= 0 && (flags & 2) && (vol >> 2)) {
            eng.rand.draw(FN("sound"));
            eng.rand.draw(FN("sound"));
        }
        return;
    }
    bank = bank < 0x100 ? S32(bank << 24) >> 24 : S32(bank);
    if (bank == -1) {
        U md = m.u32(e + 0x100) ? U(m.u32(m.u32(e + 0x100) + 8)) : 0;
        bank = md ? m.u16(md + 0xC8) : 0;
    }
    m.w32(e + 0xFC, eng.audio->play(bank, snd, e, vol, vspread, pitch, pspread, 0, loop, 0, 0));
}


void prop(VM& vm, I sel, I value, I typ, I grp, I tgt) {
    Mem& m = vm.m;
    U t = U(tgt & 0xFFFFFFFF);
    if (t == 0) {
        t = vm.e;
        if (S32(typ) != -1 && S32(grp) != -1) t = find_type_group(m, S32(typ << 24) >> 24, S32(grp << 24) >> 24);
    }
    if (!t) {
        vm.eng.note("property of a missing entity");
        return;
    }
    value = S32(value);
    if (sel == 1)
        m.w32(t + 0xF4, value);
    else if (sel == 2)
        for (int k = 0; k < 3; k++) m.w32(t + 0x270 + 4 * k, value);
    else if (sel == 3)
        m.w32(t + 0x2D4, value);
    else if (sel == 4) {
        m.w32(t + 0x294, value >> 16);
        m.w32(t + 0x290, value >> 16);
    } else if (sel == 5)
        m.w32(t + 0x44, value >> 16);
    else if (sel == 6)
        m.w32(t + 0x48, value >> 16);
    else if (sel == 19)
        m.w16(t + 0x2E8, value >> 16);
    else if (sel == 20)
        m.w16(t + 0x2EC, value >> 16);
}

}  // namespace

void hit_parts_set(Mem& m, U e, I mask, I kind, I radius, I damage) {
    U pp = U(m.u32(e + 0x304));
    for (I i = 0; i < m.u16(e + 0x36); i++) {
        U q = U(pp + 0x48 * i);
        if (!(m.u32(q + 4) & mask)) continue;
        if (kind > 0)
            m.w32(q + 8, kind);
        else if (kind < 0)
            m.w32(q + 8, m.u32(q + 0xC));
        if (damage > 0)
            m.w32(q + 0x10, damage);
        else if (damage < 0)
            m.w32(q + 0x10, m.u32(q + 0x14));
        if (radius > 0) {
            m.w32(q + 0x34, radius);
            m.w32(q + 0x38, S32(radius * radius));
        } else if (radius < 0) {
            I r = m.s32(q + 0x30);
            m.w32(q + 0x34, r);
            m.w32(q + 0x38, S32(r * r));
        }
    }
}

void VM::effect(const char* name, const I* a, int) {
    std::string n = name;
    if (n == "hit_part") {
        hit_parts_set(m, e, a[0] & 0xFFFFFFFF, S32(a[1]), S32(a[2]), S32(a[3] << 16));
        if (m.s32(e + 8) == 1) {
            if (a[1] == 9 || a[1] == 5 || a[1] == 6) {
                // stat_melee (0x8008034C): flags A | 0x10000000, the melee statistic (0x80039CB4)
                // and the view object's count (+0x14C)
                m.w32(e + 0xC, m.u32(e + 0xC) | 0x10000000);
                m.w32(0x80039CB4, m.u32(0x80039CB4) + 1);
                U inv = U(m.u32(e + 0x388));
                m.w32(inv + 0x14C, m.u32(inv + 0x14C) + 1);
            } else
                m.w32(e + 0xC, m.u32(e + 0xC) & 0xEFFFFFFF);
        }
    } else if (n == "hit_part_reset")
        hit_parts_set(m, e, 0xFFFFFFFF, -1, -1, -1);
    else if (n == "leave_ground")
        m.w32(e + 0x284, 0);  // op 0x12 (0x8005908C): entity_leave_ground (0x80075118)
    else if (n == "remove_self") {
        eng.remove(e, "remove_self");
        m.w32(0x80099EC0, 1);
    } else if (n == "model_variant") {
        U in = U(m.u32(e + 0x100));
        if (in) m.w32(in + 0x68, a[0]);
    } else if (n == "property")
        prop(*this, a[0], a[1], a[2], a[3], a[4]);
    else if (n == "send_event") {
        auto it = eng.ents.find(e);
        eng.logic->send(a[0] & 0xFFFF, it == eng.ents.end() ? nullptr : it->second, 0, nullptr, 0, "send_event");
    } else if (n == "sound")
        sound(*this, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
    else if (n == "root_motion")
        root_motion(eng, e);
    else if (n == "anim") {
        if (a[1] == 1) save_pose(*this);
    } else if (n == "pose_blend")
        save_pose(*this);
    else if (n == "behaviour")
        behaviour(*this, a[0], a[1], a[2], a[3], a[4]);
    else if (n == "alarm")
        op_alarm(eng, e, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
    else
        eng.note("script effect " + n + " not implemented");
}

// ---------------------------------------------------------------- world ops (engine_world_ops.py)

bool VM::world_op(I op, U pc, I w, I mask, I imm, U& next) {
    I imm_lo = imm & 0xFF, imm_hi = imm >> 8;
    auto O = [&](int n, I v) { return operand(n, mask, v); };
    auto W = [&](int k) { return m.s32(pc + 4 * k); };
    if (op == 0x33) {
        I r = compute_node(eng, e, O(2, W(2)), O(3, W(3)), O(4, W(4)));
        set_var(O(1, W(1)), r);
        next = pc + 0x18;
        return true;
    }
    if (op == 0x34) {  // 0x8005B32C: node_eval, the fact in the instruction's low byte
        I r = node_eval(eng, e, U(O(1, W(1)) & 0xFFFFFFFF), w & 0xFF);
        set_var(O(2, W(2)), r);
        next = pc + 0xC;
        return true;
    }
    if (op == 0x35) {
        U wo = U(m.u32(m.u32(0x8009D648) + 0x388));
        I node = O(1, W(1)) & 0xFFFFFFFF, val = O(2, W(2));
        if (node < m.u32(wo + 0xC)) {
            U nd = U(m.u32(wo + 8) + 0x34 * node);
            if (imm_lo == 1)
                m.w32(nd, val);
            else if (imm_lo == 6)
                m.w32(nd + 0x30, val ? m.u32(val) : 0);
        }
        next = pc + 0xC;
        return true;
    }
    if (op == 0x37) {
        I t1 = S32(O(1, W(1)) << 24) >> 24, t2 = S32(O(2, W(2)) << 24) >> 24;
        U tgt = t1 != -1 ? find_type_group(m, t1, t2 != -1 ? t2 : 0) : U(O(3, W(3)) & 0xFFFFFFFF);
        I v1 = O(4, W(4)), v2 = O(5, W(5));
        I k = 0, a = 0;
        if (tgt) route_turn_kind(eng, e, m.s16(tgt + 0x20E), m.s16(tgt + 0x216), k, a);
        set_var(v1, k);
        set_var(v2, a);
        next = pc + 0x18;
        return true;
    }
    if (op == 0x3A) {
        I node = O(1, W(1));
        I v1 = O(2, W(2)), v2 = O(3, W(3));
        NodeTarget got;
        I via = brain_node_target(m, e, got, S32(node));
        I px, pz;
        if (via)
            px = got.side[0], pz = got.side[1];
        else
            px = got.node_point[0], pz = got.node_point[2];
        I k, a;
        route_turn_kind(eng, e, px, pz, k, a);
        set_var(v1, k);
        set_var(v2, a);
        next = pc + 0x10;
        return true;
    }
    if (op == 0x3E) {
        I r = probe(eng, e, O(2, imm_hi), S16(O(3, W(1))));
        set_var(S16(O(1, imm_lo)), r);
        next = pc + 8;
        return true;
    }
    if (op == 0x46) {
        I dist = O(2, imm);
        U tgt = U(O(1, W(1) & 0xFFFF) & 0xFFFFFFFF);
        I off = O(3, W(2) & 0xFFFF);
        I var = O(4, (W(2) >> 16) & 0xFFF);
        I mode = O(5, (W(2) >> 28) & 0xF);
        V3 u;
        I yaw;
        if (mode == 0) {
            u = vnormal(eng.gte, {S16(m.s16(tgt + 0x20E) - m.s16(e + 0x20E)), 0, S16(m.s16(tgt + 0x216) - m.s16(e + 0x216))});
            yaw = S32(I(eng.gte.ratan2(int32_t(u[0]), int32_t(u[2]))) << 16);
        } else if (mode & 1) {
            u = {m.s16(tgt + 0x344), 0, m.s16(tgt + 0x350)};
            yaw = m.s32(tgt + 0x22C);
        } else {
            next = pc + 0xC;
            return true;
        }
        set_var(var, yaw);
        m.w32(e + 0x22C, yaw);
        if (S16(off)) {
            M3 M;
            for (int i = 0; i < 3; i++)
                for (int k = 0; k < 3; k++) M[i][k] = m.s16(tgt + 0x340 + 6 * i + 2 * k);
            V3 T = {m.s32(tgt + 0x354), m.s32(tgt + 0x358), m.s32(tgt + 0x35C)};
            V3 mac = gmul_mac(M, {S16(off), 0, 0}, T, 12);
            V3 p = {S16(mac[0]), S16(mac[1]), S16(mac[2])};
            m.w32(e + 0x20C, S32((((-S16(u[0]) * dist) >> 12) + p[0]) << 16));
            m.w32(e + 0x214, S32((((-S16(u[2]) * dist) >> 12) + p[2]) << 16));
        } else {
            m.w32(e + 0x20C, S32(m.s32(tgt + 0x20C) + (((-S16(u[0]) * dist) >> 12) << 16)));
            m.w32(e + 0x214, S32(m.s32(tgt + 0x214) + (((-S16(u[2]) * dist) >> 12) << 16)));
        }
        next = pc + 0xC;
        return true;
    }
    if (op == 0x36) {
        U obj = U(m.u32(m.u32(0x8009D648) + 0x388));
        I node = O(1, W(1)) & 0xFFFFFFFF, var = O(2, W(2)), how = O(3, W(3)) & 0xFFFFFFFF;
        if (node < m.u32(obj + 0xC)) {
            U n = U(m.u32(obj + 8) + 0x34 * node);
            U br = U(m.u32(e + 0x38C));
            if (m.s32(n + 4)) {
                m.w16(e + 0x2E8, S16(S32(m.u16(br + 0xC6) << 16) >> 17));
                m.w16(e + 0x3C8, S16(S32(m.u16(br + 0xCA) << 16) >> 17));
            }
            if (how == 0 || (how & 1)) {
                m.w32(e + 0x20C, S32(m.s16(br + 0xA0) << 16));
                m.w32(e + 0x214, S32(m.s16(br + 0xA4) << 16));
            }
            bool have = false;
            I a = 0;
            if (how == 0 || (how & 2))
                a = S32(m.s16(n + 0x1E) << 16), have = true;
            else if (how & 4)
                a = S32(I(eng.gte.ratan2(int32_t(m.s16(n + 0x24)), int32_t(m.s16(n + 0x28)))) << 16), have = true;
            if (have) {
                m.w32(e + 0x22C, a);
                set_var(var, a);
            }
            m.w32(e + 0x10, m.u32(e + 0x10) | 0x80);
        }
        next = pc + 0x10;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- one instruction

U VM::step(U pc, I chn, U c) {
    I w = m.u32(pc);
    I op = w >> 24;
    I mask = (w >> 16) & 0xFF, imm = w & 0xFFFF;
    if (op == 0x02) {
        I a[7];
        a[0] = operand(1, mask, imm & 0xFF);
        for (int k = 0; k < 5; k++) a[k + 1] = operand(k + 2, mask, m.s32(pc + 4 * (k + 1)));
        a[6] = (imm >> 8) & 0xFF;
        ch = chn, ctx = c, yielded = 0;  // op_sound does not yield
        effect("sound", a, 7);
        return pc + 0x18;
    }
    if (op == 0x33 || op == 0x37 || op == 0x3A || op == 0x3E || op == 0x34 || op == 0x35 || op == 0x36 || op == 0x46) {
        ch = chn, ctx = c, yielded = 0;
        U next;
        if (world_op(op, pc, w, mask, imm, next)) return next;
    }
    U npc = base_step(pc, chn, c);
    if (op == 0x00) root_motion(eng, e);
    return npc;
}

U VM::base_step(U pc, I chn, U c) {
    ch = chn, ctx = c, yielded = 0;
    I w = m.u32(pc);
    I op = w >> 24, mask = (w >> 16) & 0xFF;
    I imm = w & 0xFFFF, imm_lo = w & 0xFF, imm_hi = (w >> 8) & 0xFF;
    auto W = [&](int k) { return m.s32(pc + 4 * k); };
    auto H = [&](int off) { return m.s16(pc + off); };
    auto HU = [&](int off) { return m.u16(pc + off); };
    auto O = [&](int n, I v) { return operand(n, mask, v); };
    U in = inst + 0x5C;
    switch (op) {
        case 0x00: {
            I anim = imm & 0xFFF, mode = S16(imm) >> 12;
            I a = HU(4), b = H(6);
            m.w16(in, anim);
            m.w16(in + 2, a);
            m.w16(in + 4, imax(S16(a), 3));
            if (b == -1) {
                if (m.u32(inst + 0x74))
                    b = m.u8(m.u32(m.u32(inst + 0x78) + 4 * anim)) - 1;
                else
                    b = m.u16(m.u32(model + 0x98) + 16 * anim + 4) - 1;
            } else if (b == S16(a))
                b = -1;
            m.w16(c + 0x18, b);
            if (mode == 1) {
                m.w16(inst + 0x3E, 4);
                m.w16(inst + 0x40, -1);
            } else if (mode == 2)
                m.w16(inst + 0x3E, 0);
            else if (m.s16(inst + 0x3E) > 0)
                m.w16(inst + 0x3E, m.s16(inst + 0x3E) - 1);
            I args[4] = {anim, mode, S16(a), H(6)};
            effect("anim", args, 4);
            m.w16(c + 0x1C, 0);
            for (U k : {0x240u, 0x244u, 0x248u}) m.w32(e + k, 0);
            yielded = 1;
            return pc + 8;
        }
        case 0x01: {
            I v = O(1, W(1));
            I words[4];
            for (int k = 0; k < 4; k++) words[k] = m.u32(c + 4 * k);
            if (imm_lo == 0)
                for (I& x : words) x &= ~v;
            else if (1 <= imm_lo && imm_lo <= 4)
                for (int k = 0; k < 4; k++) words[k] = k == imm_lo - 1 ? (words[k] | v) : (words[k] & ~v);
            for (int k = 0; k < 4; k++) m.w32(c + 4 * k, words[k]);
            return pc + 8;
        }
        case 0x03: m.w32(e + 0x1FC, O(1, W(1))); return pc + 8;
        case 0x04:
            if (!(w & 4)) m.w32(e + 0x200, O(1, W(1)));
            if (!(w & 8)) m.w32(e + 0x21C, O(2, W(2)));
            if (w & 2) return pc + 0xC;
            if (!(w & 0x10)) m.w32(e + 0x218, O(3, W(3)));
            return pc + 0x10;
        case 0x05: {
            if (!(w & 2)) return pc + 0x10;
            m.w32(e + 0x21C, m.s32(e + 0x21C) + O(2, W(2)));
            I v = m.s32(e + 0x200) + O(1, W(1));
            I cap = m.s32(e + 0x204);
            if (cap) v = imin(v, cap);
            m.w32(e + 0x200, v);
            return pc + 0xC;
        }
        case 0x06: {
            if (!(w & 2)) return pc + 0x10;
            I d = O(1, W(1));
            if ((w & 1) && m.s32(e + 0x288) < 0) d = -d;
            m.w32(e + 0x20C, m.s32(e + 0x20C) + d);
            m.w32(e + 0x210, m.s32(e + 0x210) + O(2, W(2)));
            return pc + 0xC;
        }
        case 0x07:
        case 0x26: {
            U npc = U(code_base(c) + imm * 4);
            m.w32(c + 0x14, npc);
            return npc;
        }
        case 0x08:
        case 0x1D:
            if (script_start(O(1, imm), op == 0x1D, chn)) return U(m.u32(c + 0x14));
            return pc + 4;
        case 0x09:
        case 0x0B: eng.note("script op 09/0B (hang)"); return 0;
        case 0x0A:
            m.w32(e + 0x22C, m.s32(e + 0x22C) + 0x08000000);
            m.w32(e + 0x28C, m.u32(e + 0x288));
            return pc + 4;
        case 0x0C: {
            I a2 = (w & 2) ? -1 : (w & 1) ? O(2, W(2)) : 0;
            I a3 = (w & 8) ? -1 : (w & 4) ? O(3, W(3)) : 0;
            I a4 = (w & 0x20) ? -1 : (w & 0x10) ? O(4, W(4)) : 0;
            I args[4] = {O(1, W(1)), a2, a3, a4};
            effect("hit_part", args, 4);
            return pc + 0x14;
        }
        case 0x0D:
        case 0x0E:
        case 0x42: return sop_event(pc, op);
        case 0x0F: return pc + 4;
        case 0x10:
        case 0x11: {
            U a = e + (imm_lo ? 0x10 : 0xC);
            I v = O(1, W(1));
            m.w32(a, op == 0x10 ? (m.u32(a) | v) : (m.u32(a) & ~v));
            return pc + 8;
        }
        case 0x12: effect("leave_ground", nullptr, 0); return pc + 4;
        case 0x13: set_var(O(1, imm), O(2, W(1))); return pc + 8;
        case 0x14: {
            I idx = S16(O(1, W(1)));
            m.w16(script_entry(idx) + 4, O(2, imm));
            return pc + 8;
        }
        case 0x15: {
            I a[1] = {O(1, imm_lo)};
            effect("spawn", a, 1);
            return pc + 8;
        }
        case 0x16: m.w16(c + 0x1C, O(1, imm) - 1); yielded = 1; return pc + 4;
        case 0x17: effect("remove_self", nullptr, 0); yielded = 1; return pc + 4;
        case 0x18:
        case 0x19: {
            U base = U(e + (op == 0x18 ? 0x2A4 : 0x2AC) + 16 * S16(O(1, imm)));
            for (int k = 0; k < 3; k++) m.w16(base + 2 * k, O(k + 2, W(k + 1)));
            return pc + 0x10;
        }
        case 0x1A:
            for (int k = 0; k < 3; k++) m.w8(e + 0x2A0 + k, O(k + 1, W(k + 1)));
            return pc + 0x10;
        case 0x1B: effect("op1b", nullptr, 0); return pc + 4;
        case 0x1C: {
            I a[1] = {O(1, imm_lo)};
            effect("model_variant", a, 1);
            return pc + 4;
        }
        case 0x1E: {
            I r = rand();
            I chance = O(4, imm_hi);
            I var = O(1, imm_lo);
            I lo = O(2, H(4)), hi = O(3, H(6));
            if (chance < cmod(r, 100) && chance != 0)
                set_var(var, -1);
            else {
                I r2 = rand();
                I span = hi - lo + 1;
                set_var(var, lo + (span ? cmod(r2, span) : 0));
            }
            return pc + 8;
        }
        case 0x1F: {
            I a[5] = {imm_lo, O(1, W(1)), O(2, W(2)), O(3, imm_hi), O(4, W(3))};
            effect("property", a, 5);
            return pc + 0x10;
        }
        case 0x20: effect("clear_input", nullptr, 0); return pc + 4;
        case 0x21: {
            I r = rand();
            I var = O(1, imm_lo);
            I div = (O(4, imm_hi) * 0x1E0) & 0xFFFFFFFF;
            I lo = O(2, W(1)), hi = O(3, W(2));
            if ((div ? pmod(r, div) : r) < 16) {
                I r2 = rand();
                I span = hi - lo + 1;
                set_var(var, lo + (span ? cmod(r2, span) : 0));
            } else
                set_var(var, -1);
            return pc + 0xC;
        }
        case 0x22:
        case 0x23:
        case 0x24:
        case 0x25: {
            U base = e + ((op == 0x22 || op == 0x23) ? 0x234 : 0x228);
            for (int k = 0; k < 3; k++) {
                I v = O(k + 1, W(k + 1));
                if (op == 0x23 || op == 0x25) v += m.s32(base + 4 * k);
                m.w32(base + 4 * k, v);
            }
            return pc + 0x10;
        }
        case 0x27: {
            I a = O(1, W(1)), b = O(3, W(2));
            I t = HU(0xC), f = HU(0xE);
            I cmp = S16(imm);
            I j;
            if (cmp == 1)
                j = a == b ? t : f;
            else if (cmp == 2)
                j = b < a ? t : f;
            else if (cmp == 3)
                j = a < b ? t : f;
            else if (cmp == 4)
                j = a == b ? f : t;
            else {
                eng.note("script if with an unknown comparison");
                return 0;
            }
            return U(code_base(c) + 4 * j);
        }
        case 0x28:
        case 0x29:
        case 0x2A: {
            I var = O(1, W(1)), v = O(2, W(2));
            I old = get_var(var);
            set_var(var, op == 0x28 ? (old ^ v) : op == 0x29 ? (old | v) : (old & v));
            return pc + 0xC;
        }
        case 0x2B: return pc + 0xC;
        case 0x2C:
        case 0x2D:
        case 0x30: {
            I var = O(1, W(1)), v = O(2, W(2));
            U a = U(e + 0x4C + 4 * var);
            I old = m.s32(a);
            m.w32(a, op == 0x2C ? old + v : op == 0x2D ? old - v : old * v);
            return pc + 0xC;
        }
        case 0x2E: return op_query(pc, mask, w);
        case 0x2F: {
            I a[5] = {O(1, W(1)), O(2, W(2)), O(3, H(0xC)), O(4, H(0xE)), O(5, HU(0))};
            effect("behaviour", a, 5);
            return pc + 0x10;
        }
        case 0x31: return 0;
        case 0x32: {
            I var = O(1, W(1));
            set_var(var, nearest_vm(O(2, W(2))));
            return pc + 0xC;
        }
        case 0x34: {
            I node = O(1, W(1)), var = O(2, W(2));
            U wo = U(m.u32(m.u32(G_WORLD) + 0x388));
            if ((node & 0xFFFFFFFF) >= m.u32(wo + 0xC))
                set_var(var, -1);
            else if (1 <= imm_lo && imm_lo <= 5)
                set_var(var, m.s32(m.u32(wo + 8) + 0x34 * node + 4 * (imm_lo - 1)));
            else if (imm_lo == 6 || imm_lo == 7)
                set_var(var, world(op, var));
            return pc + 0xC;
        }
        case 0x38: {
            I d = m.s16(c + 0x1E) + 1;
            m.w16(c + 0x1E, d);
            if (d >= 6) {
                m.w16(c + 0x1E, 5);
                effect("call_overflow", nullptr, 0);
                return pc + 4;
            }
            U s = script_entry(S16(O(1, imm)));
            U f = U(c + 0x14 + 16 * d);
            m.w32(f, pc + 4);
            m.w32(f + 4, m.u32(m.u32(c + 0x10) + 0xC));
            m.w32(f + 8, m.u32(s + 0xC));
            m.w16(f + 0xC, m.u16(s + 0xA));
            m.w32(c + 0x14, m.u32(s + 0xC));
            return U(m.u32(s + 0xC));
        }
        case 0x39: {
            I d = m.s16(c + 0x1E) - 1;
            m.w16(c + 0x1E, d);
            if (d >= 0) {
                U r = U(m.u32(c + 0x24 + 16 * d));
                m.w32(c + 0x14, r);
                return r;
            }
            m.w16(c + 0x1E, 0);
            return pc + 4;
        }
        case 0x3B:
            if (m.s16(e + 0x192) != S16(imm)) script_start(O(1, S16(imm)), 0, 1);
            return pc + 4;
        case 0x3C:
            m.w16(e + 0x192, -1);
            m.w16(e + 0x190, -1);
            m.w32(e + 0x188, 0);
            return pc + 4;
        case 0x3D: set_var(O(1, imm), m.s16(e + 0x192)); return pc + 4;
        case 0x3F:
        case 0x40: {
            I t = O(1, W(1));
            if (t) {
                U a = U((t & 0xFFFFFFFF) + (op == 0x3F ? 0x3C : 0x38));
                m.w32(a, m.u32(a) | O(2, W(2)));
            }
            return pc + 0xC;
        }
        case 0x41: m.w32(inst + 0x6C, O(1, imm_lo)); return pc + 4;
        case 0x43: {
            I a[1] = {O(1, imm)};
            effect("send_event", a, 1);
            return pc + 4;
        }
        case 0x44:
            if (imm_hi < 2) {
                m.w32(e + 0x20C, m.s16(e + 0x3A4) << 16);
                m.w32(e + 0x214, m.s16(e + 0x3A8) << 16);
            }
            if (imm_hi == 0 || imm_hi == 2) {
                m.w32(e + 0x22C, m.s16(e + 0x3AE) << 16);
                set_var(O(1, imm_lo), m.s32(e + 0x22C));
            }
            return pc + 4;
        case 0x45: {
            I var = O(1, W(1)), anim = O(2, W(2));
            m.w32(e + 0x4C + 4 * var, m.u32(m.u32(inst + 0x78) + 4 * anim) != 0 ? 1 : 0);
            return pc + 0xC;
        }
        case 0x47: {
            U t = U(m.u32(c + 0x20));
            for (I i = 0; i < m.u32(e + 0x1EC); i++) m.w32(t + 0x3C * i, 0);
            return pc + 4;
        }
        case 0x48: effect("run_message", nullptr, 0); return pc + 8;
        case 0x49: {
            I a[7];
            for (int k = 0; k < 7; k++) a[k] = operand(k + 1, mask, m.s32(pc + 4 * U(k + 1)));
            effect("alarm", a, 7);
            return pc + 0x20;
        }
        // 0x33, 0x35, 0x36, 0x37, 0x3A, 0x3E, 0x46: the world ops above
        default: eng.note("script op " + std::to_string(op) + " unknown"); return 0;
    }
}

// ---------------------------------------------------------------- the two runners

void VM::run_b() {
    if (m.s32(e + 0x44) > 0) {
        m.w32(e + 0x38, m.u32(e + 0x38) & ~I(0x600));
        m.w32(e + 0x44, m.s32(e + 0x44) - 1);
    }
    if (m.s32(e + 0x48) > 0) {
        m.w32(e + 0x38, m.u32(e + 0x38) & 0xFFE7FE1F);
        m.w32(e + 0x48, m.s32(e + 0x48) - 1);
    }
    I w38 = m.u32(e + 0x38), w3c = m.u32(e + 0x3C), w40 = m.u32(e + 0x40);
    bool matched = false;
    for (I chn = 0; chn < 2; chn++) {
        U c = U(e + 0x104 + 0x74 * chn);
        if (!m.u32(c + 0x10)) continue;
        for (I i = 0; i < m.u32(e + 0x1EC); i++) {
            U r = U(m.u32(c + 0x20) + 0x3C * i);
            I cc = m.u32(r);
            if (cc == 0) {
                if (matched) break;
                continue;
            }
            if (cc & 3) continue;
            bool ok = true;
            struct T {
                I bit;
                U off;
                I word;
                bool all;
            } tests[6] = {{4, 0x10, w38, true}, {8, 0x14, w38, false}, {0x10, 0x18, w3c, true},
                          {0x20, 0x1C, w3c, false}, {0x40, 0x20, w40, true}, {0x80, 0x24, w40, false}};
            for (const T& t : tests) {
                if (cc & t.bit) {
                    I v = m.u32(r + t.off);
                    if ((t.all && (v & t.word) != v) || (!t.all && (v & t.word))) {
                        ok = false;
                        break;
                    }
                }
            }
            if (!ok) continue;
            I stored = m.u8(r + 0x2B);
            if (stored == 0x26) {
                m.w16(c + 0x18, -1);
                m.w16(c + 0x1C, 0);
                m.w16(c + 0x1E, 0);
                m.w32(c + 0x14, m.u32(m.u32(c + 0x10) + 0xC) + 4 * m.u16(r + 0x28));
            } else if (stored != 0x38)
                step(r + 0x28, chn, c);
            matched = true;
            break;
        }
    }
}

void VM::run_a() {
    for (I chn = 0; chn < 2; chn++) {
        U c = U(e + 0x104 + 0x74 * chn);
        if (!m.u32(c + 0x10)) {
            if (chn == 0) script_start(0, 0, 0);
            continue;
        }
        if (m.s16(c + 0x1C) > 0) {
            m.w16(c + 0x1C, m.s16(c + 0x1C) - 1);
            continue;
        }
        if (m.s16(inst + 0x5E) < m.s16(c + 0x18)) {
            m.w16(inst + 0x5E, m.s16(inst + 0x5E) + 1);
            if (m.s16(inst + 0x3E) > 0) m.w16(inst + 0x3E, m.s16(inst + 0x3E) - 1);
            m.w16(c + 0x1C, 0);
            ch = chn;
            effect("root_motion", nullptr, 0);
            continue;
        }
        m.w16(c + 0x18, -1);
        int steps = 0;
        for (;;) {
            U pc = U(m.u32(c + 0x14));
            if ((S32(pc - code_base(c)) >> 2) >= code_len(c)) {
                m.w32(c + 0x10, 0);
                m.w16(c + 0x1A, -1);
                m.w16(inst + 0x5C, -1);
                m.w16(inst + 0x5E, 0);
                if (chn != 0) break;
                I r = rand();
                I roll = cdiv(r * 100, 32767);
                U lst = U(m.u32(model + 0xB8));
                I n = m.u16(model + 0xBC);
                bool done = false;
                for (I i = 0; i < n; i++)
                    if (roll < m.u16(lst + 16 * i + 4)) {
                        script_start(i, 0, 0);
                        done = true;
                        break;
                    }
                if (!done) script_start(0, 0, 0);
                continue;
            }
            U npc = step(pc, chn, c);
            m.w32(c + 0x14, npc);
            if (yielded) break;
            if (++steps > 5000) {
                eng.note("script: no yield after 5000 instructions");
                break;
            }
        }
    }
}

}  // namespace valor::sim
