// A level's BSD file: the header fields the engine uses so far and the player start
// (docs/re/formats.md, BSD; levels.md, Entity records).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace valor {

struct AnimLight {
    std::vector<uint32_t> steps;  // low 24 bits RGB (R low), top byte the ticks it lasts
    uint32_t step = 0;            // +0x8 as stored: the step before the first
};

struct PlayerStart {
    int16_t pos[3]{};  // record +0x10, whole units
    int16_t yaw = 0;   // record +0x1A, 4096 per turn
};

struct LevelInfo {
    int mission = 0, level = 0;
    uint32_t tsp_count = 0, chunk_slots = 0, first_chunk = 0;  // +0x80, +0x84, +0x88
    int16_t fog_near = 0;                                       // +0xCC (0: no fog)
    uint8_t fog_rgb[3]{};                                       // +0xCE
    std::vector<AnimLight> lights;                              // +0xD8 count, +0xDC entries
    uint8_t sky_rgb[3]{};                                       // +0x58C (stored R, B, G)
    uint32_t view_distance = 4500;                              // +0x590 (0: 4500)
    PlayerStart start;

    // Reads DATA/MSNm/LVLl/m_l.BSD.
    bool load(const std::string& data_dir, int mission, int level, std::string* err);
};

}  // namespace valor
