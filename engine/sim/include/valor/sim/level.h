// The level logic (docs/re/levels.md "Level logic: messages and scripts", "Spawning", "Triggers",
// "Objectives"; messages.md): tools/sim/level_vm.py's simulator as tools/sim/engine_level.py drives
// it, on the engine's own entities. The level-script VM (24 opcodes, the 22 built-ins),
// subscriptions and their node pool, sending with the five filters, the 128-entry queue, record
// state bits, activation and the spawn queue, the walkers' trigger volumes, objectives.
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "valor/sim/common.h"

namespace valor::sim {

class Engine;

constexpr U G_LEVEL = 0x8009CC64;
constexpr U G_SCRIPTS = 0x8009D5AC;
constexpr U G_SUBS = 0x8009D5B0;
constexpr U G_FREE = 0x8009D5B4;
constexpr U G_HEAD = 0x8009D5B8;
constexpr U G_TAIL = 0x8009D5BC;
constexpr U RING = 0x800B6460;
constexpr U POOL = 0x800AD460;
constexpr U OBJ_COUNT = 0x80098620;
constexpr U OBJ_PRIMARY = 0x80098624;
constexpr U OBJ_DONE = 0x800988DC;
constexpr U OBJ_TEXT = 0x80098904;
constexpr U OBJ_ID = 0x8009892C;
constexpr U MSG_TEXTS = 0x80098188;
constexpr U LEVEL_FLAGS = 0x8009C840;
constexpr U SPQ_HEAD = 0x8009E280, SPQ_TAIL = 0x8009E284, SPQ_COUNT = 0x8009E288, SPQ = 0x8009E294;
constexpr U G_ZONES = 0x8009EC5C;

struct Record {
    int idx = 0;
    U pair = 0, addr = 0, cls = 0;
    I kind = 0;
    I pos[3]{}, ang[3]{};
    I shape[4]{};
    // the lists (+0x38 enter, +0x3C leave, +0x40 c_enter, +0x44 c_leave, +0x48 tags, +0x4C use);
    // has_list false for a null pointer
    std::vector<U> lists[6];
    bool has_list[6]{};
    I script = -1, ent_script = -1;
    U flags30 = 0;
    I p34 = 0;
    U model = 0, pickup = 0;
    bool has_model = false;
};
enum { L_ENTER, L_LEAVE, L_C_ENTER, L_C_LEAVE, L_TAGS, L_USE };

struct Level {
    std::string name;
    U g_level = 0, scripts = 0, zones = 0;
    I nmsg = 0;
    std::vector<Record> records;
    std::unordered_map<U, Record*> by_pair;
    std::unordered_map<U, I> models;  // hash -> type (+0xFC)
    std::vector<U> nodes;
    const Mem* mem = nullptr;
    void load(const Mem& m, const std::string& name);
    std::vector<I> zone_records(I zone) const;
};

bool contains(const Record& rec, const I p[3]);

// An entity as the level logic sees it, backed by the engine's memory: flags A, the variables
// (+0x20..+0x27), the zone fields (+0x390..+0x39C) are the entity's own words.
struct Ent {
    U handle = 0;
    I type = 0;
    Record* rec = nullptr;
    U addr = 0;
    bool removed = false;
    I serial = 0;
};

struct Ctx {
    I pc = 0;
    Ent *sub = nullptr, *sender = nullptr, *actor = nullptr;
    I sub_obj = 0, sender_obj = 0, msg = 0, script = 0;
    std::string src;
};

// A level-script stack value: (type, value), the value an integer, an entity, nothing, bytes or a
// float.
struct SVal {
    enum Kind { INT, ENT, NONE, BYTES, FLOAT } kind = INT;
    I t = 0;
    I v = 0;
    Ent* e = nullptr;
    double f = 0;
    std::string bytes;
    static SVal num(I t, I v) {
        SVal s;
        s.t = t, s.v = v;
        return s;
    }
    bool truthy() const;
};

class LevelLogic {
public:
    explicit LevelLogic(Engine& eng);
    void adopt();
    Ent* bind(U e);
    Ent* spawn(Record& rec);
    void record_spawn(U pair);
    void removed(Ent* ent, const std::string& why);
    void send(I msg, Ent* sender, I sender_obj, Ent* actor, I target = 0, const std::string& src = "");
    I run_queue(I limit = -1);
    I run_one();        // msg_run_one
    I run_queue_all();  // msg_run_all (0x800663B8)
    bool spawn_retry_one();
    bool spawn_retry();
    void run_message(Ent* ent, I obj, I msg, const std::string& why = "");
    void msg_start(Ent* ent, I obj) { run_message(ent, obj, 0, "start"); }
    void zones_update(Ent* ent, I zone, const I pos[3]);
    void use(Ent* ent);
    void remove(Ent* ent, const std::string& why);

    Ent* player = nullptr;
    std::unordered_map<U, Ent*> handles;
    std::deque<Ctx> queue;
    I head = 0, tail = 0;
    std::deque<U> spawn_q;
    I spq_head = 0;
    U ladder_held = 0;
    std::set<U> removed_by_script;
    std::vector<I> alarm_requests;
    std::map<std::string, I> stats;
    std::map<I, I> calls;

private:
    Engine& eng_;
    Mem& m_;
    std::vector<std::unique_ptr<Ent>> owned_;
    I serial_ = 0;

    I script_of(Ent* ent, I obj);
    U script_base(I sid);
    bool handler(I sid, I k, I& msg, U& pc);
    std::vector<U> nodes(I msg);
    Ent* node_ent(U n, U off = 0);
    bool same(U n, U off, Ent* ent);
    void unregister(I msg, Ent* ent, I obj, I filt, Ent* fent, I fobj, I val);
    void reg(I msg, Ent* ent, I obj, I filt, Ent* fent, I fobj, I val, I pc, I sid);
    void drop_subscriptions(Ent* ent);
    I rec_of(Ent* ent, I obj);
    I has_tag(Ent* ent, I obj, I val);
    I tags_match(Ent* e1, I o1, Ent* e2, I o2, I typ);
    void enqueue(U n, Ent* sender, I sender_obj, Ent* actor, I msg, const std::string& src);
    bool still_subscribed(const Ctx& c);
    void activate(U pair);
    void merge(Ent* ent, const std::vector<I>& a, const std::vector<I>& b, I bits, I pb);
    void record_enter(Ent* ent, Record& r);
    void record_leave(Ent* ent, Record& r);
    void kind0_enter(Ent* ent, Record& r);
    void kind0_leave(Ent* ent, Record& r);
    void record_spawn_type(U pair);
    void objective_add(I oid, I text);
    void objective_remove(I oid);
    I objective_find(I oid);
    void vm(Ctx c);
    SVal binop(I op, const SVal& t, const SVal& n);
    bool prop(Ent* e, I o, I n, SVal& out);
    void builtin(I n, Ctx& c, std::vector<SVal>& st, U base, I sid);
    void builtin_base(I n, Ctx& c, std::vector<SVal>& st, U base, I sid);
    void alarm_level(I node_id);
};

}  // namespace valor::sim
