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

// An animated light's state (level entry +0x8, +0xC, +0x10).
struct LightState {
    uint32_t step = 0, colour = 0;
    int32_t ticks = 0;
};

class Session {
public:
    bool load(const std::string& data_dir, int mission, int level, std::string* err);
    void tick(const PlayerInput& in);

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

    void lights_update();
};

}  // namespace valor
