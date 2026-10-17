// Frame interpolation (see interp.cpp): the game simulates at 30 Hz; extra frames redraw the last
// drawn frame with the camera and moving objects placed between this tick and the next one.
#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace psx {

// A vertex the GTE projected: its camera-space position and how it was projected.
struct InterpVertex {
    float x, y, z;   // camera space (MAC1..3 before projection)
    float kx, ky;    // widescreen/FOV scale applied to the projected offset
    float h;         // projection plane distance
    int16_t v[3];    // model-space input
    int32_t group;   // the transform it was projected with (InterpTick::groups)
    bool translated; // projected with a translation (world objects); false: rotation only (sky)
    bool exact;      // the screen position is h*x/z (not clamped, no divide overflow)
};

// A transform (GTE rotation and translation) the game projected vertices with, and where from.
struct InterpGroup {
    int16_t rt[9];
    int32_t tr[3];
    uint16_t h;
    uint32_t pc;     // the projecting GTE command
    uint32_t sig;    // hash of the first model vertices projected with it (identifies the mesh)
    uint32_t sig_n;  // vertices hashed so far
    uint32_t count;
};

// Everything projected between two drawing phases (one game tick).
struct InterpTick {
    uint64_t seq = 0;
    std::vector<InterpVertex> vertices;
    // Projected SXY -> the latest vertex projected there; earlier ones at the same coordinate
    // are chained through `previous` (-1 ends the chain).
    std::unordered_map<uint32_t, uint32_t> by_sxy;
    std::vector<int32_t> previous;
    std::vector<InterpGroup> groups;
    std::unordered_map<uint64_t, uint32_t> group_index;
    int32_t last_group = -1;
};

// Where a tick's vertices go at some point between it and the next tick: q = a * p + b (b only
// for translated vertices), or for a vertex of an object that moved, from its model-space input:
// q = objects[i].m * v + objects[i].t.
struct InterpMotion {
    double a[3][3], b[3];
    struct Object { double m[3][3], t[3]; };
    std::vector<Object> objects;
    std::vector<int32_t> group_object;  // per group of the tick: index into objects, or -1
};

bool interp_enabled();
// gte.cpp: a vertex was projected (acc: camera-space position * 4096; v: model-space input).
void interp_record(int16_t sx, int16_t sy, const int64_t acc[3], uint16_t h, double kx, double ky,
                   const int16_t rt[3][3], const int32_t tr[3], const int16_t v[3], uint32_t pc);
// The tick whose projections the GPU is drawing now (the latest one), and the one after `seq`.
std::shared_ptr<const InterpTick> interp_drawing_tick();
std::shared_ptr<const InterpTick> interp_tick(uint64_t seq);
// Called by the GPU for every primitive: a drawing phase starts after projections.
void interp_note_draw();
// Motion from tick `from` to `alpha` of the way to tick `to`. False when there is no sensible
// in-between (camera cut, no camera found).
bool interp_motion(const InterpTick &from, const InterpTick &to, double alpha, InterpMotion &out);

inline uint32_t interp_key(int16_t sx, int16_t sy) { return uint32_t(uint16_t(sx)) | uint32_t(uint16_t(sy)) << 16; }

}  // namespace psx
