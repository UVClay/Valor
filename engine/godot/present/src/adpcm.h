// SPU ADPCM (docs/re/audio.md, formats.md VB): 16-byte blocks of a shift/filter byte, a flags byte
// (1 end, 2 repeat, 4 loop start) and 28 four-bit samples. Decoded to 16-bit PCM.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace valor_present {

struct Adpcm {
    std::vector<int16_t> pcm;
    int loop_start = -1;  // the sample a block flagged "loop start" begins at, if any
    bool loops = false;   // the last block is flagged end + repeat
};

// Decodes blocks from data[0..size), stopping after a block flagged end (unless whole_file).
Adpcm adpcm_decode(const uint8_t* data, size_t size, bool whole_file);

}  // namespace valor_present
