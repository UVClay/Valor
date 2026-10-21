// PsyQ libgte's integer math as the game uses it: ratan2, the square roots, rsin, the rotation
// matrices of angles and of quaternions (docs/re/animation.md; tools/sim/combat_sim.py). The arctan,
// square-root and sine tables are the game's: read from the executable on the extracted disc.
#pragma once

#include <cstdint>
#include <string>

namespace valor {

struct Gte {
    // From DATA's parent: SLUS_009.74 (the tables at 0x8002D4F8, 0x8003440C, 0x800347EC).
    bool load(const std::string& data_dir, std::string* err);
    bool loaded = false;

    int32_t ratan2(int32_t y, int32_t x) const;  // 0x8001536C: 4096 per turn
    int32_t sqrt0(int32_t a) const;              // SquareRoot0 0x80016188
    int32_t sqrt12(int32_t a) const;             // SquareRoot12 0x8001BC68 (4.12)
    int32_t rsin(int32_t a) const;               // rsin: 4096 per turn, 4096 = 1

    int16_t ratan_[1025]{}, sqrt_[192]{}, sin1_[1025]{};
};

using Mat3 = int16_t[3][3];

// RotMatrixYXZ (exe 0x8001AF98) of halfword angles.
void rot_yxz(int32_t ax, int32_t ay, int32_t az, int16_t m[3][3]);
// A quaternion {x, y, z, w} (4096 = 1) as the game builds its matrix (0x80013E58): the transpose of
// the textbook one, each element rounded on its own.
void quat_matrix(const int16_t q[4], int16_t m[3][3]);
// GTE mvmva, sf = 1, no translation: (M v) >> 12 per row.
void mvmva12(const int16_t m[3][3], const int32_t v[3], int32_t out[3]);

}  // namespace valor
