"""The sound system's state the game logic reads (docs/re/audio.md, Voices): the voice table that
soldiers' hearing tests (soldier.md, Hearing) and sensor 0x68 reads. Volumes and panning are left
out (nothing in the logic reads them); which voice a sound gets, its position, age and whether
the SPU still plays it are kept.

The SPU's key status is modelled from the sample's length: a voice keyed on at a frame's
sound_update plays for (its ADPCM samples) / (44100 Hz x pitch / 4096); the first update at or
after that reads it as 3 (keyed off, releasing), the next as 0."""
from engine import s16, s32

G_VOICES = 0x8009115C
G_ENABLED = 0x800910D0
G_BANK_SPU = 0x800910E8        # per bank slot: SPU address (+0x40: loaded flag at 0x80091128)
G_SIZES = 0x8009263C           # per slot 0x200: u16 VAG sizes in 8-byte units (entry n+1 = VAG n)
G_PITCH = 0x8009163C           # base pitch per sound and slot


class Audio:
    def __init__(self, eng):
        self.eng = eng
        self.keyon = 0                      # g_keyon_pending
        self.spu = {}                       # voice -> (start vblank, end vblank) once keyed on
        self.ended = set()                  # voices read as 3 at the last update
        self.pan_phase = 0

    def adopt(self, vb):
        """The voices playing in the seed: keyed on, their remaining time from their age."""
        m = self.eng.m
        for v in range(1, 24):
            if m.u8(G_ENABLED + v) & 1:
                a = G_VOICES + 0x34 * v
                bank, sound = m.u16(a + 0xC), m.s32(a + 8)
                tbl = m.u32(G_BANK_SPU + 4 * bank + 0x20) if bank < 8 else 0
                size = m.u16(tbl + 2 * sound + 2) if tbl and sound >= 0 else 0
                p = s32(m.u16(G_PITCH + 2 * bank + 0x10 * sound) * m.s32(a + 0x20)) >> 16 if bank < 8 else 0x800
                dur = size * 8 // 16 * 28 * 60.0 / (44100.0 * max(p, 1) / 4096.0)
                start = vb - 2 * m.u16(a + 0x28)
                self.spu[v] = ("on", start, start + dur)

    # -- sound_play (0x80041CE8)
    def play(self, bank, sound, ent, volume, vspread, pitch, pspread, fade, loop, cls, flags):
        eng, m = self.eng, self.eng.m
        bank &= 0xFFFF
        if bank >= 8 or sound < 0 or not m.u32(G_BANK_SPU + 4 * s16(bank) + 0x40):
            return -1
        v = self.alloc()
        if v < 0:
            eng.note("sound dropped: no free voice")
            return -1
        a = G_VOICES + 0x34 * v
        m.w16(a + 0xC, bank)
        m.w32(a + 8, sound)
        vol = min(volume << 7, 0xFF0000) if (volume << 7) <= 0xFF0000 else 0xFF0000
        m.w32(a + 0x14, vol)
        m.w32(a + 0x18, volume)
        m.w32(a + 0x1C, vspread)
        m.w32(a + 0x20, pitch)
        m.w8(a + 0xE, loop)
        m.w16(a + 0x28, 0)
        m.w32(a + 0x24, pspread)
        m.w8(a + 0x13, cls)
        if vspread:
            from engine_feed import FN
            lo = s32(vol * vspread)
            r = eng.rand.draw(*FN["sound"])
            m.w32(a + 0x14, s32(vol + (lo - (r % s32(lo * 2) if lo else 0))))
        m.w8(a + 0xF, fade)
        m.w8(a + 0x12, 0)
        m.w8(a + 0x10, 0)
        m.w32(a + 4, m.u32(ent) if ent else 0)
        m.w32(a, flags)
        if ent and not m.u32(ent + 0xC) & 0x40000000:
            for k in range(3):
                m.w16(a + 0x2C + 2 * k, m.s16(ent + 0x20E + 4 * k))
        else:
            for k in range(3):
                m.w16(a + 0x2C + 2 * k, 0)
            m.w32(a, m.u32(a) | 1)
        self.start(bank, sound, v, pitch, pspread)
        return v

    def alloc(self):
        """voice_alloc (0x80041AA0): the first of 1..23 not looping, status 0 or 3."""
        m = self.eng.m
        for v in range(1, 24):
            if m.u8(G_VOICES + 0x34 * v + 0xE) == 0 and m.u8(G_ENABLED + v) in (0, 3):
                return v
        return -1

    def start(self, bank, sound, v, pitch, pspread):
        """voice_start (0x80041B04): skipped for samples under 13 blocks; the pitch with its
        spread; the key-on queued; the status 1 at once."""
        eng, m = self.eng, self.eng.m
        size = m.u16(m.u32(G_BANK_SPU + 4 * bank + 0x20) + 2 * sound + 2) if m.u32(G_BANK_SPU + 4 * bank + 0x20) else 0
        if size < 0xD:
            return
        p = s32(m.u16(G_PITCH + 2 * bank + 0x10 * sound) * pitch) >> 16
        lo = s32(p * pspread)
        sp = lo >> 15
        if sp:
            from engine_feed import FN
            p += (lo >> 16) - (eng.rand.draw(*FN["sound"]) % sp)
        p &= 0xFFFF
        self.keyon |= 1 << v
        m.w8(G_ENABLED + v, 1)
        samples = size * 8 // 16 * 28
        dur = samples * 60.0 / (44100.0 * max(p, 1) / 4096.0)
        self.spu[v] = ("pending", dur)

    def stop(self, v):
        """voice_stop (0x800425C4): key off now, entry cleared."""
        m = self.eng.m
        self.spu[v] = ("off", 0)
        m.zero(G_VOICES + 0x34 * v, 0x34)

    # -- sound_update (0x8004137C), once per drawn frame
    def update(self):
        eng, m = self.eng, self.eng.m
        vb = eng.vblank
        # SpuGetAllKeysStatus
        for v in range(1, 24):
            st = self.spu.get(v)
            if st is None:
                continue
            if st[0] == "on":
                status = 1 if vb < st[2] else 3
                if status == 3:
                    self.spu[v] = ("rel", 0)
            elif st[0] == "rel":
                status = 0
                self.spu[v] = ("off", 0)
            elif st[0] == "pending":
                status = m.u8(G_ENABLED + v)
            else:
                status = 0
            m.w8(G_ENABLED + v, status)
        if self.keyon:
            for v in range(24):
                if self.keyon >> v & 1:
                    st = self.spu.get(v)
                    dur = st[1] if st and st[0] == "pending" else 0
                    self.spu[v] = ("on", vb, vb + dur)
                    m.w8(G_ENABLED + v, 1)
            self.keyon = 0
        for v in range(1, 24):
            a = G_VOICES + 0x34 * v
            en = m.u8(G_ENABLED + v)
            if en & 1:
                age = (m.u16(a + 0x28) + 1) & 0xFFFF
                m.w16(a + 0x28, age)
                if age >= 0x15F and m.u8(a + 0xE) == 0:
                    self.stop(v)
                    continue
                h = m.u32(a + 4)
                if not eng.handle_dead(h):
                    o = eng.resolve(h)
                    if not m.u32(o + 0xC) & 0x80000:
                        for k in range(3):
                            m.w16(a + 0x2C + 2 * k, m.s16(o + 0x20E + 4 * k))
                    else:
                        m.w32(a + 4, 0)
                if en == 3 and m.u8(a + 0x12) == 0:
                    continue
                if m.u8(a + 0x12):
                    n = m.u8(a + 0x12) - 1
                    m.w8(a + 0x12, n)
                    if n == 0:
                        self.release(v)
                continue
            if m.u8(a + 0xE):
                m.w8(a + 0xE, 0)
                eng.note("looping sounds (restart) not implemented")
            else:
                m.w32(a + 4, 0)
                m.w8(a + 0x12, 0)
                m.w16(a + 0x28, 0)

    def release(self, v):
        st = self.spu.get(v)
        if st and st[0] == "on":
            self.spu[v] = ("rel", 0)

    def fade_entity(self, e, frames):
        """sound_fade_entity (0x8004249C): every voice following e fades out over `frames`."""
        m = self.eng.m
        h = m.u32(e)
        for v in range(1, 24):
            a = G_VOICES + 0x34 * v
            if m.u32(a + 4) == h and m.u8(G_ENABLED + v) & 1:
                m.w8(a + 0x11, frames)
                m.w8(a + 0x12, frames)
