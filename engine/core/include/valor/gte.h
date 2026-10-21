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

    int32_t rcos(int32_t a) const;               // rcos: 4096 per turn, 4096 = 1

    // VectorNormal (0x80016A70) with the GTE: v scaled to length 4096, from the game's table.
    void vector_normal(const int32_t v[3], int32_t out[3]) const;

    // The view's projection for RTPS: the GTE's division (UNR table) and, for a recording made
    // with Valor's widescreen or FOV options, its scale of every projected offset about the
    // screen centre (runtime gte.cpp, gte_set_projection; 1.0 in the original). The game logic
    // reads projected points (aim_update), so a check against such a recording needs it.
    double proj_kx = 1.0, proj_ky = 1.0;
    void set_projection(double aspect, double hfov_deg);  // as the runtime's (world H 400, tan 120/400)
    // RTPS (sf = 1) of a point through rt, tr with projection distance h and offsets ofx, ofy
    // (16.16): the screen point.
    void rtps(const int16_t rt[3][3], const int32_t tr[3], uint32_t h, int32_t ofx, int32_t ofy,
              const int32_t v[3], int32_t& sx, int32_t& sy) const;

    int16_t ratan_[1025]{}, sqrt_[192]{}, sin1_[1025]{}, vn_[192]{};
};

// GTE helpers with the hardware's integer behaviour (tools/sim/engine_gte.py).
int64_t wrap44(int64_t v);
int32_t sat16(int64_t v);
// MVMVA: MAC = (t * 0x1000 + m . v) >> sf with 44-bit accumulators; ir saturated to s16.
void gte_mul(const int16_t m[3][3], const int32_t v[3], const int32_t t[3], int sf, int32_t mac[3], int32_t ir[3]);
// OP: the outer product of a and b, >> 12.
void outer_product12(const int32_t a[3], const int32_t b[3], int32_t out[3]);
// ApplyMatrixLV (0x800163E8): a 32-bit vector through the matrix, in two 15-bit halves.
void apply_matrix_lv(const int16_t m[3][3], const int32_t v[3], int32_t out[3]);
// RotMatrixY(r) of an identity.
void rot_matrix_y(int32_t r, int16_t m[3][3]);

using Mat3 = int16_t[3][3];

// RotMatrixYXZ (exe 0x8001AF98) of halfword angles.
void rot_yxz(int32_t ax, int32_t ay, int32_t az, int16_t m[3][3]);
// A quaternion {x, y, z, w} (4096 = 1) as the game builds its matrix (0x80013E58): the transpose of
// the textbook one, each element rounded on its own.
void quat_matrix(const int16_t q[4], int16_t m[3][3]);
// GTE mvmva, sf = 1, no translation: (M v) >> 12 per row.
void mvmva12(const int16_t m[3][3], const int32_t v[3], int32_t out[3]);

}  // namespace valor
