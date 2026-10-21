// The level's entity records, their volumes, the floor zones' record lists and the player's zone
// tracking (docs/re/levels.md: Entity records, Triggers), and the world chunk streaming the
// kind-0 records drive (formats.md, BSD: chunk streaming records). Ported from tools/sim/level_vm.py.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace valor {

constexpr uint32_t kClassStream = 0x4D078F23;  // kind 0: streams the world
constexpr uint32_t kClassLadder = 0x3011B913;  // kind 0: a ladder

struct Record {
    uint32_t cls = 0, kind = 0;
    int16_t pos[3]{};    // +0x10: the volume's centre
    int16_t ang[3]{};    // +0x18: angles (the box's turn)
    int16_t shape[4]{};  // +0x28: 0 sphere (r), 1 cylinder (r, top, bottom), 2 box (x, y, z sizes)
    bool enter = false, leave = false, c_enter = false, c_leave = false, use = false;  // lists present
    int16_t chunks[4]{};  // +0x30: a streaming record's four chunk numbers

    // record_contains (0x80083D30): whole-unit position inside the volume.
    bool contains(const int32_t p[3]) const;
};

struct Records {
    std::vector<Record> records;
    std::vector<std::vector<uint16_t>> zones;  // per floor zone id, the record indices (sorted)

    // From a BSD file's bytes: the entity table (+0x53C) and the zone lists (+0x598, zones_parse).
    bool load(const std::vector<uint8_t>& bsd, std::string* err);
    const std::vector<uint16_t>& zone(uint16_t z) const;
};

// zones_mask + zones_events (0x80084400, 0x800840E0) for one walker.
struct ZoneTracker {
    uint16_t zone = 0xFFFF, prev_zone = 0xFFFF;
    uint32_t bits = 0, prev_bits = 0;

    // After the walker's move: its floor zone and whole-unit position. Calls enter/leave with record
    // indices, in the original's order and with its skips on a zone change.
    void update(const Records& recs, uint16_t zone, const int32_t pos[3], bool is_player,
                const std::function<void(int, bool)>& event);  // (record, true: enter)
};

// The four chunk slots and chunk_stream (0x8005439C): entering a streaming record loads its first
// chunk not loaded into the first slot holding a chunk the record doesn't list. One chunk loads at
// a time, asynchronously: the slot takes the new chunk number at once and is skipped by the
// collision (and the draws) until the read is done; the frame idle job re-runs chunk_stream for
// the record's next missing chunk. In the original a read took 240..390 vblanks (4..6.5 s) on the
// recorded session. With `instant`, a load completes when it starts.
struct Streaming {
    int slot_chunk[4] = {-1, -1, -1, -1};  // 0x80098970: chunk numbers (1-based file numbers), -1 empty
    int loading = -1;                      // 0x800AA184: the slot being read, -1 none
    const Record* record = nullptr;        // 0x80099070: the last streaming record entered
    bool instant = true;
    int loads = 0;

    void start(int first_chunk, int slots);  // tsp_load_initial: chunks first.. into the slots
    void trigger(const Record* r) { record = r, run(); }  // chunk_stream_trigger
    bool run();                                           // chunk_stream; true if a load started
    void finish() { loading = -1; }                       // the read is done
    // The slots the collision tests see: the loading one is skipped.
    void collision_slots(int out[4]) const;
};

}  // namespace valor
