// A level in play: what the core owns between the host's ticks. The order of a tick follows
// docs/re/objects.md ("The frame and the tick") for the parts ported so far: the player's
// update and move, his zones (enter and leave, streaming), the idle job's chunk_stream.
#pragma once

#include <string>

#include "valor/level.h"
#include "valor/player.h"
#include "valor/records.h"
#include "valor/world.h"

namespace valor {

// A ladder's entity (type 0x4C, ladder_spawn 0x80061244): made when the player enters a ladder
// record, it holds the record's radius and top; while the player is within the radius and below
// the top, its think (0x800611D0) sets inv+0x70, and player_controls climbs (levels.md, Triggers).
struct Ladder {
    int record;
    int32_t pos[3];   // the record's position, 16.16
    int32_t radius;   // record +0x2A, whole units
    int32_t top;      // y - (record +0x2C << 16)
};

// An animated light's state (level entry +0x8, +0xC, +0x10).
struct LightState {
    uint32_t step = 0, colour = 0;
    int32_t ticks = 0;
};

class Session {
public:
    bool load(const std::string& data_dir, int mission, int level, std::string* err);
    // One tick. The host sets player().controls (the pad, its own axis values) before; events
    // stand for systems not ported yet (player.h).
    void tick(const PlayerEvents& ev = {});

    // Records' state words (bit 0 a live entity; levels.md, Entity records), the ladders.
    std::vector<uint32_t>& record_state() { return record_state_; }
    std::vector<Ladder>& ladders() { return ladders_; }  // in the entity list's order (newest first)
    int& held_ladder() { return held_ladder_; }          // 0x8009A5B8, a record index or -1
    Streaming& streaming_state() { return streaming_; }
    ZoneTracker& zone_state() { return zones_; }
    // The records whose Use lists player_use sent this tick; their messages are the level logic's.
    const std::vector<int>& use_records() const { return use_records_; }

    const LevelInfo& info() const { return info_; }
    const World& world() const { return world_; }
    const Records& records() const { return records_; }
    const Streaming& streaming() const { return streaming_; }
    const ZoneTracker& zones() const { return zones_; }
    const std::vector<LightState>& lights() const { return lights_; }
    Player& player() { return player_; }
    const Player& player() const { return player_; }

private:
    LevelInfo info_;
    World world_;
    Records records_;
    Streaming streaming_;
    ZoneTracker zones_;
    Player player_;
    std::vector<LightState> lights_;
    std::vector<uint32_t> record_state_;
    std::vector<Ladder> ladders_;
    int held_ladder_ = -1;
    std::vector<int> use_records_;

    void lights_update();
    void ladder_spawn(int record);
    void ladder_release();
    void zone_event(int record, bool enter);
};

}  // namespace valor
