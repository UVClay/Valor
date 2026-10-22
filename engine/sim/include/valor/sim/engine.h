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
#include "valor/sim/campaign.h"
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
    bool sweep(const I frm[3], const I to[3], I hit[3], I normal[3], SweepFace* face = nullptr) const;
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
    // The recording's inputs: build/engine (the session), or ENG_INPUTS=DIR for another recording
    // (orig.log, orig.jsonl, seed/; frames.log, timing.log and drawn.log if kept apart from
    // orig.log). input_log(name): that file, else orig.log.
    std::string inputs() const;
    std::string input_log(const std::string& name) const;
    // The GTE's projection: a recording's own (its "gte: projection aspect A, world FOV F deg"
    // line; the session's 21:9 at 110), live play the session's.
    void set_recording_projection();
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
    I frame_ticks = 1;  // the drawn frame's ticks (the hit wedges' countdown)
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
    // 0 when the heap refuses; with iflags >= 0 (and the game's heap) model_instance_init's block.
    U entity_create(I etype, I obj_size = 0, U model = 0, U addr = 0, I parts = -1, I channels = 0, I list_n = 0,
                    bool instance = false, I iflags = -1);
    void entity_defaults(U e);
    void instance_init(U e, U model);
    bool model_swap(U e, U model, I flags);  // false: the heap refused
    // While the level is built from the disc (load.cpp), the original's heap: model_swap releases
    // the old pose buffer and allocates the new one exactly (model_instance_init).
    std::function<void(U e, U model, I flags)> swap_hook;
    // The game's heap at run time, once the state was built from the disc (load.h).
    std::unique_ptr<class GameHeap> gheap;
    struct Heap* load_heap = nullptr;  // load_disc's exact heap while it builds (the level start's spawns)
    // model_instance_init (0x8006A96C) on load_disc's loader while it builds (GameHeap's after)
    std::function<bool(U inst, U model, I flags, U e)> load_instance_init;
    std::map<I, size_t> idle_used_;  // the idle records consumed, by tick
    std::map<I, size_t> keys_used_;  // the SPU key records consumed, by tick
    // Live play (no recording): see start_live and live_frame; in_view answers the draw pass's
    // question (is the entity drawn this frame) for the host's camera.
    std::function<bool(U e)> live_view;
    void start_live(std::function<bool(U e)> in_view);
    void live_frame(I vblanks = 2);
    // A frame of the pause menu (g_paused, objects.md: The frame): no tick, the 1/3-second timer
    // held at 0, but the frame drawn and waited for: the out-of-sight counters run and the idle
    // work goes on (messages, spawn retries, handles freed).
    void live_paused_frame(I vblanks = 2);
    // Live play's chunk streaming (levels.md, Streaming): chunk_stream (0x8005439C) on the
    // record last entered, the read's end at a frame's start (0x8008C798), the collision slots
    // following the slot table.
    void chunk_stream();
    I stream_slot = -1;
    double stream_done = 0;   // live: the vblank the read under way ends (the music's refills)
    I music_vblank = -1;      // the vblank the level's music started (music_start), -1 none
    double refill_after(double v) const;
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
    // With auto_advance off (a host with its own shell), the level's end stops the engine with
    // level_over and its results; the host commits them to its Campaign and calls load_next.
    bool auto_advance = true, level_over = false;
    struct LevelResults results;
    LevelResults level_results();
    std::vector<uint8_t> save_data() const;  // g_game as the level has it
    bool load_next(const std::vector<uint8_t>& game, std::string* err);
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
    const std::array<uint8_t, 24>* spu_keys();  // the frame's SPU key status as recorded
    // The seed frame's sound_update: in the seed already when its voice states are the SPU
    // record's (the record is passed over); otherwise (a dump that landed in the frame's wait, as
    // after a restart's long first frame) it runs now.
    void seed_sound_update();
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
