#include "valor/session.h"

#include <algorithm>
#include <cstdio>

#include "valor/fixed.h"
#include "valor/io.h"

namespace valor {

namespace {

struct WorldBehaviour : Behaviour {
    // The world entity's think (0x80053C00) steps the animated lights.
    void think(Session& s, Entity&) override { s.lights_update(); }
};

struct SoldierBehaviour : Behaviour {
    void think(Session& s, Entity& e) override { s.player_think(e); }
    void move(Session& s, Entity& e) override { s.player_move(e); }
};

struct LadderBehaviour : Behaviour {
    Ladder l;
    explicit LadderBehaviour(const Ladder& x) : l(x) {}
    // ladder_think (0x800611D0) with dist2d_approx (0x80050658): |dx| + |dz| - min / 2.
    void think(Session& s, Entity&) override {
        Player& p = s.player();
        int32_t dx = int32_t(iabs(int64_t(p.w.pos[0]) - l.pos[0])), dz = int32_t(iabs(int64_t(p.w.pos[2]) - l.pos[2]));
        int32_t d = (dx >> 16) + (dz >> 16) - (std::min(dx, dz) >> 17);
        if (l.radius >= d && p.w.pos[1] >= l.top) p.inv.ladder = l.top;
    }
};

}  // namespace

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
    player_.mission = m, player_.level = l;
    record_state_.assign(records_.records.size(), 0);
    held_ladder_ = -1;
    lights_.assign(info_.lights.size(), LightState{});
    for (size_t i = 0; i < lights_.size(); i++) lights_[i].step = info_.lights[i].step;
    // level_load's entities: handles in the order the original makes them (the world, the camera,
    // the soldier), the list camera, world, soldier (a level start's 0x10000, 0x20001, 0x30002).
    entities_ = Entities{};
    Entity* world = entities_.create(kTypeWorld, std::make_unique<WorldBehaviour>());
    Entity* camera = entities_.create(kTypeCamera, nullptr);
    Entity* soldier = entities_.create(kTypeSoldierPlayer, std::make_unique<SoldierBehaviour>());
    soldier->flags_a = kFlagAPlayer;
    entities_.set_head(camera);
    entities_.link_front(soldier);
    entities_.link_front(world);
    tick_ = 0;
    vbl_timer_ = 0;
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

Entity* Session::adopt(uint32_t handle, int32_t type, uint32_t flags_a, int record, const Ladder* ladder) {
    std::unique_ptr<Behaviour> b;
    if (type == kTypeWorld) b = std::make_unique<WorldBehaviour>();
    if (type == kTypeSoldierPlayer) b = std::make_unique<SoldierBehaviour>();
    if (type == kTypeLadder && ladder) b = std::make_unique<LadderBehaviour>(*ladder);
    Entity* e = entities_.adopt(handle, type, std::move(b));
    if (!e) return nullptr;
    e->flags_a = flags_a;
    e->record = record;
    return e;
}

// ladder_spawn (0x80061244), from record_spawn_type(0x4C) on entering the record.
void Session::ladder_spawn(int ri) {
    if (held_ladder_ == ri)
        held_ladder_ = -1;
    else
        ladder_release();
    const Record& r = records_.records[ri];
    Ladder l{{}, r.shape[1], 0};
    for (int k = 0; k < 3; k++) l.pos[k] = s32(int64_t(r.pos[k]) << 16);
    l.top = s32(int64_t(l.pos[1]) - (int64_t(r.shape[2]) << 16));
    Entity* e = entities_.create(kTypeLadder, std::make_unique<LadderBehaviour>(l));
    if (!e) return;
    e->record = ri;
    e->flags_a = kFlagANoMove;
    entities_.link_front(e);
    record_state_[ri] |= 1;
}

// 0x8006149C: the held ladder's entity is removed (if it still has one).
void Session::ladder_release() {
    int h = held_ladder_;
    held_ladder_ = -1;
    if (h < 0 || !(record_state_[h] & 1)) return;
    for (Entity* e : entities_.list())
        if (e->record == h) {
            entities_.remove(*this, e);
            break;
        }
    record_state_[h] &= ~1u;
}

void Session::send_list(const std::vector<uint32_t>& msgs, int record, uint32_t actor) {
    for (uint32_t m : msgs) queue_.push({m, record, 0, actor});
}

// The player's enter and leave (zones_events, levels.md Triggers): kind-0 records stream the world
// (loads are instant here; the original reads them from the CD over seconds) or make a ladder;
// others send their lists' messages to the level logic.
void Session::zone_event(int ri, bool enter) {
    const Record& r = records_.records[ri];
    uint32_t actor = entities_.find_type(kTypeSoldierPlayer) ? entities_.find_type(kTypeSoldierPlayer)->handle : 0;
    if (r.kind != 0) {
        send_list(enter ? r.enter_msgs : r.leave_msgs, ri, actor);
        return;
    }
    if (enter) {
        if (r.cls == kClassStream) streaming_.trigger(&r);
        if (r.cls == kClassLadder && !(record_state_[ri] & 1)) ladder_spawn(ri);
    } else if (r.cls == kClassLadder && held_ladder_ != ri) {
        // 0x80061518: leaving a ladder holds it, releasing the one held before.
        if (held_ladder_ >= 0) ladder_release();
        held_ladder_ = ri;
    }
}

void Session::player_think(Entity& e) {
    Player& pl = player_;
    bool use = !pl.dead && pl.controls.action_pressed(kUse);
    pl.update(events_);
    e.flags_b = pl.w.flags_b;
    if (use) {
        // player_use (0x80084670): the Use lists of the records whose bits the last zone update
        // set (the think reads the pad before the move).
        const std::vector<uint16_t>& list = records_.zone(zones_.zone);
        for (size_t k = 0; k < list.size() && k < 32; k++) {
            const Record& r = records_.records[list[k]];
            if (r.kind >= 1 && r.kind <= 6 && r.use && (zones_.bits >> k & 1)) send_list(r.use_msgs, list[k], e.handle);
        }
    }
}

void Session::player_move(Entity&) {
    Player& pl = player_;
    if (pl.dead) return;
    int slots[4];
    streaming_.collision_slots(slots);
    world_.set_slots_by_number(slots);
    pl.move(world_);
    // zones_update (0x800845A8) after the move.
    const Walker& w = pl.w;
    int32_t p[3] = {s16(w.pos[0] >> 16), s16(w.pos[1] >> 16), s16(w.pos[2] >> 16)};
    zones_.update(records_, w.zone, p, true, [&](int ri, bool enter) { zone_event(ri, enter); });
}

void Session::tick(const PlayerEvents& ev) {
    events_ = ev;
    tick_++;
    // entities_update: in list order; an entity removed by an earlier one this tick is skipped.
    std::vector<Entity*> walk = entities_.list();
    for (Entity* e : walk) {
        if (e->flags_a & kFlagARemoved) continue;
        e->behaviour->think(*this, *e);
        if (e->flags_a & kFlagARemoved) continue;
        if (!(e->flags_a & kFlagANoMove)) e->behaviour->move(*this, *e);
    }
    // entities_collide: not ported (touches and hits arrive as events). texts_update: drawing.
    // The 1/3-second message.
    if (vbl_timer_ >= 10) {
        vbl_timer_ -= 20;
        queue_.push({0x59, -1, 0, 0});
    }
}

void Session::end_frame() {
    delivered_.clear();
    queue_.run_all([&](const Message& m) {
        if (on_message)
            on_message(m);
        else
            delivered_.push_back(m);
    });
    // Spawn retries: not ported (the level logic spawns).
    entities_.free_removed();
    streaming_.run();  // the idle job's chunk_stream
}

}  // namespace valor
