// CD-ROM controller at 0x1F801800, reading raw 2352-byte sectors from the disc image.
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
        // TODO: decode XA-ADPCM into the SPU's CD audio input.
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
    default:  // audio volume registers
        break;
    }
}

}  // namespace psx::hw
