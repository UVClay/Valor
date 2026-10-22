"""The on-screen text list (docs/re/hud.md, Messages: the text list): text_add (0x8005F28C),
text_add_fade (0x8005F3E8), text_remove (0x8005F5A0), texts_remove_id (0x8005F60C), texts_update
(0x8005F69C, every tick after the collisions) and the level's show_text (0x80053464). The list is
kept where the game keeps it (50 entries of 32 bytes at 0x80099F4C, the count at 0x80099ED0), so
the seed's permanent entries (the ammo digits) carry on."""
from engine import s32

G_TEXTS = 0x80099F4C
G_TEXT_COUNT = 0x80099ED0
G_MSG_TABLE = 0x80098188
G_MSG_LINE_Y = 0x80098628
G_MSG_LAST = 0x8009862C


def text_add(eng, text, x, y, r, g, b, semi, life, tid, mask):
    """text_add: a new last entry (none past 50: -1); id 0xFE draws an id 120..247 not in use
    (draws by text_add); hold -2 (no colour change). Returns the index."""
    m = eng.m
    n = m.s32(G_TEXT_COUNT)
    if n >= 0x32:
        return -1
    a = G_TEXTS + 0x20 * n
    m.w32(a, text)
    m.w16(a + 4, x)
    m.w16(a + 6, y)
    m.w8(a + 8, r)
    m.w8(a + 9, g)
    m.w8(a + 0xA, b)
    m.w8(a + 0xB, semi)
    m.w32(a + 0xC, life)
    if tid & 0xFF == 0xFE:
        rd = lambda: (eng.rand.draw(0x8005F28C) & 0x7F) + 0x78
        t = rd()
        i = 0
        while i < m.s32(G_TEXT_COUNT):
            i += 1
            if m.u8(G_TEXTS + 0x20 * (i - 1) + 0x1F) == t & 0xFF:
                i = 0
                t = rd()
        m.w8(G_TEXTS + 0x20 * i + 0x1F, t)
    else:
        m.w8(a + 0x1F, tid)
    m.w32(a + 0x10, -2)
    m.w32(a + 0x18, mask)
    m.w32(G_TEXT_COUNT, n + 1)
    return n


def text_add_fade(eng, text, x, y, r, g, b, semi, life, tid, mask, hold, fade, sr, sg, sb):
    """text_add_fade: text_add, then the hold (+0x10), the fade-in ticks (+0x14; negative: -target
    level, the fewest ticks for any channel to reach it from its start at its step) and the
    per-tick steps (+0x1C..)."""
    m = eng.m
    i = text_add(eng, text, x, y, r & 0xFF, g & 0xFF, b & 0xFF, semi, life, tid, mask)
    if i == -1:
        return -1
    if fade < 0:
        t = -fade
        fade = min(int((t - b) / sb), int((t - g) / sg), int((t - r) / sr))
    a = G_TEXTS + 0x20 * i
    m.w32(a + 0x14, fade)
    m.w8(a + 0x1C, sr)
    m.w8(a + 0x1D, sg)
    m.w8(a + 0x1E, sb)
    m.w32(a + 0x10, hold)
    return i


def text_remove(m, i):
    """text_remove: the last entry moves into the slot."""
    n = m.s32(G_TEXT_COUNT) - 1
    m.w32(G_TEXT_COUNT, n)
    m.copy(G_TEXTS + 0x20 * i, G_TEXTS + 0x20 * n, 0x20)


def texts_remove_id(eng, tid):
    m = eng.m
    i = 0
    while i < m.s32(G_TEXT_COUNT):
        if m.u8(G_TEXTS + 0x20 * i + 0x1F) == tid & 0xFF:
            text_remove(m, i)
        else:
            i += 1


def texts_update(eng):
    """texts_update: lifetimes count down (-1 forever; at 0 removed, the last moving in, which is
    then looked at in the same slot); then each text with a hold >= 0: hold > 0 counts down and,
    while fade-in > 0 (0x7FFF endless), the colour rises by its steps (to 255) and fade-in -1;
    hold 0: the colour falls by its steps (to 0)."""
    m = eng.m
    i = 0
    while i < m.s32(G_TEXT_COUNT):
        a = G_TEXTS + 0x20 * i
        life = m.s32(a + 0xC)
        if life != -1:
            life -= 1
            m.w32(a + 0xC, life)
            if life <= 0:
                text_remove(m, i)
                continue
        i += 1
    for i in range(m.s32(G_TEXT_COUNT)):
        a = G_TEXTS + 0x20 * i
        hold = m.s32(a + 0x10)
        if hold < 0:
            continue
        if hold > 0:
            m.w32(a + 0x10, hold - 1)
            f = m.s32(a + 0x14)
            if f > 0:
                if f != 0x7FFF:
                    m.w32(a + 0x14, f - 1)
                for k in range(3):
                    m.w8(a + 8 + k, min(m.u8(a + 8 + k) + m.u8(a + 0x1C + k), 0xFF))
        else:
            for k in range(3):
                m.w8(a + 8 + k, max(m.u8(a + 8 + k) - m.u8(a + 0x1C + k), 0))


def show_text(eng, n):
    """show_text (level built-in 8): message n of the table 0x80098188 at x 20 on the message
    line, (128, 128, 128) additive, 100 ticks, id 0xFF, viewport 3; a message other than the last
    moves the line down 12 (from 157 back to 120)."""
    m = eng.m
    if m.s32(G_TEXT_COUNT) >= 0x32:
        return
    if n != m.s32(G_MSG_LAST):
        m.w32(G_MSG_LAST, n)
        y = m.s32(G_MSG_LINE_Y) + 0xC
        m.w32(G_MSG_LINE_Y, 0x78 if y >= 0x9D else y)
    text_add(eng, m.u32(G_MSG_TABLE + 4 * n), 0x14, m.s32(G_MSG_LINE_Y), 0x80, 0x80, 0x80, 1, 0x64, 0xFF, 3)


def pickup_message(eng, e, text):
    """The pickups' message (health, ammo): id 2 replaces the last one; at (10, 70), additive,
    40 ticks, held 40, fading in from black over 11 ticks by (10, 8, 4) a tick, in the
    picker's viewport."""
    m = eng.m
    texts_remove_id(eng, 2)
    if text:
        mask = 1 << m.u8(m.u32(e + 0x388) + 0x152)
        text_add_fade(eng, text, 0xA, 0x46, 0, 0, 0, 1, 0x28, 2, mask, 0x28, 0xB, 0xA, 8, 4)


def read_string(m, a):
    out = []
    while a and len(out) < 64:
        c = m.u8(a)
        if not c:
            break
        out.append(chr(c))
        a += 1
    return "".join(out)


def state(m):
    """The trace's text entries: [string, x, y, r, g, b, life, hold] in list order."""
    out = []
    for i in range(max(0, min(m.s32(G_TEXT_COUNT), 0x32))):
        a = G_TEXTS + 0x20 * i
        out.append([read_string(m, m.u32(a)), m.s16(a + 4), m.s16(a + 6), m.u8(a + 8), m.u8(a + 9),
                    m.u8(a + 0xA), m.s32(a + 0xC), m.s32(a + 0x10)])
    return out


def ammo_digits(m, buf, value, width):
    """ammo_digits (0x80046700, buf, value, 0, width 2|3): space-padded decimal digits; a zero
    tens digit after a hundreds digit is written as 0."""
    pos = 0
    v = value
    if width == 3:
        if v >= 100:
            m.w8(buf, int(value / 100) + 0x30)
            v = int(value % 100) if value >= 0 else -int(-value % 100)
        else:
            m.w8(buf, 0x20)
        pos = 1
    if not v < 10:
        m.w8(buf + pos, int(v / 10) + 0x30)
        v = v - int(v / 10) * 10
    elif pos and m.u8(buf + pos - 1) != 0x20 and width != 2:
        m.w8(buf + pos, 0x30)
    else:
        m.w8(buf + pos, 0x20)
    m.w8(buf + pos + 1, (v + 0x30) & 0xFF)


def ammo_hud_update(eng, e):
    """ammo_hud_update (0x800467EC): the HUD's clip (width 2, inv+0x108) and reserve (width 3,
    inv+0x10C) strings, rewritten in place (the text list points at them); empty for the papers
    (weapon 7)."""
    m = eng.m
    inv = m.u32(e + 0x388)
    slot = m.s8(inv + 0x54)
    if m.u8(0x8009E878 + slot) == 7:
        m.w32(inv + 0x108, 0)
        m.w32(inv + 0x10C, 0)
        return
    ammo_digits(m, inv + 0x108, m.s16(inv + 0x66 + 2 * slot), 2)
    ammo_digits(m, inv + 0x10C, m.s16(inv + 0x56 + 2 * slot), 3)
