// A level in play: the entity list walked once per tick in the original's order, and the work
// of the end of the frame (docs/re/objects.md, The frame and the tick; entities.md, The tick).
//
//   per tick:  g_tick + 1; every entity in list order: think, then (unless flags A 0x20000)
//              its move, which for the player and walkers ends with zones_update; then
//              entities_collide (not ported: its effects come in as events), texts_update, the
//              1/3-second message 0x59;
//   per frame: the queued messages (to the level logic's handler), spawn retries (not ported),
//              the removed entities' handles freed, the chunk streaming check.
//
// The entities the core runs: the camera (type 2, its think not ported), the world (type 3: the
// animated lights), the player's soldier (type 1: player.h), ladders (type 0x4C). Others (soldiers,
// pickups, effects...) are entities with no behaviour until their systems are ported.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "valor/camera.h"
#include "valor/entities.h"
#include "valor/gte.h"
#include "valor/level.h"
#include "valor/player.h"
#include "valor/records.h"
#include "valor/world.h"

namespace valor {

// An animated light's state (level entry +0x8, +0xC, +0x10).
struct LightState {
    uint32_t step = 0, colour = 0;
    int32_t ticks = 0;
};

// A ladder's entity (type 0x4C, ladder_spawn 0x80061244): made when the player enters a ladder
// record, it holds the record's radius and top; while the player is within the radius and below
// the top, its think (0x800611D0) sets inv+0x70, and player_controls climbs (levels.md, Triggers).
struct Ladder {
    int32_t pos[3];  // the record's position, 16.16
    int32_t radius;  // record +0x2A, whole units
    int32_t top;     // y - (record +0x2C << 16)
};

class Session {
public:
    bool load(const std::string& data_dir, int mission, int level, std::string* err);

    // One tick. The host sets player().controls (the pad, its own axis values) before; events
    // stand for systems not ported yet (player.h).
    void tick(const PlayerEvents& ev = {});
    // Vblanks went by (60 per second): the 1/3-second message counts them (the vblank handler).
    void vblanks(int n) { vbl_timer_ += n; }
    // The end of a drawn frame: the message queue, spawn retries, handles, chunk streaming.
    void end_frame();

    const LevelInfo& info() const { return info_; }
    const World& world() const { return world_; }
    const Records& records() const { return records_; }
    const Streaming& streaming() const { return streaming_; }
    const ZoneTracker& zones() const { return zones_; }
    const std::vector<LightState>& lights() const { return lights_; }
    Player& player() { return player_; }
    const Player& player() const { return player_; }
    Entities& entities() { return entities_; }
    Camera& camera() { return camera_; }
    const Gte& gte() const { return gte_; }
    Gte& gte() { return gte_; }
    // The aim (aim_update) runs once the camera object holds a level's state (a seed from RAM): the
    // targets are the entities as the aim sees them, the arms the first-person model's gun bones.
    void enable_aim();
    std::vector<AimTarget>& aim_targets() { return aim_targets_; }
    MessageQueue& queue() { return queue_; }
    uint32_t tick_count() const { return tick_; }  // g_tick (0x8009A5DC)

    // The level logic's message handler (not ported): by default the delivered messages are kept
    // in delivered() until the next end_frame().
    std::function<void(const Message&)> on_message;
    const std::vector<Message>& delivered() const { return delivered_; }

    // Records' state words (bit 0 a live entity; levels.md, Entity records).
    std::vector<uint32_t>& record_state() { return record_state_; }
    int& held_ladder() { return held_ladder_; }  // 0x8009A5B8: a record index, or -1
    Streaming& streaming_state() { return streaming_; }
    ZoneTracker& zone_state() { return zones_; }
    void set_tick(uint32_t t) { tick_ = t; }
    void set_vbl_timer(int32_t v) { vbl_timer_ = v; }
    // An entity as the original had it (a seed from its RAM, or one a system not ported made),
    // with the core's behaviour for its type (a ladder's data in `ladder`). Not linked.
    Entity* adopt(uint32_t handle, int32_t type, uint32_t flags_a, int record, const Ladder* ladder = nullptr);

    // Internal: the behaviours call back.
    void lights_update();
    void player_think(Entity& e);
    void player_move(Entity& e);
    void camera_think(Entity& e);

private:
    LevelInfo info_;
    World world_;
    Records records_;
    Streaming streaming_;
    ZoneTracker zones_;
    Player player_;
    std::vector<LightState> lights_;
    std::vector<uint32_t> record_state_;
    int held_ladder_ = -1;
    Entities entities_;
    Gte gte_;
    Camera camera_;
    std::vector<AimTarget> aim_targets_;
    MessageQueue queue_;
    std::vector<Message> delivered_;
    uint32_t tick_ = 0;
    int32_t vbl_timer_ = 0;
    PlayerEvents events_;

    void ladder_spawn(int record);
    void ladder_release();
    void zone_event(int record, bool enter);
    void send_list(const std::vector<uint32_t>& msgs, int record, uint32_t actor);
};

}  // namespace valor
