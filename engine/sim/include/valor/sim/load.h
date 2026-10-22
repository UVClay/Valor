// The level's starting state built from the disc files (docs/re/levels.md Loading, shell.md): what
// the original's LEVEL overlay does from its entry to the first tick (level_entry, level_load_all,
// level_load, level_start), ported into the engine's memory, so the engine runs without a RAM
// dump. The input besides the disc is the save data the shell hands over (`g_game`, 0x780 bytes:
// the profile, mission and level, options, carried ammunition).
#pragma once

#include <memory>
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

class Engine;

// The game's heap at run time, for an engine whose state was built from the disc
// (Engine::load_disc): the engine's allocations go through the exe's allocator, entity blocks and
// pose buffers are freed two frames late as the destructors ask (0x8008A4F8, counted down by
// 0x8008A580 at each frame's start), and model swaps allocate the pose buffers exactly
// (model_instance_init, 0x8006A96C).
class GameHeap {
public:
    explicit GameHeap(Engine& eng);
    ~GameHeap();
    U calloc(I size);
    void free(U p);
    void free_later(U p, I frames);  // 0x8008A4F8 (frames 0: now)
    void frame();                    // 0x8008A580
    void model_swap(U e, U model, I flags);
    // The removal's part of the destructors (+0x384) for an entity on the heap: its instance's
    // pose buffers and its brain released, its block freed two frames late.
    void destroy(U e);
    // Streaming: tsp_unload (0x8008C8F4, the slot's instance buffer freed) and a read's end (the
    // chunk file into the slot's buffer, tsp_parse, its instance: 0x8008C798).
    void tsp_unload(I slot);
    void tsp_load(I chunk_file, I slot);
    // level_start (0x80061E14) again on the running state (level_restart 0x800624AC, after a
    // death): the caller has removed the entities level_start removes.
    void level_start();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct LevelBuild {
    std::vector<std::string> notes;  // what was not built, by step
    U rand_seed = 0;                 // in: rand's seed at the level's entry (the shell's srand + its draws)
    I rand_calls = 0;                // out: the draws the build made
};

// The save data a fresh campaign hands over (shell.md, game_defaults and new_game), set to start
// mission m, level l (1-based) with the level's row giving every weapon `ammo` rounds (-1: the
// fresh game's none) and 128.0 health; controls as the defaults (layout 1, DualShock).
std::vector<uint8_t> game_defaults(int mission, int level, int ammo);

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
