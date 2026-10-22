#include "valor/io.h"

#include <cstdio>

#include "valor/disc.h"

namespace valor {

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    if (path.compare(0, 5, "disc:") == 0) return disc_read(path, out);  // the mounted image (disc.h)
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool ok = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

}  // namespace valor
