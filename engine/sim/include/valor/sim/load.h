// The level's starting state built from the disc files (docs/re/levels.md Loading, shell.md): what
// the original's LEVEL overlay does from its entry to the first tick (level_entry, level_load_all,
// level_load, level_start), ported into the engine's memory, so the engine runs without a RAM
// dump. The input besides the disc is the save data the shell hands over (`g_game`, 0x780 bytes:
// the profile, mission and level, options, carried ammunition).
#pragma once

#include <string>
#include <vector>

#include "valor/sim/common.h"

namespace valor::sim {

// The exe's heap (0x80036B20 InitHeap, 0x80036C14 malloc, 0x80036DC4 calloc, 0x80036E44 free): a
// circular free list of 16-byte-headed blocks {blocks, next | tag, flags | serial, check}, blocks
// taken from the tail of the first that fits. Its globals live at gp (0x80035D1C) + 0x3F24...
struct Heap {
    Mem& m;
    void init(U base, U size);
    U malloc(I size, I tag = 0, I flags = 0);
    U calloc(I size, I count);
    void free(U p);
};

struct LevelBuild {
    std::vector<std::string> notes;  // what was not built, by step
    U rand_seed = 0;                 // in: rand's seed at the level's entry (the shell's srand + its draws)
    I rand_calls = 0;                // out: the draws the build made
};

// rand's seed n draws before `seed` (the LCG run backwards).
inline U rand_seed_back(U seed, I n) {
    U inv = 1, a = 0x41C64E6Du;
    for (int i = 0; i < 5; i++) inv *= 2 - a * inv;
    for (I k = 0; k < n; k++) seed = (seed - 0x3039u) * inv;
    return seed;
}

// Engine::load_disc (engine.h) builds the state at the end of level_start (the RAM before the
// level's first tick) for the engine's mission and level; game: the 0x780-byte g_game block.

}  // namespace valor::sim
