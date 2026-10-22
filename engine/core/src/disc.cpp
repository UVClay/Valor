// The disc image read in place (disc.h); tools/extract_disc.py's walk, in C++.
#include "valor/disc.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>

namespace valor {

namespace {
constexpr long kSector = 2352;
constexpr long kUser = 24;  // 12 sync + 4 header + 8 subheader
constexpr uint8_t kForm2 = 0x20;  // the subheader's submode bit

struct Mount {
    std::mutex lock;
    FILE* f = nullptr;
    std::string bin;
    std::map<std::string, DiscFile> files;
};

Mount& mount() {
    static Mount m;
    return m;
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool raw_sector(FILE* f, uint32_t lba, uint8_t* out) {
    if (std::fseek(f, long(lba) * kSector, SEEK_SET) != 0) return false;
    return std::fread(out, 1, kSector, f) == size_t(kSector);
}

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

// A directory's records: files into the map, directories walked (ISO9660 9.1).
bool walk(FILE* f, uint32_t lba, uint32_t size, const std::string& prefix, std::map<std::string, DiscFile>& out, int depth) {
    if (depth > 16) return false;
    uint8_t raw[kSector];
    for (uint32_t s = 0; s < (size + 2047) / 2048; s++) {
        if (!raw_sector(f, lba + s, raw)) return false;
        const uint8_t* d = raw + kUser;
        for (uint32_t o = 0; o < 2048;) {
            uint8_t len = d[o];
            if (len == 0) break;  // the rest of this sector is padding
            if (o + len > 2048 || len < 34) return false;
            uint32_t ext = le32(d + o + 2), sz = le32(d + o + 10);
            uint8_t flags = d[o + 25], nl = d[o + 32];
            std::string name(reinterpret_cast<const char*>(d + o + 33), nl);
            o += len;
            if (nl == 1 && (name[0] == 0 || name[0] == 1)) continue;  // "." and ".."
            size_t semi = name.find(';');
            if (semi != std::string::npos) name.resize(semi);
            name = upper(name);
            if (flags & 2) {
                if (!walk(f, ext, sz, prefix + name + "/", out, depth + 1)) return false;
            } else {
                out[prefix + name] = DiscFile{ext, sz, false};  // form2: found when read
            }
        }
    }
    return true;
}

// The .bin a .cue names (its FILE line, next to the .cue).
std::string bin_of(const std::string& path) {
    std::string low = path;
    for (char& c : low) c = char(std::tolower(uint8_t(c)));
    if (low.size() < 4 || low.substr(low.size() - 4) != ".cue") return path;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    char line[1024];
    std::string bin;
    while (std::fgets(line, sizeof line, f)) {
        std::string l = line;
        size_t a = l.find("FILE \""), b;
        if (a != std::string::npos && (b = l.find('"', a + 6)) != std::string::npos) {
            bin = l.substr(a + 6, b - a - 6);
            break;
        }
    }
    std::fclose(f);
    if (bin.empty()) return "";
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? bin : path.substr(0, slash + 1) + bin;
}
}  // namespace

bool disc_mount(const std::string& path, std::string* err) {
    disc_unmount();
    Mount& m = mount();
    std::lock_guard<std::mutex> g(m.lock);
    std::string bin = bin_of(path);
    if (bin.empty()) {
        if (err) *err = "the .cue names no .bin: " + path;
        return false;
    }
    FILE* f = std::fopen(bin.c_str(), "rb");
    if (!f) {
        if (err) *err = "can't open " + bin;
        return false;
    }
    // the primary volume descriptor (sector 16): "CD001", the root directory's record at +156
    uint8_t raw[kSector];
    std::map<std::string, DiscFile> files;
    const uint8_t* pvd = raw + kUser;
    if (!raw_sector(f, 16, raw) || pvd[0] != 1 || std::string(reinterpret_cast<const char*>(pvd + 1), 5) != "CD001" ||
        !walk(f, le32(pvd + 156 + 2), le32(pvd + 156 + 10), "", files, 0)) {
        std::fclose(f);
        if (err) *err = "not a Mode 2/2352 ISO9660 image: " + bin;
        return false;
    }
    if (!files.count("SLUS_009.74") || !files.count("DATA/GLOBAL.RSC")) {
        std::fclose(f);
        if (err) *err = "not Medal of Honor (USA) (no SLUS_009.74): " + bin;
        return false;
    }
    m.f = f;
    m.bin = bin;
    m.files = std::move(files);
    return true;
}

void disc_unmount() {
    Mount& m = mount();
    std::lock_guard<std::mutex> g(m.lock);
    if (m.f) std::fclose(m.f);
    m.f = nullptr;
    m.bin.clear();
    m.files.clear();
}

bool disc_mounted() { return mount().f != nullptr; }
const std::string& disc_image() { return mount().bin; }
const std::map<std::string, DiscFile>& disc_files() { return mount().files; }

std::string disc_key(const std::string& path) {
    if (path.compare(0, 5, "disc:") != 0) return "";
    std::vector<std::string> parts;
    std::string cur;
    auto flush = [&]() {
        if (cur == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!cur.empty() && cur != ".")
            parts.push_back(cur);
        cur.clear();
    };
    for (size_t i = 5; i < path.size(); i++) {
        char c = path[i];
        if (c == '/' || c == '\\')
            flush();
        else
            cur += char(std::toupper(uint8_t(c)));
    }
    flush();
    std::string key;
    for (auto& p : parts) key += (key.empty() ? "" : "/") + p;
    size_t semi = key.find(';');
    if (semi != std::string::npos) key.resize(semi);
    return key;
}

bool disc_read(const std::string& path, std::vector<uint8_t>& out) {
    Mount& m = mount();
    std::lock_guard<std::mutex> g(m.lock);
    if (!m.f) return false;
    auto it = m.files.find(disc_key(path));
    if (it == m.files.end()) return false;
    DiscFile& df = it->second;
    uint32_t n = (df.size + 2047) / 2048;
    std::vector<uint8_t> raw(size_t(n) * kSector);
    if (n && (std::fseek(m.f, long(df.lba) * kSector, SEEK_SET) != 0 || std::fread(raw.data(), 1, raw.size(), m.f) != raw.size()))
        return false;
    for (uint32_t i = 0; i < n && !df.form2; i++)
        if (raw[size_t(i) * kSector + 18] & kForm2) df.form2 = true;
    if (df.form2) {
        out = std::move(raw);
        return true;
    }
    out.resize(size_t(n) * 2048);
    for (uint32_t i = 0; i < n; i++) std::copy_n(&raw[size_t(i) * kSector + kUser], 2048, &out[size_t(i) * 2048]);
    out.resize(df.size);
    return true;
}

std::string game_root(const std::string& root) { return disc_mounted() ? "disc:" : root + "/extracted/moh"; }

}  // namespace valor
