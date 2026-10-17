// Memory card: 128 KB backed by a standard raw .mcd file, speaking the SIO0 card protocol.
#include <fstream>
#include <vector>

#include "memcard.h"
#include "runtime.h"

namespace psx::hw {

namespace {

constexpr size_t SIZE = 128 * 1024, FRAME = 128, FRAMES = SIZE / FRAME;

std::vector<uint8_t> data;
std::filesystem::path file;
bool inserted;
uint8_t flag = 0x08;  // bit 3: no write since power on

// Exchange state
enum class Cmd { None, Read, Write, Id } cmd;
uint16_t sector;
uint8_t checksum, host_checksum, last_rx;
uint8_t write_buf[FRAME];

uint8_t xor_frame(const uint8_t *f) {
    uint8_t x = 0;
    for (size_t i = 0; i < FRAME - 1; i++) x ^= f[i];
    return x;
}

// An empty, formatted card: header, 15 free directory entries, empty broken-sector list.
std::vector<uint8_t> formatted_card() {
    std::vector<uint8_t> c(SIZE, 0);
    auto frame = [&](size_t n) { return c.data() + n * FRAME; };
    frame(0)[0] = 'M';
    frame(0)[1] = 'C';
    frame(0)[127] = xor_frame(frame(0));
    for (size_t n = 1; n < 16; n++) {
        uint8_t *f = frame(n);
        f[0] = 0xA0;  // free
        f[8] = f[9] = 0xFF;
        f[127] = xor_frame(f);
    }
    for (size_t n = 16; n < 36; n++) {
        uint8_t *f = frame(n);
        f[0] = f[1] = f[2] = f[3] = 0xFF;  // no broken sector
        f[8] = f[9] = 0xFF;
        f[127] = xor_frame(f);
    }
    for (size_t n = 36; n < 63; n++) std::fill(frame(n), frame(n) + FRAME, 0xFF);
    std::copy(frame(0), frame(0) + FRAME, frame(63));  // write test frame
    return c;
}

void save_frame(uint16_t n) {
    std::fstream f(file, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) {
        log("memcard: cannot write %s", file.c_str());
        return;
    }
    f.seekp(std::streamoff(n) * FRAME);
    f.write(reinterpret_cast<const char *>(data.data() + size_t(n) * FRAME), FRAME);
}

}  // namespace

void memcard_init() {
    inserted = false;
    if (config.memcard.empty()) return;
    file = config.memcard;
    std::ifstream f(file, std::ios::binary);
    if (f) {
        data.assign(std::istreambuf_iterator<char>(f), {});
        if (data.size() != SIZE) fatal("%s is not a 128 KB memory card image", file.c_str());
    } else {
        data = formatted_card();
        if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char *>(data.data()), SIZE);
        if (!out) fatal("cannot create memory card %s", file.c_str());
        log("memcard: created %s", file.c_str());
    }
    inserted = true;
}

bool memcard_present() { return inserted; }

// Byte `step` of an exchange (step 0 is the 0x81 select byte). Returns the card's reply and
// whether it acknowledges (asks for another byte).
uint8_t memcard_exchange(int step, uint8_t tx, bool &ack) {
    ack = true;
    uint8_t prev = last_rx;
    last_rx = tx;
    if (step == 0) {
        cmd = Cmd::None;
        return 0xFF;
    }
    if (step == 1) {
        cmd = tx == 0x52 ? Cmd::Read : tx == 0x57 ? Cmd::Write : tx == 0x53 ? Cmd::Id : Cmd::None;
        if (cmd == Cmd::None) { ack = false; return 0xFF; }
        return flag;
    }
    if (step == 2) return 0x5A;
    if (step == 3) return 0x5D;

    if (cmd == Cmd::Id) {
        static const uint8_t id[] = {0x5C, 0x5D, 0x04, 0x00, 0x00, 0x80};
        int i = step - 4;
        if (i < 6) { ack = i < 5; return id[i]; }
        ack = false;
        return 0xFF;
    }

    if (step == 4) { sector = uint16_t(tx << 8); return 0x00; }
    if (step == 5) {
        sector |= tx;
        checksum = uint8_t((sector >> 8) ^ (sector & 0xFF));
        return prev;
    }

    if (cmd == Cmd::Read) {
        bool bad = sector >= FRAMES;
        if (step == 6) return 0x5C;
        if (step == 7) return 0x5D;
        if (step == 8) return bad ? 0xFF : uint8_t(sector >> 8);
        if (step == 9) {
            if (bad) { ack = false; return 0xFF; }
            return uint8_t(sector);
        }
        int i = step - 10;
        if (i < int(FRAME)) {
            uint8_t b = data[size_t(sector) * FRAME + i];
            checksum ^= b;
            return b;
        }
        if (i == int(FRAME)) return checksum;
        ack = false;
        return 0x47;  // 'G'
    }

    // Write: 128 data bytes, checksum, then the card reports.
    int i = step - 6;
    if (i < int(FRAME)) {
        write_buf[i] = tx;
        checksum ^= tx;
        return prev;
    }
    if (i == int(FRAME)) {
        host_checksum = tx;
        return prev;
    }
    if (i == int(FRAME) + 1) return 0x5C;
    if (i == int(FRAME) + 2) return 0x5D;
    ack = false;
    if (sector >= FRAMES) return 0xFF;
    if (host_checksum != checksum) return 0x4E;  // 'N': bad checksum
    std::copy(write_buf, write_buf + FRAME, data.begin() + size_t(sector) * FRAME);
    save_frame(sector);
    flag &= ~0x08;
    return 0x47;  // 'G'
}

}  // namespace psx::hw
