#include "valor/session.h"

#include <cstdio>

#include "valor/io.h"

namespace valor {

bool Session::load(const std::string& data_dir, int m, int l, std::string* err) {
    char rel[64];
    std::snprintf(rel, sizeof rel, "/MSN%d/LVL%d/%d_%d.BSD", m, l, m, l);
    std::vector<uint8_t> bsd;
    if (!info_.load(data_dir, m, l, err) || !world_.load(data_dir, m, l, err)) return false;
    if (!read_file(data_dir + rel, bsd) || !records_.load(bsd, err)) return false;
    streaming_.start(int(info_.first_chunk), int(info_.chunk_slots));
    world_.set_slots_by_number(streaming_.slot_chunk);
    zones_ = ZoneTracker{};
    player_.spawn(world_, info_);
    lights_.assign(info_.lights.size(), LightState{});
    for (size_t i = 0; i < lights_.size(); i++) lights_[i].step = info_.lights[i].step;
    return true;
}

// lights_update (0x80053AC0), from the world entity's think: count down; at 0 the next step's
// colour and duration (the top byte), wrapping at the count. The colour is 0 until the first tick.
void Session::lights_update() {
    for (size_t i = 0; i < lights_.size(); i++) {
        const std::vector<uint32_t>& steps = info_.lights[i].steps;
        if (steps.empty()) continue;
        LightState& l = lights_[i];
        if (--l.ticks <= 0) {
            if (++l.step >= steps.size()) l.step = 0;
            l.ticks = int32_t(steps[l.step] >> 24);
            l.colour = steps[l.step];
        }
    }
}

void Session::tick(const PlayerInput& in) {
    lights_update();
    int slots[4];
    streaming_.collision_slots(slots);
    world_.set_slots_by_number(slots);
    player_.tick(world_, in);
    // zones_update after the move: kind-0 records stream the world (loads are instant here; the
    // original reads them from the CD over seconds, levels.md Triggers).
    const Walker& w = player_.walker();
    int32_t p[3] = {s16(w.pos[0] >> 16), s16(w.pos[1] >> 16), s16(w.pos[2] >> 16)};
    zones_.update(records_, w.zone, p, true, [&](int ri, bool enter) {
        const Record& r = records_.records[ri];
        if (enter && r.kind == 0 && r.cls == kClassStream) streaming_.trigger(&r);
    });
    streaming_.run();
}

}  // namespace valor
