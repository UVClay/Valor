// The level's collision: the TSP chunks' collision blocks (docs/re/levels.md, Collision; formats.md,
// TSP), the floor search and the walls of a walker. Ported from tools/sim/move_sim.py.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace valor {

struct V3s {
    int16_t x, y, z;
    int16_t operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};

struct CollNode {
    int16_t a, b;
    uint16_t split, zone;  // zone: the leaf's zone id (the BSD's zone lists)
};

struct CollFace {
    uint16_t v[3], n;
    int16_t d;  // (n . p) >> 15 + d = 0 on the plane
};

struct CollChunk {
    int number = 0;     // the N of m_l_cN.TSP
    std::string name;   // file name
    int16_t rect[4]{};  // x min, z min, x max, z max
    std::vector<CollNode> nodes;
    std::vector<uint16_t> idx;
    std::vector<V3s> verts, norms;
    std::vector<CollFace> faces;

    // Parses the collision block of a TSP file's bytes; false if it doesn't fit.
    bool parse(const std::vector<uint8_t>& tsp);
};

struct Floor {
    int32_t height;  // whole units, kNoFloor if none
    uint16_t zone;   // 0xFFFF if none
};

// A plane the walker's move goes into.
struct WallPlane {
    int16_t n[3];
    int16_t d;
    uint16_t face;
    const CollChunk* chunk;
};

class World {
public:
    // Reads every m_l_cN.TSP of a level from the extracted disc (DATA/MSNm/LVLl/TSP0).
    bool load(const std::string& data_dir, int mission, int level, std::string* err);

    std::vector<CollChunk> chunks;       // by file number - 1
    std::vector<const CollChunk*> slots;  // the loaded chunk slots, in slot order (null: empty)

    // The slots by the chunks' rectangles, as the game's slot table (0x80090DE8) holds them.
    void set_slots_by_rect(const int16_t rects[4][4]);
    void set_all_chunks();
    // The slots by chunk numbers (1-based file numbers, -1 empty), as Streaming holds them.
    void set_slots_by_number(const int numbers[4]);

    // floor_find (0x800603DC): the floor under a 16.16 position.
    Floor floor_find(const int32_t pos[3]) const;

    // walls_gather: the planes near a walker's segment (centre c, radius r, height h) that the
    // move delta goes into, at most maxn.
    void walls_gather(const int32_t c[3], int32_t r, int32_t h, const int32_t delta[3],
                      std::vector<WallPlane>& out, int maxn = 32) const;
};

}  // namespace valor
