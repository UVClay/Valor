#include "valor/session.h"

#include <algorithm>
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
    player_.controls.cfg = PadConfig::fresh_card();
    player_.controls.pad.analog = 1;
    player_.spawn(world_, info_);
    record_state_.assign(records_.records.size(), 0);
    ladders_.clear();
    held_ladder_ = -1;
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

// ladder_spawn (0x80061244), from record_spawn_type(0x4C) on entering the record.
void Session::ladder_spawn(int ri) {
    if (held_ladder_ == ri)
        held_ladder_ = -1;
    else
        ladder_release();
    const Record& r = records_.records[ri];
    Ladder l{ri, {}, r.shape[1], 0};
    for (int k = 0; k < 3; k++) l.pos[k] = s32(int64_t(r.pos[k]) << 16);
    l.top = s32(int64_t(l.pos[1]) - (int64_t(r.shape[2]) << 16));
    ladders_.insert(ladders_.begin(), l);  // spawned entities go first in the list
    record_state_[ri] |= 1;
}

// 0x8006149C: the held ladder's entity is removed (if it still has one).
void Session::ladder_release() {
    int h = held_ladder_;
    held_ladder_ = -1;
    if (h < 0 || !(record_state_[h] & 1)) return;
    for (size_t i = 0; i < ladders_.size(); i++)
        if (ladders_[i].record == h) {
            ladders_.erase(ladders_.begin() + i);
            break;
        }
    record_state_[h] &= ~1u;
}

// The player's enter and leave (zones_events, levels.md Triggers): kind-0 records stream the world
// (loads are instant here; the original reads them from the CD over seconds) or make a ladder;
// others' lists are messages for the level logic (not ported).
void Session::zone_event(int ri, bool enter) {
    const Record& r = records_.records[ri];
    if (r.kind != 0) return;
    if (enter) {
        if (r.cls == kClassStream) streaming_.trigger(&r);
        if (r.cls == kClassLadder && !(record_state_[ri] & 1)) ladder_spawn(ri);
    } else if (r.cls == kClassLadder && held_ladder_ != ri) {
        // 0x80061518: leaving a ladder holds it, releasing the one held before.
        if (held_ladder_ >= 0) ladder_release();
        held_ladder_ = ri;
    }
}

void Session::tick(const PlayerEvents& ev) {
    use_records_.clear();
    // The entities in list order (objects.md, The frame and the tick): spawned ones first (the
    // ladders), the world (the lights), the player's soldier.
    Player& pl = player_;
    for (const Ladder& l : ladders_) {
        // ladder_think (0x800611D0) with dist2d_approx (0x80050658): |dx| + |dz| - min / 2.
        int32_t dx = int32_t(iabs(int64_t(pl.w.pos[0]) - l.pos[0])), dz = int32_t(iabs(int64_t(pl.w.pos[2]) - l.pos[2]));
        int32_t d = (dx >> 16) + (dz >> 16) - (std::min(dx, dz) >> 17);
        if (l.radius >= d && pl.w.pos[1] >= l.top) pl.inv.ladder = l.top;
    }
    lights_update();
    int slots[4];
    streaming_.collision_slots(slots);
    world_.set_slots_by_number(slots);
    bool use = !pl.dead && pl.controls.action_pressed(kUse);
    player_.tick(world_, ev);
    if (use) {
        // player_use (0x80084670): the Use lists of the records whose bits the last zone update
        // set (the think reads the pad before the move).
        const std::vector<uint16_t>& list = records_.zone(zones_.zone);
        for (size_t k = 0; k < list.size() && k < 32; k++) {
            const Record& r = records_.records[list[k]];
            if (r.kind >= 1 && r.kind <= 6 && r.use && (zones_.bits >> k & 1)) use_records_.push_back(list[k]);
        }
    }
    // zones_update after the move.
    const Walker& w = pl.w;
    int32_t p[3] = {s16(w.pos[0] >> 16), s16(w.pos[1] >> 16), s16(w.pos[2] >> 16)};
    zones_.update(records_, w.zone, p, true, [&](int ri, bool enter) { zone_event(ri, enter); });
    streaming_.run();
}

}  // namespace valor
