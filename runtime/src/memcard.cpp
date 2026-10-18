// Memory card in slot 1, speaking the SIO0 card protocol. Backed either by a raw 128 KB .mcd
// image, or by a folder of single saves (.mcs: the save's directory frame, then its 8 KB blocks):
// the card is assembled from them at start and split back into them after the game writes, so
// saves are files of their own and the card never fills up with other games' saves.
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <vector>

#include "gpu.h"
#include "memcard.h"
#include "runtime.h"

namespace psx::hw {

namespace {

uint64_t gpu_frames_now() { return gpu.frames; }

constexpr size_t SIZE = 128 * 1024, FRAME = 128, FRAMES = SIZE / FRAME;

constexpr size_t BLOCK = 8192, BLOCKS = 16;

std::vector<uint8_t> data;
std::filesystem::path file;  // .mcd image, or the save folder
bool folder;
bool inserted;
bool dirty;           // folder mode: written since the last export
uint64_t last_write;  // vblank of the latest write
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

uint8_t *dir(size_t block) { return data.data() + block * FRAME; }

// The saves on the card: first block -> its blocks in order.
std::map<size_t, std::vector<size_t>> card_saves() {
    std::map<size_t, std::vector<size_t>> saves;
    for (size_t b = 1; b < BLOCKS; b++) {
        if (dir(b)[0] != 0x51) continue;  // first block of a save in use
        std::vector<size_t> chain;
        for (size_t n = b; n >= 1 && n < BLOCKS && chain.size() < BLOCKS;) {
            chain.push_back(n);
            uint16_t next = uint16_t(dir(n)[8] | dir(n)[9] << 8);
            if (next == 0xFFFF) break;
            n = size_t(next) + 1;
        }
        saves[b] = chain;
    }
    return saves;
}

std::string save_name(size_t first) {
    std::string name;
    for (size_t i = 0x0A; i < 0x1E && dir(first)[i]; i++) {
        char ch = char(dir(first)[i]);
        name += (isalnum(uint8_t(ch)) || ch == '-' || ch == '_') ? ch : '_';
    }
    return name.empty() ? "save" : name;
}

// Folder mode: one .mcs per save on the card; files of saves no longer on it go to deleted/.
void export_saves() {
    std::filesystem::create_directories(file);
    std::vector<std::string> written;
    for (const auto &[first, chain] : card_saves()) {
        std::vector<uint8_t> out(dir(first), dir(first) + FRAME);
        out[8] = out[9] = 0xFF;  // no next block in a single save
        out[127] = xor_frame(out.data());
        for (size_t b : chain) out.insert(out.end(), data.begin() + b * BLOCK, data.begin() + (b + 1) * BLOCK);
        std::string name = save_name(first) + ".mcs";
        auto tmp = file / (name + ".tmp");
        std::ofstream(tmp, std::ios::binary).write(reinterpret_cast<const char *>(out.data()), std::streamsize(out.size()));
        std::filesystem::rename(tmp, file / name);
        written.push_back(name);
    }
    for (const auto &e : std::filesystem::directory_iterator(file)) {
        std::string name = e.path().filename().string();
        if (e.path().extension() != ".mcs" || std::find(written.begin(), written.end(), name) != written.end()) continue;
        std::filesystem::create_directories(file / "deleted");
        std::filesystem::rename(e.path(), file / "deleted" / name);
        log("memcard: %s deleted from the card (moved to %s)", name.c_str(), (file / "deleted").c_str());
    }
    dirty = false;
}

// Folder mode: a card holding the folder's saves.
void import_saves() {
    data = formatted_card();
    std::vector<std::filesystem::path> files;
    for (const auto &e : std::filesystem::directory_iterator(file))
        if (e.path().extension() == ".mcs") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    size_t next_free = 1;
    for (const auto &path : files) {
        std::ifstream f(path, std::ios::binary);
        std::vector<uint8_t> s(std::istreambuf_iterator<char>(f), {});
        if (s.size() < FRAME + BLOCK || (s.size() - FRAME) % BLOCK) {
            log("memcard: %s isn't a single save (.mcs), skipped", path.c_str());
            continue;
        }
        size_t n = (s.size() - FRAME) / BLOCK;
        if (next_free + n > BLOCKS) {
            log("memcard: no room for %s on the card, skipped", path.c_str());
            continue;
        }
        for (size_t k = 0; k < n; k++) {
            size_t b = next_free + k;
            uint8_t *d = dir(b);
            if (k == 0) std::copy(s.begin(), s.begin() + FRAME, d);
            else std::fill(d, d + FRAME, 0);
            d[0] = k == 0 ? 0x51 : k + 1 < n ? 0x52 : 0x53;
            uint16_t next = k + 1 < n ? uint16_t(b) : 0xFFFF;  // next block's index (blocks count from 0 after the directory)
            d[8] = uint8_t(next);
            d[9] = uint8_t(next >> 8);
            d[127] = xor_frame(d);
            std::copy(s.begin() + FRAME + k * BLOCK, s.begin() + FRAME + (k + 1) * BLOCK, data.begin() + b * BLOCK);
        }
        next_free += n;
    }
    log("memcard: %zu saves from %s", files.size(), file.c_str());
}

void save_frame(uint16_t n) {
    if (folder) {
        dirty = true;
        last_write = gpu_frames_now();
        return;
    }
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
    folder = file.extension() != ".mcd";
    if (folder) {
        if (!std::filesystem::exists(file) && std::filesystem::exists(config.memcard_import)) {
            // First run with a save folder: take the saves from the old card image (left as is).
            std::ifstream old(config.memcard_import, std::ios::binary);
            data.assign(std::istreambuf_iterator<char>(old), {});
            if (data.size() == SIZE) {
                export_saves();
                log("memcard: saves imported from %s into %s (the image is left untouched)",
                    config.memcard_import.c_str(), file.c_str());
            }
        }
        std::filesystem::create_directories(file);
        import_saves();
        if (const char *dump = getenv("VALOR_MEMCARD_DUMP"))  // testing: the assembled card
            std::ofstream(dump, std::ios::binary).write(reinterpret_cast<const char *>(data.data()), SIZE);
        inserted = true;
        return;
    }
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

void memcard_flush(bool now) {
    if (folder && dirty && (now || gpu_frames_now() > last_write + 60)) export_saves();
}

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
