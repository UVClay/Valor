// coretest --pose M:L DUMP...: the core's model data and pose code against the original's RAM, as
// tools/sim/combat_sim.py pose does. For every soldier and dog in the five character slots of each
// RAM dump: the pose the core builds from the level file for the instance's cached animation and
// frame against the pose in RAM, the joints from RAM's pose against RAM's joints (drawn models),
// and the hit parts' world centres from RAM's joints against RAM's parts.
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "valor/gte.h"
#include "valor/io.h"
#include "valor/model.h"

using namespace valor;

namespace {

struct Ram {
    std::vector<uint8_t> m;
    uint32_t u32(uint32_t a) const {
        a &= 0x1FFFFF;
        return uint32_t(m[a]) | (uint32_t(m[a + 1]) << 8) | (uint32_t(m[a + 2]) << 16) | (uint32_t(m[a + 3]) << 24);
    }
    int32_t s32(uint32_t a) const { return int32_t(u32(a)); }
    int16_t s16(uint32_t a) const { return int16_t(u32(a) & 0xFFFF); }
    uint16_t u16(uint32_t a) const { return uint16_t(u32(a) & 0xFFFF); }
    uint8_t u8(uint32_t a) const { return m[a & 0x1FFFFF]; }
    bool ok(uint32_t a) const { return (a & 0xFFE00000) == 0x80000000 && (a & 0x1FFFFF); }
};

struct Count {
    long n = 0, ok = 0;
};

}  // namespace

int run_pose(int argc, char** argv) {
    std::string data = "extracted/moh/DATA";
    std::vector<std::string> dumps;
    std::string level;
    bool verbose = false;
    for (int i = 2; i < argc; i++) {
        if (!std::strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!std::strcmp(argv[i], "-v")) verbose = true;
        else if (level.empty()) level = argv[i];
        else dumps.push_back(argv[i]);
    }
    int ml = 0, ll = 0;
    if (std::sscanf(level.c_str(), "%d:%d", &ml, &ll) != 2 || dumps.empty()) {
        std::fprintf(stderr, "usage: coretest --pose M:L DUMP...\n");
        return 2;
    }
    Models models;
    std::string err;
    if (!models.load(data, ml, ll, &err)) {
        std::fprintf(stderr, "coretest --pose: %s\n", err.c_str());
        return 2;
    }
    std::map<std::string, Count> res;
    for (const std::string& path : dumps) {
        Ram r;
        if (!read_file(path, r.m) || r.m.size() < 0x200000) continue;
        int32_t eye = r.s32(r.u32(r.u32(0x8009D638) + 0x388) + 0x94);
        for (int slot = 0; slot < 5; slot++) {
            uint32_t e = 0x800B7B08 + slot * 0x2814;
            int32_t type = r.s32(e + 8);
            uint32_t inst = r.u32(e + 0x100);
            if ((type != 0x1402 && type != 0x1405) || !r.ok(inst)) continue;
            const Model* md = models.by_hash(r.u32(r.u32(inst + 8)));
            if (!md || md->bones.empty()) {
                res["model found in the level file"].n++;
                continue;
            }
            res["model found in the level file"].n++, res["model found in the level file"].ok++;
            int nb = r.u16(inst + 0x72);
            int anim = r.s16(inst + 0x62), frame = r.s16(inst + 0x64), blend = r.s16(inst + 0x3E);
            uint32_t qp = r.u32(inst + 0x88);
            if (!qp || anim < 0 || size_t(anim) >= md->anims.size() || md->anims[anim].frames.empty()) continue;
            std::vector<Quat> quats;
            for (int i = 0; i < nb; i++)
                quats.push_back({r.s16(qp + 8 * i), r.s16(qp + 8 * i + 2), r.s16(qp + 8 * i + 4), r.s16(qp + 8 * i + 6)});
            if (blend == 0 && qp == r.u32(inst + 0x90)) {
                std::vector<Quat> mine = models.frame_pose(*md, anim, frame);
                bool same = !mine.empty();
                for (size_t i = 0; same && i < mine.size() && i < quats.size(); i++) same = mine[i] == quats[i];
                res["pose from the animation"].n++, res["pose from the animation"].ok += same;
                if (verbose && !same) std::printf("%s %08X anim %d frame %d: pose differs\n", path.c_str(), e, anim, frame);
            }
            int nq = md->anims[anim].frames[size_t(frame) < md->anims[anim].frames.size() ? frame : 0].nquats;
            int16_t root[3] = {r.s16(inst + 0x98), r.s16(inst + 0x9A), r.s16(inst + 0x9C)};
            auto joints = skeleton_joints(*md, quats, root, nq);
            uint32_t jp = r.u32(inst + 0x94);
            std::vector<std::array<int16_t, 3>> ram_joints;
            for (int i = 0; i < nb; i++) ram_joints.push_back({r.s16(jp + 8 * i), r.s16(jp + 8 * i + 2), r.s16(jp + 8 * i + 4)});
            // Joints are written when the model is drawn (+0x298: drawn once), from the pose of
            // that draw: a dump catches some after the ticks moved the instance on, as in
            // combat_sim.py pose, which this check reproduces count for count.
            if (r.u32(e + 0x298)) {
                bool same = true;
                for (int i = 0; i < nb; i++) same &= joints[i] == ram_joints[i];
                res["joints from the pose"].n++, res["joints from the pose"].ok += same;
                if (verbose && !same) std::printf("%s %08X: joints differ\n", path.c_str(), e);
            }
            // The parts, from RAM's joints (what the game used), the matrix +0x340, position +0x354.
            int16_t m[3][3];
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) m[i][j] = r.s16(e + 0x340 + 6 * i + 2 * j);
            int32_t t[3] = {r.s32(e + 0x354), r.s32(e + 0x358), r.s32(e + 0x35C)};
            uint32_t pp = r.u32(e + 0x304);
            bool parts_same = true;
            for (size_t i = 0; i < md->parts.size(); i++) {
                int16_t c[3];
                part_centre(md->parts[i], ram_joints, &quats, eye, c);
                int32_t v[3] = {c[0], c[1], c[2]}, w[3];
                mvmva12(m, v, w);
                for (int k = 0; k < 3; k++) parts_same &= w[k] + t[k] == r.s32(pp + 0x48 * uint32_t(i) + 0x20 + 4 * k);
            }
            res["parts from the joints"].n++, res["parts from the joints"].ok += parts_same;
            // The matrix from the angles.
            int16_t mm[3][3];
            rot_yxz(r.s32(e + 0x228) >> 16, r.s32(e + 0x22C) >> 16, r.s32(e + 0x230) >> 16, mm);
            res["matrix from the angles"].n++, res["matrix from the angles"].ok += !std::memcmp(mm, m, sizeof m);
        }
    }
    bool all = true;
    for (auto& [k, c] : res) {
        std::printf("%-30s %ld/%ld exact\n", k.c_str(), c.ok, c.n);
        all &= c.ok == c.n;
    }
    return all ? 0 : 1;
}
