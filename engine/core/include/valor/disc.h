// The user's own disc image, read in place (nothing extracted or copied): a BIN/CUE of raw
// 2352-byte Mode 2 sectors, its ISO9660 file system walked once at mount. Mounted, read_file
// (io.h) serves paths under "disc:" from it: "disc:/DATA/MSN1/LVL1/1_1.BSD", with "." and ".."
// resolved, "\" taken as "/", any case, ";1" optional. Form 1 sectors give their 2048 bytes of
// user data (the game's files, the music's .VB streams among them); a file with Form 2 sectors
// (the STR movies' video and XA audio) reads as its raw 2352-byte sectors, as
// tools/extract_disc.py writes them.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace valor {

struct DiscFile {
    uint32_t lba = 0, size = 0;
    bool form2 = false;
};

// Mounts a .cue (its FILE) or a .bin. Checks it is Medal of Honor (USA): SLUS_009.74 in the root.
bool disc_mount(const std::string& path, std::string* err);
void disc_unmount();
bool disc_mounted();
const std::string& disc_image();  // the .bin mounted
// The files by upper-case path from the root ("DATA/GLOBAL.RSC", "SLUS_009.74").
const std::map<std::string, DiscFile>& disc_files();
// A path under "disc:" (any form above) as the files' key; "" when it isn't one.
std::string disc_key(const std::string& path);
bool disc_read(const std::string& path, std::vector<uint8_t>& out);

// Where the game's files are: "disc:" with an image mounted, else ROOT/extracted/moh (the
// extractor's output).
std::string game_root(const std::string& root);

}  // namespace valor
