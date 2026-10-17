// CD-ROM controller at 0x1F801800, reading raw 2352-byte sectors from the disc image.
#include <algorithm>
#include <cstring>
#include <deque>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "hw.h"

namespace psx::hw {

namespace {

constexpr int SECTOR = 2352;
constexpr int64_t READ_PERIOD_1X = CPU_HZ / 75;
constexpr int64_t FIRST_RESPONSE = 25000;   // command -> INT3
constexpr int64_t ACK_TO_NEXT = 2000;       // gap before a queued response after an ack

uint8_t bcd(int v) { return uint8_t((v / 10) << 4 | (v % 10)); }
int unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

// ---------------------------------------------------------------------------------------------
// Disc image

struct Disc {
    std::ifstream bin;
    int sectors = 0;

    bool open(const std::filesystem::path &path) {
        std::filesystem::path binpath = path;
        if (path.extension() == ".cue" || path.extension() == ".CUE") {
            std::ifstream cue(path);
            std::string line;
            while (std::getline(cue, line)) {
                auto q1 = line.find('"'), q2 = line.rfind('"');
                if (line.find("FILE") != std::string::npos && q1 != std::string::npos && q2 > q1) {
                    binpath = path.parent_path() / line.substr(q1 + 1, q2 - q1 - 1);
                    break;
                }
            }
        }
        bin.open(binpath, std::ios::binary);
        if (!bin) return false;
        bin.seekg(0, std::ios::end);
        sectors = int(bin.tellg() / SECTOR);
        log("cdrom: %s, %d sectors", binpath.c_str(), sectors);
        return true;
    }

    // `lba` is absolute (MSF 00:02:00 is the first sector of the image).
    bool read(int lba, uint8_t *out) {
        int idx = lba - 150;
        if (!bin.is_open() || idx < 0 || idx >= sectors) return false;
        bin.seekg(int64_t(idx) * SECTOR);
        bin.read(reinterpret_cast<char *>(out), SECTOR);
        return bool(bin);
    }
};

Disc disc;

// ---------------------------------------------------------------------------------------------
// XA-ADPCM audio. Decoding and the 37.8/18.9 kHz -> 44.1 kHz resampler follow Mednafen's
// CDC (via Beetle PSX, GPL-2.0-or-later), including its impulse table.

const int16_t XA_IMPULSE[7][25] = {
    {0, -5, 17, -35, 70, -23, -68, 347, -839, 2062, -4681, 15367, 21472, -5882, 2810, -1352, 635, -235, 26, 43, -35, 16, -8, 2, 0},
    {0, -2, 10, -34, 65, -84, 52, 9, -266, 1024, -2680, 9036, 26516, -6016, 3021, -1571, 848, -365, 107, 10, -16, 17, -8, 3, -1},
    {-2, 0, 3, -19, 60, -75, 162, -227, 306, -67, -615, 3229, 29883, -4532, 2488, -1471, 882, -424, 166, -27, 5, 6, -8, 3, -1},
    {-1, 3, -2, -5, 31, -74, 179, -402, 689, -926, 1272, -1446, 31033, -1446, 1272, -926, 689, -402, 179, -74, 31, -5, -2, 3, -1},
    {-1, 3, -8, 6, 5, -27, 166, -424, 882, -1471, 2488, -4532, 29883, 3229, -615, -67, 306, -227, 162, -75, 60, -19, 3, 0, -2},
    {-1, 3, -8, 17, -16, 10, 107, -365, 848, -1571, 3021, -6016, 26516, 9036, -2680, 1024, -266, 9, 52, -84, 65, -34, 10, -2, 0},
    {0, 2, -8, 16, -35, 43, 26, -235, 635, -1352, 2810, -5882, 21472, 15367, -4681, 2062, -839, 347, -68, -23, 70, -35, 17, -5, 0},
};

struct XaAudio {
    int16_t samples[2][18 * 8 * 28];  // one sector, decoded
    unsigned size = 0, pos = 0;
    unsigned freq = 0;                // 6 = 37.8 kHz, 3 = 18.9 kHz (in sevenths of 44.1 kHz)
    int16_t previous[2][2] = {};
    int16_t resamp[2][64] = {};
    unsigned resamp_phase = 0, resamp_pos = 0;
} xa;

bool muted;
uint8_t vol_pending[2][2] = {{0x80, 0}, {0, 0x80}}, volume[2][2] = {{0x80, 0}, {0, 0x80}};  // [src][dst]
bool adpcm_muted;

void decode_xa_unit(const uint8_t *in, int16_t *out, unsigned shift, unsigned weight) {
    static const int32_t W[16][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    for (int i = 0; i < 28; i++) {
        int32_t s = int16_t(in[i] << 8);
        s >>= shift;
        s += ((out[i - 1] * W[weight][0]) >> 6) + ((out[i - 2] * W[weight][1]) >> 6);
        out[i] = int16_t(std::clamp(s, -32768, 32767));
    }
}

void decode_xa_sector(const uint8_t *raw) {
    uint8_t coding = raw[19];
    bool eight_bit = coding & 0x10, stereo = coding & 0x01;
    unsigned shift_units = eight_bit ? 0 : 1;
    xa.pos = 0;
    xa.size = 18 * (4u << shift_units) * 28 / (stereo ? 2 : 1);
    xa.freq = (coding & 0x04) ? 3 : 6;
    for (unsigned group = 0; group < 18; group++) {
        const uint8_t *sg = raw + 24 + group * 128;
        const uint8_t *params = sg, *data = sg + 16;
        for (unsigned unit = 0; unit < (4u << shift_units); unit++) {
            uint8_t param = params[(unit & 3) | ((unit & 4) << 1)];
            uint8_t param_copy = params[4 | (unit & 3) | ((unit & 4) << 1)];
            uint8_t in[28];
            for (int i = 0; i < 28; i++) {
                uint8_t t = data[i * 4 + (unit >> shift_units)];
                if (shift_units) t = uint8_t((t << ((unit & 1) ? 0 : 4)) & 0xF0);
                in[i] = t;
            }
            int ch = (unit & 1) && stereo;
            int16_t out[30];
            out[0] = xa.previous[ch][0];
            out[1] = xa.previous[ch][1];
            decode_xa_unit(in, out + 2, param & 0x0F, param >> 4);
            xa.previous[ch][0] = out[28];
            xa.previous[ch][1] = out[29];
            if (param != param_copy) memset(out, 0, sizeof out);
            for (int i = 0; i < 28; i++) {
                if (stereo) {
                    xa.samples[ch][group * (2u << shift_units) * 28 + (unit >> 1) * 28 + i] = out[2 + i];
                } else {
                    unsigned at = group * (4u << shift_units) * 28 + unit * 28 + i;
                    xa.samples[0][at] = xa.samples[1][at] = out[2 + i];
                }
            }
        }
    }
}

}  // namespace
}  // namespace psx::hw

// The SPU pulls one 44.1 kHz sample of CD audio at a time.
extern "C" void CDC_GetCDAudioSample(int32_t samples[2]) {
    using namespace psx::hw;
    samples[0] = samples[1] = 0;
    if (!xa.freq || xa.pos >= xa.size) return;
    int32_t s[2];
    for (int c = 0; c < 2; c++) {
        const int16_t *imp = XA_IMPULSE[xa.resamp_phase];
        const int16_t *wf = &xa.resamp[c][(xa.resamp_pos + 32 - 25) & 0x1F];
        int32_t acc = 0;
        for (int k = 0; k < 25; k++) acc += imp[k] * wf[k];
        s[c] = std::clamp(acc >> 15, -32768, 32767);
    }
    xa.resamp_phase += xa.freq;
    if (xa.resamp_phase >= 7) {
        xa.resamp_phase -= 7;
        for (int c = 0; c < 2; c++)
            xa.resamp[c][xa.resamp_pos] = xa.resamp[c][xa.resamp_pos + 32] = xa.samples[c][xa.pos];
        xa.pos++;
        xa.resamp_pos = (xa.resamp_pos + 1) & 0x1F;
    }
    if (muted || adpcm_muted) return;
    for (int d = 0; d < 2; d++)
        samples[d] = std::clamp(((s[0] * volume[0][d]) >> 7) + ((s[1] * volume[1][d]) >> 7), -32768, 32767);
}

namespace psx::hw {
namespace {

// ---------------------------------------------------------------------------------------------
// Controller state

struct Response {
    int64_t when;
    uint8_t type;  // INT1..INT5
    std::vector<uint8_t> bytes;
    std::optional<std::vector<uint8_t>> sector;  // INT1: data becomes readable on delivery
};

uint8_t index_reg, int_enable, int_flag, mode, stat = 0x02;
std::deque<uint8_t> params, response;
std::vector<uint8_t> data;  // data FIFO
size_t data_pos;
std::vector<uint8_t> sector_buf;  // last delivered sector (user data per mode)
uint8_t last_header[8];           // GetlocL
std::deque<Response> queue;
bool busy;
int setloc_lba = 150, read_lba = 150;
bool reading, setloc_pending;
uint8_t filter_file, filter_chan;

int64_t read_period() { return (mode & 0x80) ? READ_PERIOD_1X / 2 : READ_PERIOD_1X; }

void schedule_delivery() {
    if (int_flag || queue.empty()) return;
    schedule_at(Event::CdResponse, std::max(queue.front().when, now()));
}

void push(int64_t delay, uint8_t type, std::vector<uint8_t> bytes,
          std::optional<std::vector<uint8_t>> sector = std::nullopt) {
    int64_t when = now() + delay;
    if (!queue.empty()) when = std::max(when, queue.back().when);
    queue.push_back({when, type, std::move(bytes), std::move(sector)});
    schedule_delivery();
}

void drop_queued_sectors() {
    std::erase_if(queue, [](const Response &r) { return r.type == 1; });
}

int64_t seek_time(int from, int to) {
    int64_t d = std::abs(to - from);
    return std::min<int64_t>(CPU_HZ / 2, 100000 + d * 12);
}

void start_reading() {
    int from = read_lba;
    if (setloc_pending) {
        read_lba = setloc_lba;
        setloc_pending = false;
    }
    reading = true;
    stat = (stat & ~0xE0) | 0x02 | 0x20;
    schedule(Event::CdSector, seek_time(from, read_lba) + read_period());
}

void stop_reading() {
    reading = false;
    cancel(Event::CdSector);
    drop_queued_sectors();
    stat &= ~0xE0;
}

std::string hex(const std::vector<uint8_t> &v) {
    std::string s;
    char b[4];
    for (uint8_t x : v) { snprintf(b, sizeof b, " %02X", x); s += b; }
    return s;
}

void command(uint8_t cmd) {
    std::vector<uint8_t> p(params.begin(), params.end());
    if (config.trace_cd) log("cd: [%lld] cmd %02X%s (if=%d queued=%zu)", (long long)now(), cmd, hex(p).c_str(), int_flag, queue.size());
    params.clear();
    auto arg = [&](size_t i) -> uint8_t { return i < p.size() ? p[i] : 0; };
    busy = true;

    switch (cmd) {
    case 0x01:  // Getstat
        push(FIRST_RESPONSE, 3, {stat});
        stat &= ~0x10;  // shell-open latch clears once reported
        break;
    case 0x02:  // Setloc mm ss ff
        setloc_lba = (unbcd(arg(0)) * 60 + unbcd(arg(1))) * 75 + unbcd(arg(2));
        setloc_pending = true;
        push(FIRST_RESPONSE, 3, {stat});
        break;
    case 0x06:  // ReadN
    case 0x1B:  // ReadS
        push(FIRST_RESPONSE, 3, {stat});
        start_reading();
        break;
    case 0x07:  // MotorOn
        stat |= 0x02;
        push(FIRST_RESPONSE, 3, {stat});
        push(CPU_HZ / 10, 2, {stat});
        break;
    case 0x08:  // Stop
        stop_reading();
        push(FIRST_RESPONSE, 3, {stat});
        stat &= ~0x02;
        push(CPU_HZ / 10, 2, {stat});
        break;
    case 0x09: {  // Pause
        uint8_t before = stat;
        bool was_reading = reading;
        stop_reading();
        push(FIRST_RESPONSE, 3, {before});
        push(was_reading ? read_period() * 2 : 7000, 2, {stat});
        break;
    }
    case 0x0A:  // Init
        stop_reading();
        mode = 0;
        stat = 0x02;
        push(FIRST_RESPONSE * 3, 3, {stat});
        push(CPU_HZ / 30, 2, {stat});
        break;
    case 0x0B:  // Mute
    case 0x0C:  // Demute
        muted = cmd == 0x0B;
        push(FIRST_RESPONSE, 3, {stat});
        break;
    case 0x0D:  // Setfilter file chan
        filter_file = arg(0);
        filter_chan = arg(1);
        push(FIRST_RESPONSE, 3, {stat});
        break;
    case 0x0E:  // Setmode
        mode = arg(0);
        push(FIRST_RESPONSE, 3, {stat});
        break;
    case 0x0F:  // Getparam
        push(FIRST_RESPONSE, 3, {stat, mode, 0, filter_file, filter_chan});
        break;
    case 0x10:  // GetlocL
        push(FIRST_RESPONSE, 3, std::vector<uint8_t>(last_header, last_header + 8));
        break;
    case 0x11: {  // GetlocP
        int lba = read_lba, rel = lba - 150;
        auto msf = [](int l, int part) { return bcd(part == 0 ? l / 4500 : part == 1 ? (l / 75) % 60 : l % 75); };
        push(FIRST_RESPONSE, 3, {0x01, 0x01, msf(rel, 0), msf(rel, 1), msf(rel, 2),
                                 msf(lba, 0), msf(lba, 1), msf(lba, 2)});
        break;
    }
    case 0x13:  // GetTN
        push(FIRST_RESPONSE, 3, {stat, 0x01, 0x01});
        break;
    case 0x14: {  // GetTD track
        int lba = unbcd(arg(0)) == 0 ? disc.sectors + 150 : 150;
        push(FIRST_RESPONSE, 3, {stat, bcd(lba / 4500), bcd((lba / 75) % 60)});
        break;
    }
    case 0x15:  // SeekL
    case 0x16: {  // SeekP
        stop_reading();
        int from = read_lba;
        if (setloc_pending) read_lba = setloc_lba;
        setloc_pending = false;
        stat |= 0x40;
        push(FIRST_RESPONSE, 3, {stat});
        stat &= ~0x40;
        push(seek_time(from, read_lba), 2, {stat});
        break;
    }
    case 0x19:  // Test
        if (arg(0) == 0x20) push(FIRST_RESPONSE, 3, {0x94, 0x09, 0x19, 0xC0});  // controller date/version
        else push(FIRST_RESPONSE, 5, {uint8_t(stat | 1), 0x10});
        break;
    case 0x1A:  // GetID: licensed Mode 2 disc, NTSC-U
        push(FIRST_RESPONSE, 3, {stat});
        push(FIRST_RESPONSE * 2, 2, {0x02, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'A'});
        break;
    case 0x1E:  // ReadTOC
        push(FIRST_RESPONSE, 3, {stat});
        push(CPU_HZ / 2, 2, {stat});
        break;
    case 0x03:  // Play (no audio tracks on these discs)
    case 0x04:  // Forward
    case 0x05:  // Backward
        push(FIRST_RESPONSE, 3, {stat});
        break;
    default:
        log("cdrom: unknown command %02X", cmd);
        push(FIRST_RESPONSE, 5, {uint8_t(stat | 1), 0x40});
        break;
    }
}

void deliver(Response &r) {
    if (config.trace_cd && r.type != 1) log("cd:   INT%d%s", r.type, hex(r.bytes).c_str());
    if (config.trace_cd && r.type == 1) log("cd:   INT1 sector %d", read_lba - 1);
    response.assign(r.bytes.begin(), r.bytes.end());
    int_flag = r.type;
    busy = false;
    if (r.sector) sector_buf = std::move(*r.sector);
    if (int_enable & int_flag) irq_raise(IRQ_CDROM);
}

}  // namespace

void cdrom_init() {
    if (!config.disc.empty() && !disc.open(config.disc)) fatal("cannot open disc image %s", config.disc.c_str());
}

void cdrom_event_response() {
    if (int_flag || queue.empty()) return;
    if (queue.front().when > now()) return schedule_delivery();
    Response r = std::move(queue.front());
    queue.pop_front();
    deliver(r);
}

void cdrom_event_sector() {
    if (!reading) return;
    uint8_t raw[SECTOR];
    if (!disc.read(read_lba, raw)) {
        stop_reading();
        push(0, 5, {uint8_t(stat | 1), 0x04});  // seek error / end of disc
        return;
    }
    memcpy(last_header, raw + 12, 8);
    read_lba++;
    schedule(Event::CdSector, read_period());

    uint8_t submode = raw[18];
    bool realtime_audio = (submode & 0x44) == 0x44;  // real-time + audio: XA-ADPCM
    if ((mode & 0x40) && realtime_audio) {
        // XA-ADPCM goes to the SPU, not the CPU (subject to the file/channel filter).
        bool match = !(mode & 0x08) || (raw[16] == filter_file && raw[17] == filter_chan);
        if (match) decode_xa_sector(raw);
        return;
    }
    if ((mode & 0x08) && realtime_audio) return;

    size_t off = (mode & 0x20) ? 12 : 24;
    size_t len = (mode & 0x20) ? 2340 : 2048;
    // Only one sector waits in the controller: a newer one replaces an undelivered one.
    drop_queued_sectors();
    push(0, 1, {stat}, std::vector<uint8_t>(raw + off, raw + off + len));
}

void cdrom_dma_read(uint32_t addr, uint32_t words) {
    if (config.trace_cd) log("cd:   dma %u words to %08X from fifo pos %zu/%zu", words, addr, data_pos, data.size());
    for (uint32_t i = 0; i < words * 4; i++) {
        uint8_t b = data_pos < data.size() ? data[data_pos++] : 0;
        psx_ram[(addr + i) & (PSX_RAM_SIZE - 1)] = b;
    }
}

uint8_t cdrom_read(uint32_t phys) {
    switch (phys & 3) {
    case 0:
        return index_reg | (params.empty() ? 0x08 : 0) | (params.size() < 16 ? 0x10 : 0) |
               (response.empty() ? 0 : 0x20) | (data_pos < data.size() ? 0x40 : 0) | (busy ? 0x80 : 0);
    case 1: {
        if (response.empty()) return 0;
        uint8_t v = response.front();
        response.pop_front();
        return v;
    }
    case 2:
        if (config.trace_cd && data_pos < 16) log("cd:   fifo byte read at %zu", data_pos);
        return data_pos < data.size() ? data[data_pos++] : 0;
    default:
        return (index_reg & 1) ? (0xE0 | int_flag) : (0xE0 | int_enable);
    }
}

void cdrom_write(uint32_t phys, uint8_t v) {
    int reg = phys & 3;
    if (reg == 0) { index_reg = v & 3; return; }
    switch (reg << 4 | index_reg) {
    case 0x10: command(v); break;
    case 0x20: if (params.size() < 16) params.push_back(v); break;
    case 0x21: int_enable = v & 0x1F; break;
    case 0x30:  // request register
        if (config.trace_cd) log("cd:   request %02X", v);
        if (v & 0x80) {
            // Loads the FIFO only if it's empty: libds sets the bit again between reading the
            // header and the data, and that must not rewind.
            if (data_pos >= data.size()) { data = sector_buf; data_pos = 0; }
        } else {
            data.clear();
            data_pos = 0;
        }
        break;
    case 0x31:  // interrupt flag: acknowledge
        int_flag &= ~(v & 0x1F);
        if (v & 0x40) params.clear();
        if (!int_flag && !queue.empty()) {
            queue.front().when = std::max(queue.front().when, now() + ACK_TO_NEXT);
            schedule_delivery();
        }
        break;
    case 0x22: vol_pending[0][0] = v; break;  // L -> L
    case 0x32: vol_pending[0][1] = v; break;  // L -> R
    case 0x13: vol_pending[1][1] = v; break;  // R -> R
    case 0x23: vol_pending[1][0] = v; break;  // R -> L
    case 0x33:  // apply volume changes / mute ADPCM
        adpcm_muted = v & 1;
        if (v & 0x20) memcpy(volume, vol_pending, sizeof volume);
        break;
    default:  // sound map registers (XA from RAM): unused
        break;
    }
}

}  // namespace psx::hw
