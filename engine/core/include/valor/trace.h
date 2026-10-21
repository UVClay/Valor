// State traces in the VALOR_TRACE_STATE format (runtime/src/watch.cpp): one JSON line per tick,
// comparable with the original's by tools/recon/trace_cmp.py.
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace valor {

struct TracePlayer {
    int32_t look_yaw = 0, look_pitch = 0, slot = 0, weapon = 0, clip = 0, reserve = 0;
    uint32_t flags_b = 0;
};

struct TraceEnt {
    uint32_t handle = 0, type = 0, flags_a = 0;
    int32_t x = 0, y = 0, z = 0;  // 16.16
    int32_t pitch = 0, yaw = 0, roll = 0;
    int32_t health = 0;
    int32_t anim = -1, frame = -1;
};

void write_trace_line(std::FILE* f, uint32_t tick, uint32_t vblank, const TracePlayer& p,
                      const std::vector<TraceEnt>& ents);

}  // namespace valor
