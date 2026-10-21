// What the engine takes from outside, read from one replay of a recording (tools/sim/engine_feed.py):
// the player's input per tick (the pad words and the control roles' values), the frame structure
// (which ticks ran in one drawn frame), and the random draws by the function that drew them.
#pragma once

#include <array>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "valor/sim/common.h"

namespace valor::sim {

// Function starts of the exe and LEVEL.BIN (the recompiler's tables).
struct Functions {
    std::vector<U> starts;
    bool load(const std::string& root);
    U of(U addr) const;
};

struct Draw {
    U fn, ra;
    I value, vblank;
};

class Feed {
public:
    bool load(const std::string& root, const std::string& log_path);
    std::vector<std::pair<I, std::string>> marks;  // (vblank, "m:l")
    std::set<std::string> analog;                  // levels whose start found the pad analog
    std::map<std::pair<std::string, I>, std::vector<Draw>> rand;
    std::map<std::pair<std::string, I>, std::array<U, 4>> pad;
    std::map<std::pair<std::string, I>, std::map<std::array<int, 3>, I>> roles;  // (port, role, src)
    // the vblank wait's idle work, per frame in order: the job state of each frame_idle_job call,
    // -1 for each frame_idle_loop call; by (level, the frame's last tick), one list per frame
    // ("idlef": paused frames share the tick)
    std::map<std::pair<std::string, I>, std::vector<std::vector<I>>> idle;

    I role_alt(const std::string& level, I tick, int port, int role) const;
    I role(const std::string& level, I tick, int port, int role) const;
    Functions fns;
};

// The draws of one tick, taken by function in the order the original made them.
class Rand {
public:
    Rand(const Feed& f, std::string level) : feed_(f), level_(std::move(level)) {}
    void start_tick(I tick);
    I draw(std::initializer_list<U> functions);
    I draw(const std::vector<U>& functions);
    std::vector<Draw> left;
    std::vector<std::pair<I, std::vector<U>>> misses;

private:
    const Feed& feed_;
    std::string level_;
    I tick_ = 0;
};

// The functions whose draws the engine takes, by the system that draws (objects.md, Randomness).
const std::vector<U>& FN(const char* name);

// One line of the VALOR_TRACE_STATE trace.
struct StateLine {
    I tick = 0, vblank = 0;
    bool has_player = false;
    std::vector<I> player;
    std::vector<std::vector<I>> ents;
};
std::vector<StateLine> read_trace(const std::string& path);
std::string state_json(const StateLine& s);

}  // namespace valor::sim
