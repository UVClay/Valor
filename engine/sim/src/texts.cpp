// The on-screen text list (docs/re/hud.md, Messages: the text list): text_add (0x8005F28C),
// text_add_fade (0x8005F3E8), text_remove (0x8005F5A0), texts_remove_id (0x8005F60C),
// texts_update (0x8005F69C, every tick after the collisions), the level's show_text
// (0x80053464), the pickups' messages and the HUD's ammo strings; the hit wedges' timers. The list
// is kept where the game keeps it (50 entries of 32 bytes at 0x80099F4C, the count at 0x80099ED0),
// so the seed's permanent entries (the ammo digits) carry on. tools/sim/engine_texts.py.
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/simgte.h"

namespace valor::sim {

namespace {
constexpr U G_TEXTS = 0x80099F4C;
constexpr U G_TEXT_COUNT = 0x80099ED0;
constexpr U G_MSG_TABLE = 0x80098188;
constexpr U G_MSG_LINE_Y = 0x80098628;
constexpr U G_MSG_LAST = 0x8009862C;
constexpr U G_HIT_TIMER = 0x8009A590;  // right, left, front, behind (hud.md, Hit wedges)

// text_add: a new last entry (none past 50: -1); id 0xFE draws an id 120..247 not in use (draws
// by text_add); hold -2 (no colour change). Returns the index.
I text_add(Engine& eng, U text, I x, I y, I r, I g, I b, I semi, I life, I tid, I mask) {
    Mem& m = eng.m;
    I n = m.s32(G_TEXT_COUNT);
    if (n >= 0x32) return -1;
    U a = G_TEXTS + 0x20 * U(n);
    m.w32(a, text);
    m.w16(a + 4, x), m.w16(a + 6, y);
    m.w8(a + 8, r), m.w8(a + 9, g), m.w8(a + 0xA, b), m.w8(a + 0xB, semi);
    m.w32(a + 0xC, life);
    if ((tid & 0xFF) == 0xFE) {
        auto rd = [&]() { return (eng.rand.draw({0x8005F28C}) & 0x7F) + 0x78; };
        I t = rd();
        I i = 0;
        while (i < m.s32(G_TEXT_COUNT)) {
            i++;
            if (m.u8(G_TEXTS + 0x20 * U(i - 1) + 0x1F) == (t & 0xFF)) i = 0, t = rd();
        }
        m.w8(G_TEXTS + 0x20 * U(i) + 0x1F, t);
    } else
        m.w8(a + 0x1F, tid);
    m.w32(a + 0x10, -2);
    m.w32(a + 0x18, mask);
    m.w32(G_TEXT_COUNT, n + 1);
    return n;
}

// text_add_fade: text_add, then the hold (+0x10), the fade-in ticks (+0x14; negative: -target
// level, the fewest ticks for any channel to reach it from its start at its step) and the
// per-tick steps (+0x1C..).
I text_add_fade(Engine& eng, U text, I x, I y, I r, I g, I b, I semi, I life, I tid, I mask, I hold, I fade, I sr, I sg, I sb) {
    Mem& m = eng.m;
    I i = text_add(eng, text, x, y, r & 0xFF, g & 0xFF, b & 0xFF, semi, life, tid, mask);
    if (i == -1) return -1;
    if (fade < 0) {
        I t = -fade;
        fade = imin(imin((t - b) / sb, (t - g) / sg), (t - r) / sr);
    }
    U a = G_TEXTS + 0x20 * U(i);
    m.w32(a + 0x14, fade);
    m.w8(a + 0x1C, sr), m.w8(a + 0x1D, sg), m.w8(a + 0x1E, sb);
    m.w32(a + 0x10, hold);
    return i;
}

// text_remove: the last entry moves into the slot.
void text_remove(Mem& m, I i) {
    I n = m.s32(G_TEXT_COUNT) - 1;
    m.w32(G_TEXT_COUNT, n);
    m.copy(G_TEXTS + 0x20 * U(i), G_TEXTS + 0x20 * U(n), 0x20);
}

void texts_remove_id(Mem& m, I tid) {
    I i = 0;
    while (i < m.s32(G_TEXT_COUNT)) {
        if (m.u8(G_TEXTS + 0x20 * U(i) + 0x1F) == (tid & 0xFF))
            text_remove(m, i);
        else
            i++;
    }
}

// ammo_digits (0x80046700, buf, value, 0, width 2|3): space-padded decimal digits; a zero tens
// digit after a hundreds digit is written as 0.
void ammo_digits(Mem& m, U buf, I value, I width) {
    I pos = 0, v = value;
    if (width == 3) {
        if (v >= 100) {
            m.w8(buf, value / 100 + 0x30);
            v = value % 100;
        } else
            m.w8(buf, 0x20);
        pos = 1;
    }
    if (!(v < 10)) {
        m.w8(buf + U(pos), v / 10 + 0x30);
        v -= v / 10 * 10;
    } else if (pos && m.u8(buf + U(pos) - 1) != 0x20 && width != 2)
        m.w8(buf + U(pos), 0x30);
    else
        m.w8(buf + U(pos), 0x20);
    m.w8(buf + U(pos) + 1, (v + 0x30) & 0xFF);
}
}  // namespace

// texts_update: lifetimes count down (-1 forever; at 0 removed, the last moving in, which is then
// looked at in the same slot); then each text with a hold >= 0: hold > 0 counts down and, while
// fade-in > 0 (0x7FFF endless), the colour rises by its steps (to 255) and fade-in -1; hold 0:
// the colour falls by its steps (to 0).
void texts_update(Engine& eng) {
    Mem& m = eng.m;
    I i = 0;
    while (i < m.s32(G_TEXT_COUNT)) {
        U a = G_TEXTS + 0x20 * U(i);
        I life = m.s32(a + 0xC);
        if (life != -1) {
            life--;
            m.w32(a + 0xC, life);
            if (life <= 0) {
                text_remove(m, i);
                continue;
            }
        }
        i++;
    }
    for (i = 0; i < m.s32(G_TEXT_COUNT); i++) {
        U a = G_TEXTS + 0x20 * U(i);
        I hold = m.s32(a + 0x10);
        if (hold < 0) continue;
        if (hold > 0) {
            m.w32(a + 0x10, hold - 1);
            I f = m.s32(a + 0x14);
            if (f > 0) {
                if (f != 0x7FFF) m.w32(a + 0x14, f - 1);
                for (U k = 0; k < 3; k++) m.w8(a + 8 + k, imin(m.u8(a + 8 + k) + m.u8(a + 0x1C + k), 0xFF));
            }
        } else
            for (U k = 0; k < 3; k++) m.w8(a + 8 + k, imax(m.u8(a + 8 + k) - m.u8(a + 0x1C + k), 0));
    }
}

// show_text (level built-in 8): message n of the table 0x80098188 at x 20 on the message line,
// (128, 128, 128) additive, 100 ticks, id 0xFF, viewport 3; a message other than the last moves
// the line down 12 (from 157 back to 120).
void show_text(Engine& eng, I n) {
    Mem& m = eng.m;
    if (m.s32(G_TEXT_COUNT) >= 0x32) return;
    if (n != m.s32(G_MSG_LAST)) {
        m.w32(G_MSG_LAST, n);
        I y = m.s32(G_MSG_LINE_Y) + 0xC;
        m.w32(G_MSG_LINE_Y, y >= 0x9D ? 0x78 : y);
    }
    text_add(eng, U(m.u32(G_MSG_TABLE + 4 * U(n))), 0x14, m.s32(G_MSG_LINE_Y), 0x80, 0x80, 0x80, 1, 0x64, 0xFF, 3);
}

// The pickups' message (health, ammo): id 2 replaces the last one; at (10, 70), additive, 40
// ticks, held 40, fading in from black over 11 ticks by (10, 8, 4) a tick, in the picker's
// viewport.
void pickup_message(Engine& eng, U e, U text) {
    Mem& m = eng.m;
    texts_remove_id(m, 2);
    if (!text) return;
    I mask = 1 << m.u8(U(m.u32(e + 0x388)) + 0x152);
    text_add_fade(eng, text, 0xA, 0x46, 0, 0, 0, 1, 0x28, 2, mask, 0x28, 0xB, 0xA, 8, 4);
}

// ammo_hud_update (0x800467EC): the HUD's clip (width 2, inv+0x108) and reserve (width 3,
// inv+0x10C) strings, rewritten in place (the text list points at them); empty for the papers
// (weapon 7).
void ammo_hud_update(Engine& eng, U e) {
    Mem& m = eng.m;
    U inv = U(m.u32(e + 0x388));
    I slot = m.s8(inv + 0x54);
    if (m.u8(0x8009E878 + slot) == 7) {
        m.w32(inv + 0x108, 0);
        m.w32(inv + 0x10C, 0);
        return;
    }
    ammo_digits(m, inv + 0x108, m.s16(inv + 0x66 + 2 * slot), 2);
    ammo_digits(m, inv + 0x10C, m.s16(inv + 0x56 + 2 * slot), 3);
}

// ammo_hud_init (0x80046874, at level load): the ammo icon's quad in the weapon block (+0xBB..
// +0x102: drawing), the strings' ends, ammo_hud_update, then the two permanent digit texts: the
// reserve at (453, 18), the clip at (380, 18), (100, 100, 100), opaque, forever, id 0xFF, in the
// soldier's viewport. ammo_hud_icon (0x80046574: the icon's texture by slot, then ammo_hud_update
// again) is drawing but for that.
void ammo_hud_init(Engine& eng, U e) {
    Mem& m = eng.m;
    U inv = U(m.u32(e + 0x388));
    for (U o : {0xBFu, 0xE7u}) m.w8(inv + o, 0x2C);
    for (U o : {0xBBu, 0xE3u}) m.w8(inv + o, 9);
    for (U o : {0xBCu, 0xBDu, 0xBEu, 0xE4u, 0xE5u, 0xE6u}) m.w8(inv + o, 0x80);
    for (U o : {0xC0u, 0xD0u, 0xE8u, 0xF8u}) m.w16(inv + o, 0x19F);
    for (U o : {0xC8u, 0xD8u, 0xF0u, 0x100u}) m.w16(inv + o, 0x1BF);
    for (U o : {0xC2u, 0xCAu, 0xEAu, 0xF2u, 0xF6u, 0xCEu}) m.w16(inv + o, 0xF);
    for (U o : {0xD2u, 0xDAu, 0xFAu, 0x102u}) m.w16(inv + o, 0x23);
    m.w8(inv + 0x10A, 0);
    m.w8(inv + 0x10F, 0);
    ammo_hud_update(eng, e);
    I mask = 1 << m.u8(inv + 0x152);
    text_add(eng, inv + 0x10C, 0x1C5, 0x12, 100, 100, 100, 0, -1, 0xFF, mask);
    text_add(eng, inv + 0x108, 0x17C, 0x12, 100, 100, 100, 0, -1, 0xFF, mask);
    ammo_hud_update(eng, e);
}

// hit_indicator (0x80060FC4): the attacker's last move (+0x258 - +0x20C, whole units) turned into
// the player's view (RotMatrixYXZ(-pitch +0x22A, -view yaw inv+0x186, -roll +0x232), GTE >> 12):
// |x| > |z|: x < 0 the left timer, x > 0 the right; else z < 0 behind, z > 0 front; set to 40.
void hit_indicator(Engine& eng, U victim, U attacker) {
    Mem& m = eng.m;
    U inv = U(m.u32(victim + 0x388));
    M3 r = rot_yxz3(S16(-m.s16(victim + 0x22A)), S16(-m.s16(inv + 0x186)), S16(-m.s16(victim + 0x232)));
    V3 v;
    for (U k = 0; k < 3; k++) v[k] = S16((m.s32(attacker + 0x258 + 4 * k) - m.s32(attacker + 0x20C + 4 * k)) >> 16);
    V3 mac = gmul_mac(r, v);
    I x = mac[0], z = mac[2];
    if (iabs(z) < iabs(x)) {
        if (x < 0) m.w32(G_HIT_TIMER + 4, 0x28);
        if (x > 0) m.w32(G_HIT_TIMER, 0x28);
    } else {
        if (z < 0) m.w32(G_HIT_TIMER + 0xC, 0x28);
        if (z > 0) m.w32(G_HIT_TIMER + 8, 0x28);
    }
}

// The HUD's hit wedges (0x80060B04, every drawn frame): each timer that is running drops by 1 +
// g_catchup_ticks (the frame's ticks beyond the first), not below 0 (hud.md).
void hit_wedges(Engine& eng) {
    Mem& m = eng.m;
    I step = 1 + imax(eng.frame_ticks - 1, 0);
    for (U k = 0; k < 4; k++) {
        U a = G_HIT_TIMER + 4 * k;
        I t = m.s32(a);
        if (t) m.w32(a, imax(t - step, 0));
    }
}

}  // namespace valor::sim
