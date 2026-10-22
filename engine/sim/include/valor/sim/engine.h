// The game logic as one free-running engine, checked against the original (tools/sim/engine.py,
// ported). The engine carries its own state from tick to tick, in the original's memory layout.
// What it takes from outside is what a new engine would get from outside (feed.h): the player's
// input per tick, the frame structure, the CD's timing for streamed world chunks, the random
// draws by the function that made them, and which models the renderer drew. Its starting state is
// one RAM dump of the original taken just after the level started, standing for what loading the
// level's files produces.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "valor/gte.h"
#include "valor/sim/common.h"
#include "valor/sim/feed.h"
#include "valor/sim/level.h"
#include "valor/world.h"

namespace valor::sim {

class Audio;
struct Drawn;

// The world's collision: the level's TSP chunks in the slots the CD had loaded at each frame (the
// original's timing, build/engine/timing.log).
class Collision {
public:
    bool load(Engine& eng, const std::string& data_dir, const std::string& timing_log, int mission, int level);
    void at(I vb);       // the table as the original had it while running the frame at vblank vb
    void skip_to(I vb);  // past the changes logged up to vb
    Floor floor(const I pos[3]) const;
    // world_sweep(from, to) with integer points: hit point, normal; false if nothing is hit.
    bool sweep(const I frm[3], const I to[3], I hit[3], I normal[3]) const;
    World w;

private:
    void apply();
    Engine* eng_ = nullptr;
    struct Event {
        I vblank;
        int name;  // 0 k, 1 ld
        U off, value;
    };
    std::vector<Event> events_;
    size_t i_ = 0;
    std::map<U, U> words_;
    I loading_ = -1;
};

class Engine {
public:
    Engine(const std::string& root, const std::string& level, const Feed& feed);
    ~Engine();
    bool load(const std::string& seed_path, std::string* err);
    // The level's start built from the disc files and the save data (load.cpp).
    bool load_disc(const std::vector<uint8_t>& game, struct LevelBuild& out, std::string* err);

    std::string root, level;
    int mission_n = 0, level_n = 0;
    const Feed& feed;
    std::vector<std::pair<I, std::vector<I>>> frames;  // (vblank, [tick, ...])
    Mem m;
    Level L;
    Rand rand;
    Gte gte;
    std::map<std::string, I> stats;
    std::map<std::string, std::vector<std::pair<I, std::string>>> notes;
    std::unordered_map<U, Ent*> ents;  // address -> the level logic's entity
    Collision world;
    std::unique_ptr<Audio> audio;
    std::unique_ptr<LevelLogic> logic;
    I tick_no = 0, vblank = 0;
    std::optional<I> vblank_prev;
    I pad_now = 0, pad_prev = 0;
    std::map<std::string, std::string> dumps;  // "TICK" or "TICKa" -> path

    void note(const std::string& kind, const std::string& detail = "");

    // -- memory blocks
    U alloc(I n);
    void free(U a);

    // -- the entity list (entities.md, Lists and handles)
    std::vector<U> entities() const { return m.entities(); }
    void link_front(U e);
    void link_end(U e);
    void unlink(U e);
    U handle_alloc(U e);
    U resolve(I h) const { return h ? U(m.u32(G_HANDLES + 4 * (h & 0xFFFF))) : 0; }
    bool handle_dead(I h) const;
    void handles_free_removed();

    // -- creating and removing
    U entity_create(I etype, I obj_size = 0, U model = 0, U addr = 0, I parts = -1, I channels = 0, I list_n = 0,
                    bool instance = false);
    void entity_defaults(U e);
    void instance_init(U e, U model);
    void model_swap(U e, U model, I flags);
    // While the level is built from the disc (load.cpp), the original's heap: model_swap releases
    // the old pose buffer and allocates the new one exactly (model_instance_init).
    std::function<void(U e, U model, I flags)> swap_hook;
    // The game's heap at run time, once the state was built from the disc (load.h).
    std::unique_ptr<class GameHeap> gheap;
    std::map<I, size_t> idle_used_;  // the idle records consumed, by tick
    // Live play (no recording): see start_live and live_frame; in_view answers the draw pass's
    // question (is the entity drawn this frame) for the host's camera.
    std::function<bool(U e)> live_view;
    void start_live(std::function<bool(U e)> in_view);
    void live_frame(I vblanks = 2);
    // Live play's chunk streaming (levels.md, Streaming): chunk_stream (0x8005439C) on the
    // record last entered, the read's end at a frame's start (0x8008C798), the collision slots
    // following the slot table.
    void chunk_stream();
    I stream_frames_left = 0, stream_slot = -1;
    void chunks_apply();
    void stream_end();
    // level_restart (0x800624AC), for live play after a death (the level flags' bit 2): every
    // entity but the player's soldier, the camera, the world and type 8 removed, level_start.
    void level_restart();
    I restarts = 0;
    // The level's end in live play (flags 4 or 8: level_exit 0x80062298 with level_end_carry, then
    // the shell's progress_commit as CONTINUE does: carry_to_next_level, campaign_advance), and the
    // next level built from the disc on the carried save data. level_changes counts them;
    // campaign_done is set past the campaign's last level.
    bool level_transition();
    I level_changes = 0;
    bool campaign_done = false;
    void remove(U e, const std::string& why = "");

    // -- input
    bool action_pressed(int a, int port = 0) const;
    bool action_held(int a, int port = 0) const;
    I role(int r, int port = 0) const { return feed.role(level, tick_no, port, r); }
    I role_alt(int r, int port = 0) const { return feed.role_alt(level, tick_no, port, r); }

    // -- the tick and the frame
    void tick(bool first_in_frame);
    void entities_update();
    void update(U e);
    void matrix(U e);
    void zones_use(U e);
    void zones_update(U e);
    void timer_message();
    void end_frame();
    void idle_job(I state);  // frame_idle_job (0x80090A30)
    void idle_loop();        // frame_idle_loop (0x80090AEC)
    void idle_work();        // a frame's idle work as the recording has it
    // Every frame of the recording's level (to: the last tick, 0 all); each tick's state line goes
    // to `line`.
    void run(I to, const std::function<void(const StateLine&)>& line);
    StateLine state_line() const;

private:
    U heap_next_ = HEAP;
    std::map<I, std::vector<U>> free_blocks_;
    std::unordered_map<U, I> block_size_;
};

}  // namespace valor::sim
