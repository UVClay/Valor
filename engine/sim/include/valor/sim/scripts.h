// The model-script VM (docs/re/enemies.md, Scripts; animation.md, Op 00): tools/sim/sst_vm.py's
// interpreter as tools/sim/engine_scripts.py runs it in the engine, the world it reads computed
// (engine_sense.py, engine_world_ops.py) and the effects it asks for carried out at once
// (engine_effects.py).
#pragma once

#include "valor/sim/common.h"
#include "valor/sim/simgte.h"

namespace valor::sim {

class Engine;

class VM {
public:
    VM(Engine& eng, U e);
    Engine& eng;
    Mem& m;
    U e, inst, model;
    I type;
    I ch = 0;
    U ctx;
    int yielded = 0;
    U query_target = 0;

    I get_var(I vid);
    void set_var(I vid, I value);
    I script_start(I idx, I force, I ch);
    void run_b();
    void run_a();

private:
    I sensor(I vid);
    I operand(int n, I mask, I value) { return (mask & (1 << (n - 1))) ? get_var(value) : value; }
    U script_entry(I idx);
    I script_of(U code);
    U code_base(U ctx);
    I code_len(U ctx);
    I rand();
    I world(I op, I var, const I* computed = nullptr);
    U step(U pc, I ch, U ctx);
    U base_step(U pc, I ch, U ctx);
    U op_query(U pc, I mask, I w);
    U sop_event(U pc, I op);
    U find_type_group_vm(I typ, I grp);
    U nearest_vm(I typ);
    bool world_op(I op, U pc, I w, I mask, I imm, U& next);
    void effect(const char* name, const I* args, int n);
};

// The scripts' world reads (engine_sense.py).
struct NodeTarget {
    V3 node_point{};
    I side[2]{};
    I via = 0, claimed = 0;
};
I dist_approx(I dx, I dy, I dz);
U sense_nearest(Mem& m, U e, I typ);
U find_type_group(Mem& m, I typ, I grp);
I sight_test(Engine& eng, U e, U t, I rng, bool blockers);
bool sees(Engine& eng, U w, U v, bool long_range);
I hearing(Engine& eng, U e);
void route_turn_kind(Engine& eng, U e, I px, I pz, I& kind, I& out);
I brain_node_target(Mem& m, U e, NodeTarget& w, I node);
I probe(Engine& eng, U e, I how, I r);
I compute_node(Engine& eng, U e, I rng, I exclude, I mask);
I node_eval(Engine& eng, U e, U n, I fact);  // op 34 (0x8005B32C)
V3 normal_ss(const Gte& g, const V3& v);     // VectorNormalSS
I world_value(VM& vm, I op, I var);
I sense_sensor(VM& vm, I vid);
void hit_parts_set(Mem& m, U e, I mask, I kind, I radius, I damage);

// brain_sim.py
I path_nearest_point(Mem& m, U e, I path);
bool frame_data(Mem& m, U inst, I anim, I frame, I mv[3], I off[3], I turn[3]);

}  // namespace valor::sim
