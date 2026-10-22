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
    // the chosen face's normal and steep flag (floor_in_chunk 0x80060138: ny < 0 and
    // -16 ny / max(|nx|, |nz|) < 28); has_normal false when no face was chosen
    int16_t normal[4]{};
    bool has_normal = false;
};

// A plane the walker's move goes into.
struct WallPlane {
    int16_t n[3];
    int16_t d;
    uint16_t face;
    const CollChunk* chunk;
};

// The face a sweep hit: its three vertices and its d (the face record's +0x10.. and +6).
struct SweepFace {
    int16_t v[3][3]{};
    int16_t d = 0;
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
    // func_8003EE30 at an integer point: over the loaded chunks holding (x, z), the leaf's
    // up-facing triangle whose height + 16 is nearest y; between chunks the largest height.
    int32_t point_floor(const int32_t p[3]) const;

    // world_sweep (0x8003EF5C): the segment from -> to (whole units) against the loaded chunks'
    // triangles crossed front to back: the nearest hit point in the chunk with the smallest t
    // (tools/sim/engine_world.py). False if nothing is hit.
    bool sweep(const int32_t from[3], const int32_t to[3], int32_t hit[3], int16_t normal[3], SweepFace* face = nullptr) const;

    // walls_gather: the planes near a walker's segment (centre c, radius r, height h) that the
    // move delta goes into, at most maxn.
    // use_delta false (the brain's walk gather) keeps the walls whatever the direction of the move.
    void walls_gather(const int32_t c[3], int32_t r, int32_t h, const int32_t delta[3],
                      std::vector<WallPlane>& out, int maxn = 32, bool use_delta = true) const;
};

// The gather's tests, for walls_gather's other users (brain_sim.py's walker_slide).
bool wall_tri_near(const CollChunk& ch, const CollFace& f, const int32_t c[3], int32_t r, int32_t h);
int32_t wall_plane_dot(const V3s& n, int32_t x, int32_t y, int32_t z);
bool wall_plane_crosses(const int32_t c[3], int32_t r, int32_t h, const V3s& n, int32_t d);

}  // namespace valor
