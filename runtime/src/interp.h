// Frame interpolation (see interp.cpp): the game simulates at 30 Hz; extra frames re-project the
// last drawn frame with a camera between this tick's and the next one's.
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
    bool translated; // projected with a translation (world objects); false: rotation only (sky)
    bool exact;      // the screen position is h*x/z (not clamped, no divide overflow)
};

// Camera: world -> camera space is M * v + T (M includes the game's per-axis scale).
struct InterpCamera {
    double m[3][3], t[3];
};

// Everything projected between two drawing phases (one game tick).
struct InterpTick {
    uint64_t seq = 0;
    std::vector<InterpVertex> vertices;
    // Projected SXY -> the latest vertex projected there; earlier ones at the same coordinate
    // are chained through `previous` (-1 ends the chain).
    std::unordered_map<uint32_t, uint32_t> by_sxy;
    std::vector<int32_t> previous;
    struct MatrixUse { int16_t rt[9]; int32_t tr[3]; uint32_t count; };
    std::vector<MatrixUse> matrices;  // world projections by transform, to find the camera
    bool camera(InterpCamera &out) const;  // the transform most world vertices used
};

// The camera-space transform that moves a tick's vertices to an in-between camera:
// p' = a * p + b (b only for translated vertices).
struct InterpTransform {
    double a[3][3], b[3];
};

bool interp_enabled();
// gte.cpp: a world-space vertex was projected (acc: camera-space position * 4096).
void interp_record(int16_t sx, int16_t sy, const int64_t acc[3], uint16_t h, double kx, double ky,
                   const int16_t rt[3][3], const int32_t tr[3]);
// The tick whose projections the GPU is drawing now (the latest one), and the one after `seq`.
std::shared_ptr<const InterpTick> interp_drawing_tick();
std::shared_ptr<const InterpTick> interp_tick(uint64_t seq);
// Called by the GPU for every primitive: a drawing phase starts after projections.
void interp_note_draw();
// The transform from tick `from`'s camera to `alpha` of the way to tick `to`'s camera.
// False when there is no sensible in-between (camera cut, missing camera).
bool interp_transform(const InterpTick &from, const InterpTick &to, double alpha, InterpTransform &out);

inline uint32_t interp_key(int16_t sx, int16_t sy) { return uint32_t(uint16_t(sx)) | uint32_t(uint16_t(sy)) << 16; }

}  // namespace psx
