#include "valor/trace.h"

#include <cinttypes>

namespace valor {

void write_trace_line(std::FILE* f, uint32_t tick, uint32_t vblank, const TracePlayer& p,
                      const std::vector<TraceEnt>& ents) {
    std::fprintf(f, "{\"tick\":%" PRIu32 ",\"vblank\":%" PRIu32 ",\"player\":[%d,%d,%d,%d,%d,%d,%" PRIu32 "],\"ents\":[",
                 tick, vblank, p.look_yaw, p.look_pitch, p.slot, p.weapon, p.clip, p.reserve, p.flags_b);
    for (size_t i = 0; i < ents.size(); i++) {
        const TraceEnt& e = ents[i];
        std::fprintf(f, "%s[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%d,%d,%d,%d,%d,%d,%d,%d,%d]", i ? "," : "",
                     e.handle, e.type, e.flags_a, e.x, e.y, e.z, e.pitch, e.yaw, e.roll, e.health, e.anim, e.frame);
    }
    std::fputs("]}\n", f);
}

}  // namespace valor
