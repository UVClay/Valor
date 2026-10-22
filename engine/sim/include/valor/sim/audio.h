// The sound system's state the game logic reads (docs/re/audio.md, Voices; tools/sim/engine_audio.py):
// the voice table soldiers' hearing tests and sensor 0x68 read. Which voice a sound gets, its
// position, age and whether the SPU still plays it are kept; volumes and panning are left out.
// The SPU's key status is modelled from the sample's length.
#pragma once

#include <map>

#include "valor/sim/common.h"

namespace valor::sim {

class Engine;

class Audio {
public:
    explicit Audio(Engine& eng) : eng_(eng) {}
    void adopt(I vb);
    I play(I bank, I sound, U ent, I volume, I vspread, I pitch, I pspread, I fade, I loop, I cls, I flags);
    void stop(int v);
    void update();
    void release(int v);
    void fade_entity(U e, I frames);
    I restart(I bank, I sound, U ent, I volume, I vspread, I pitch, I pspread, I loop, int v, I cls);  // 0x80042250

private:
    struct Voice {
        enum { ON, PENDING, REL, OFF } st;
        double start = 0, end = 0;  // ON: vblanks; PENDING: end holds the duration
    };
    int alloc();
    void start(I bank, I sound, int v, I pitch, I pspread);
    Engine& eng_;
    I keyon_ = 0;
    std::map<int, Voice> spu_;
};

}  // namespace valor::sim
