// The collision rules of docs/re/levels.md (Collision), ported from tools/sim/move_sim.py, which
// matches the original to the bit. Comments name the original's functions.
#include "valor/world.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "valor/fixed.h"
#include "valor/gte.h"
#include "valor/io.h"

namespace valor {

namespace {

uint16_t rd16(const std::vector<uint8_t>& d, size_t o) { return uint16_t(d[o] | (d[o + 1] << 8)); }
uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8) | (uint32_t(d[o + 2]) << 16) | (uint32_t(d[o + 3]) << 24);
}

// 0x8005FC00: the face is up-facing (normal y <= 0) and holds (x, z).
bool tri_holds(const CollChunk& ch, const CollFace& f, int32_t x, int32_t z) {
    const V3s &v0 = ch.verts[f.v[0]], &v1 = ch.verts[f.v[1]], &v2 = ch.verts[f.v[2]];
    if (ch.norms[f.n].y > 0) return false;
    // x: below v0's x or beyond the larger of v1's and v2's (v0 isn't checked for the max, nor
    // v1/v2 for the min); z: the true min and max.
    if (x < v0.x || std::max(v1.x, v2.x) < x) return false;
    if (z < std::min({v0.z, v1.z, v2.z}) || std::max({v0.z, v1.z, v2.z}) < z) return false;
    return nclip(v0.x, v0.z, v1.x, v1.z, x, z) <= 0 && nclip(v1.x, v1.z, v2.x, v2.z, x, z) <= 0 &&
           nclip(v2.x, v2.z, v0.x, v0.z, x, z) <= 0;
}

// 0x8005FDCC: the plane's y at (x, z); nearly vertical planes give v0's y.
int32_t plane_height(const CollChunk& ch, const CollFace& f, int32_t x, int32_t z) {
    const V3s& n = ch.norms[f.n];
    if (iabs(n.y) < 0x101) return ch.verts[f.v[0]].y;
    int64_t num = s32(-(int64_t(n.x) * x + int64_t(n.z) * z + (int64_t(f.d) << 15)));
    int64_t q = iabs(num) / iabs(n.y);
    if ((num < 0) != (n.y < 0)) q = -q;
    return s16(q);
}

// 0x8005FEA0 over a leaf's faces.
int32_t floor_pick(const CollChunk& ch, size_t start, size_t count, int32_t px, int32_t py, int32_t pz) {
    int32_t best = kNoFloor, best_dist = 0x7FFF;
    bool best_below = false, best_above = false;
    size_t end = std::min(start + count, ch.idx.size());
    for (size_t i = start; i < end; i++) {
        const CollFace& f = ch.faces[ch.idx[i]];
        if (!tri_holds(ch, f, px, pz)) continue;
        int32_t h = plane_height(ch, f, px, pz);
        int32_t dist = int32_t(iabs(h - py));
        bool below = py < h;
        bool above = !below && !(h + 32 < py);
        if (!(below || above)) continue;
        bool take;
        if (best_below)
            take = below ? h < best : true;
        else if (best_above)
            take = dist < best_dist;
        else
            take = true;
        if (take) {
            best_below = below;
            best_above = above;
            best_dist = dist;
            best = h;
        }
    }
    return best;
}

// 0x80060034: the k-d descent to the leaf holding (x, z).
Floor floor_leaf(const CollChunk& ch, int32_t px, int32_t py, int32_t pz) {
    int32_t xmin = ch.rect[0], zmin = ch.rect[1];
    const CollNode* n = &ch.nodes[0];
    while (n->a >= 0) {
        if (n->b >= 0) {
            int32_t s = s16(xmin + n->split);
            if (px >= s) {
                xmin = s;
                n = &ch.nodes[n->b];
            } else {
                n = &ch.nodes[n->a];
            }
        } else {
            int32_t s = s16(zmin + n->split);
            if (pz < s) {
                n = &ch.nodes[n->a];
            } else {
                zmin = s;
                n = &ch.nodes[~n->b];
            }
        }
    }
    return {floor_pick(ch, size_t(uint16_t(n->b)), size_t(~n->a), px, py, pz), n->zone};
}

// 0x800776C0: the triangle is near the walker's segment (centre c, radius r, height h). The
// player's gather always passes arg3 = 1.
bool wall_test(const int32_t c[3], int32_t r, int32_t h, const V3s& A, const V3s& B, const V3s& C, const V3s& n);

bool tri_near(const CollChunk& ch, const CollFace& f, const int32_t c[3], int32_t r, int32_t h) {
    const V3s &A = ch.verts[f.v[0]], &B = ch.verts[f.v[1]], &C = ch.verts[f.v[2]];
    const V3s& n = ch.norms[f.n];
    int32_t cx = c[0], cy = c[1], cz = c[2];
    if (cx + r < A.x || std::max(B.x, C.x) < cx - r) return false;
    if (cz + r < std::min({A.z, B.z, C.z}) || std::max({A.z, B.z, C.z}) < cz - r) return false;
    if (std::max({A.y, B.y, C.y}) < cy || cy + h < std::min({A.y, B.y, C.y})) return false;
    int64_t rr = int64_t(r) * r;
    bool wall = iabs(n.y) < std::max(iabs(n.x), iabs(n.z));
    for (const V3s* V : {&A, &B, &C}) {
        if (cy < V->y && V->y < cy + h) {
            int64_t dx = s16(V->x - cx), dz = s16(V->z - cz);
            if (int64_t(uint32_t(dx * dx + dz * dz)) < rr) return wall ? true : n.y >= 0;
        }
    }
    if (wall) return wall_test(c, r, h, A, B, C, n);
    if (n.y < 0) return false;
    // Down-facing (ceiling-like): the square of side 2r around (x, z) against the edges.
    const int32_t px_[3] = {A.x, B.x, C.x}, pz_[3] = {A.z, B.z, C.z};
    for (int e = 0; e < 3; e++) {
        int i0 = e, i1 = (e + 1) % 3;
        int32_t px = pz_[i1] < pz_[i0] ? cx + r : cx - r;
        int32_t pz = px_[i1] < px_[i0] ? cz - r : cz + r;
        if (nclip(px_[i0], pz_[i0], px_[i1], pz_[i1], s16(px), s16(pz)) < 0) return false;
    }
    return true;
}

// 0x8007711C: a wall-like triangle against the walker's box seen along the wall's main
// horizontal axis (x-y when |nx| < |nz|, z-y otherwise).
bool wall_test(const int32_t c[3], int32_t r, int32_t h, const V3s& A, const V3s& B, const V3s& C, const V3s& n) {
    int32_t h2 = h >> 1;
    int32_t mid = c[1] + h2;
    int u;
    int32_t cu;
    bool pos_side;
    if (iabs(n.x) < iabs(n.z)) {
        u = 0, cu = c[0], pos_side = n.z < 0;
    } else {
        u = 2, cu = c[2], pos_side = n.x > 0;
    }
    const int32_t pu_[3] = {A[u], B[u], C[u]}, pv_[3] = {A.y, B.y, C.y};
    for (int e = 0; e < 3; e++) {
        int i0 = e, i1 = (e + 1) % 3;
        if (pos_side) {
            int32_t pu = pv_[i1] < pv_[i0] ? cu + r : cu - r;
            int32_t pv = pu_[i1] < pu_[i0] ? mid - h2 : mid + h2;
            if (nclip(pu_[i0], pv_[i0], pu_[i1], pv_[i1], s16(pu), s16(pv)) < 0) return false;
        } else {
            int32_t pu = pv_[i1] < pv_[i0] ? cu - r : cu + r;
            int32_t pv = pu_[i1] < pu_[i0] ? mid + h2 : mid - h2;
            if (nclip(pu_[i0], pv_[i0], pu_[i1], pv_[i1], s16(pu), s16(pv)) > 0) return false;
        }
    }
    return true;
}

// GTE MVMVA with sf = 0 of a point and a normal: the 32-bit MAC1.
int32_t plane_dot(const V3s& n, int32_t x, int32_t y, int32_t z) {
    return s32(int64_t(x) * n.x + int64_t(y) * n.y + int64_t(z) * n.z);
}

// 0x80078350: the segment c .. c + (0, h, 0) crosses the plane, or its centre is in front and the
// point r sideways (against the normal's signs) is not.
bool plane_crosses(const int32_t c[3], int32_t r, int32_t h, const V3s& n, int32_t d) {
    int64_t t0 = (int64_t(plane_dot(n, c[0], c[1], c[2])) >> 15) + d;
    int64_t v0 = t0 + ((int64_t(h) * n.y) >> 15);
    if ((t0 ^ v0) < 0) return true;
    if (t0 < 0) return false;
    int32_t x = n.x > 0 ? c[0] - r : c[0] + r;
    int32_t z = n.z > 0 ? c[2] - r : c[2] + r;
    int64_t v1 = (int64_t(plane_dot(n, s16(x), c[1], s16(z))) >> 15) + d;
    v0 = v1 + ((int64_t(h) * n.y) >> 15);
    if ((v1 ^ v0) < 0) return true;
    return (v1 ^ t0) < 0;
}

int gather_leaf(const CollChunk& ch, size_t start, size_t count, const int32_t c[3], int32_t r, int32_t h,
                const int32_t delta[3], std::vector<WallPlane>& out, size_t base, int maxn, bool use_delta) {
    int added = 0;
    size_t end = std::min(start + count, ch.idx.size());
    for (size_t i = start; i < end; i++) {
        uint16_t fi = ch.idx[i];
        const CollFace& f = ch.faces[fi];
        const V3s& n = ch.norms[f.n];
        if (!tri_near(ch, f, c, r, h)) continue;
        if (use_delta && !(plane_dot(n, delta[0], delta[1], delta[2]) < 0)) continue;  // only walls the move goes into
        if (!plane_crosses(c, r, h, n, f.d)) continue;
        bool dup = false;
        for (size_t k = base; k < out.size(); k++)
            if (out[k].face == fi) dup = true;
        if (dup) continue;
        out.push_back({{n.x, n.y, n.z}, f.d, fi, &ch});
        added++;
        if (int(out.size() - base) >= maxn) return added;
    }
    return added;
}

// 0x800787C0: every leaf whose cell comes within r of (x, z), a stack of at most 8 halves.
int gather_chunk(const CollChunk& ch, const int32_t c[3], int32_t r, int32_t h, const int32_t delta[3],
                 std::vector<WallPlane>& out, int maxn, bool use_delta) {
    size_t base = out.size();
    int32_t x = c[0], z = c[2];
    struct Half {
        int node;
        int32_t xmin, xmax, zmin, zmax;
    };
    Half stack[8];
    int sp = 0;
    int32_t xmin = ch.rect[0], xmax = ch.rect[2], zmin = ch.rect[1], zmax = ch.rect[3];
    int node = 0, total = 0;
    while (true) {
        const CollNode& nd = ch.nodes[node];
        if (nd.a >= 0) {
            if (nd.b >= 0) {
                int32_t s = s16(xmin + nd.split);
                if (x - r < s) {
                    if (s < x + r && sp < 8) stack[sp++] = {nd.b, s, xmax, zmin, zmax};
                    xmax = s - 1;
                    node = nd.a;
                } else {
                    xmin = s;
                    node = nd.b;
                }
            } else {
                int32_t s = s16(zmin + nd.split);
                if (z - r < s) {
                    if (s < z + r && sp < 8) stack[sp++] = {~nd.b, xmin, xmax, s, zmax};
                    zmax = s - 1;
                    node = nd.a;
                } else {
                    zmin = s;
                    node = ~nd.b;
                }
            }
            continue;
        }
        int k = gather_leaf(ch, size_t(uint16_t(nd.b)), size_t(~nd.a), c, r, h, delta, out, base, maxn, use_delta);
        if (k > 0) {
            total += k;
            if (total >= maxn) return total;
        }
        if (sp == 0) return total;
        const Half& hf = stack[--sp];
        node = hf.node, xmin = hf.xmin, xmax = hf.xmax, zmin = hf.zmin, zmax = hf.zmax;
    }
}

// 0x80078AA4: the rectangle holds (x, z), or its nearest edge point is within r. For a
// coordinate inside the rectangle's range, the nearer edge is used, not the coordinate.
bool chunk_near(const CollChunk& ch, int32_t x, int32_t z, int32_t r) {
    int32_t x0 = ch.rect[0], z0 = ch.rect[1], x1 = ch.rect[2], z1 = ch.rect[3];
    if (x0 <= x && x <= x1 && z0 <= z && z <= z1) return true;
    int32_t ex, ez;
    if (x0 < x)
        ex = ((x < x1 && (x1 - x) < (x - x0)) || x >= x1) ? x1 : x0;
    else
        ex = x0;
    if (z0 < z)
        ez = ((z < z1 && (z1 - z) < (z - z0)) || z >= z1) ? z1 : z0;
    else
        ez = z0;
    int64_t dz = z - ez, dx = x - ex;
    return dz * dz + dx * dx < int64_t(r) * r;
}

bool in_rect(const int16_t r[4], int32_t x, int32_t z) { return r[0] <= x && x <= r[2] && r[1] <= z && z <= r[3]; }

// 0x8003ECE4: the segment a -> b (x, z) crossing the rectangle, as its code tests it.
bool crosses(const int16_t r[4], const int32_t a[3], const int32_t b[3]) {
    int64_t t1 = s16(a[0]), t0 = s16(a[2]), t5 = s16(b[0]), t6 = s16(b[2]);
    int64_t t2 = t5 - t1, a1 = t6 - t0;
    int64_t x0 = r[0], z0 = r[1], x1 = r[2], z1 = r[3];
    int64_t lo = (x1 - t1) * a1;
    int64_t t4 = (z1 - t0) * t2 - lo, t7 = (z0 - t0) * t2 - lo;
    if ((t4 ^ t7) <= 0 && x1 < t1 && t5 < x1) return true;
    int64_t t3 = (z1 - t0) * t2 - (x0 - t1) * a1;
    if (!((t4 ^ t3) > 0 || z1 >= t0 || t6 >= z1)) return true;
    int64_t v1 = (z0 - t0) * t2 - (x0 - t1) * a1;
    if ((v1 ^ t3) <= 0 && !(x0 >= t1 || t5 >= x0)) return true;
    if ((v1 ^ t7) <= 0 && !(z0 >= t0 || !(t6 < z0))) return true;
    return false;
}

int64_t frac16(int64_t num, int64_t den) {
    if (den == 0) return 0;
    int64_t q = iabs(num << 16) / iabs(den);
    return ((num << 16) >= 0) == (den >= 0) ? q : -q;
}

struct SweepHit {
    int64_t t = 0x7FFF;
    int32_t p[3]{};
    int16_t n[3]{};
};

// leaf_sweep (0x8003DC10): faces crossed from their front (d0 >= 0) to their back, the crossing at
// t = |d0| * 4096 / (|d0| + |d1|) inside the triangle (three edge cross products against the normal,
// GTE OP sf=1); the smallest t of the chunk so far.
bool leaf_sweep(const CollChunk& ch, size_t start, size_t count, const int32_t a[3], const int32_t b[3],
                int32_t miny, int32_t maxy, SweepHit& best) {
    bool hit = false;
    int64_t delta[3] = {s16(b[0] - a[0]), s16(b[1] - a[1]), s16(b[2] - a[2])};
    size_t end = std::min(start + count, ch.idx.size());
    for (size_t i = start; i < end; i++) {
        const CollFace& f = ch.faces[ch.idx[i]];
        const V3s* V[3] = {&ch.verts[f.v[0]], &ch.verts[f.v[1]], &ch.verts[f.v[2]]};
        int32_t lo = std::min({V[0]->y, V[1]->y, V[2]->y}), hi = std::max({V[0]->y, V[1]->y, V[2]->y});
        if (hi < miny || maxy < lo) continue;
        const V3s& n = ch.norms[f.n];
        int64_t d0 = (int64_t(s32(int64_t(n.x) * a[0] + int64_t(n.y) * a[1] + int64_t(n.z) * a[2])) >> 15) + f.d;
        int64_t d1 = (int64_t(s32(int64_t(n.x) * b[0] + int64_t(n.y) * b[1] + int64_t(n.z) * b[2])) >> 15) + f.d;
        if (d0 < 0 || (d0 ^ d1) >= 0) continue;
        int64_t t = (iabs(d0) << 12) / (iabs(d0) + iabs(d1));
        if (!(t < best.t)) continue;
        int32_t p[3];
        for (int k = 0; k < 3; k++) p[k] = s16(sat16((t * delta[k]) >> 12) + a[k]);
        bool ok = true;
        for (int e = 0; e < 3 && ok; e++) {
            const V3s &va = *V[e], &vb = *V[(e + 1) % 3];
            int32_t A[3] = {s16(p[0] - va.x), s16(p[1] - va.y), s16(p[2] - va.z)};
            int32_t B[3] = {s16(vb.x - va.x), s16(vb.y - va.y), s16(vb.z - va.z)};
            // OP with A as the diagonal and B as IR, saturated to IR's 16 bits.
            int64_t c0 = sat16(s32(wrap44(int64_t(B[2]) * A[1] - int64_t(B[1]) * A[2]) >> 12));
            int64_t c1 = sat16(s32(wrap44(int64_t(B[0]) * A[2] - int64_t(B[2]) * A[0]) >> 12));
            int64_t c2 = sat16(s32(wrap44(int64_t(B[1]) * A[0] - int64_t(B[0]) * A[1]) >> 12));
            if (s32(n.x * c0 + n.y * c1 + n.z * c2) < 0) ok = false;
        }
        if (!ok) continue;
        best.t = t;
        std::copy(p, p + 3, best.p);
        best.n[0] = n.x, best.n[1] = n.y, best.n[2] = n.z;
        hit = true;
    }
    return hit;
}

// chunk_sweep (0x8003E3F0): the k-d tree walked along the segment (x, z), the near half first, up
// to 8 far halves deferred (LIFO); the first leaf with a hit ends the walk.
bool chunk_sweep(const CollChunk& ch, const int32_t start[3], const int32_t end[3], SweepHit& out) {
    int32_t sx = s16(start[0]), ex = s16(end[0]), sz = s16(start[2]), ez = s16(end[2]);
    int32_t x0 = ch.rect[0], z0 = ch.rect[1], x1 = ch.rect[2], z1 = ch.rect[3];
    struct Half {
        int node;
        int32_t x0, x1, z0, z1, sx, ex, sz, ez;
    };
    Half stack[8];
    int sp = 0, node = 0;
    SweepHit best;
    int32_t miny = std::min(start[1], end[1]), maxy = std::max(start[1], end[1]);
    while (true) {
        const CollNode& nd = ch.nodes[node];
        if (nd.a >= 0) {
            if (nd.b >= 0) {
                int32_t s = s16(x0 + nd.split);
                if (sx < s) {
                    if (s < ex && sp < 8) {
                        int64_t f = frac16(s - sx, ex - sx);
                        int32_t nz = s16(sz + ((int64_t(ez - sz) * f) >> 16));
                        stack[sp++] = {nd.b, s, x1, z0, z1, s, ex, nz, ez};
                        x1 = s - 1, ex = s, ez = nz;
                    } else {
                        x1 = s - 1;
                    }
                    node = nd.a;
                } else {
                    if (ex < s && sp < 8) {
                        int64_t f = frac16(s - sx, ex - sx);
                        int32_t nz = s16(sz + ((int64_t(ez - sz) * f) >> 16));
                        stack[sp++] = {nd.a, x0, s - 1, z0, z1, s, ex, nz, ez};
                        x0 = s, ex = s, ez = nz;
                    } else {
                        x0 = s;
                    }
                    node = nd.b;
                }
            } else {
                int32_t s = s16(z0 + nd.split);
                if (sz < s) {
                    if (s < ez && sp < 8) {
                        int64_t f = frac16(s - sz, ez - sz);
                        int32_t nx = s16(sx + ((int64_t(ex - sx) * f) >> 16));
                        stack[sp++] = {~nd.b, x0, x1, s, z1, nx, ex, s, ez};
                        z1 = s - 1, ez = s, ex = nx;
                    } else {
                        z1 = s - 1;
                    }
                    node = nd.a;
                } else {
                    if (ez < s && sp < 8) {
                        int64_t f = frac16(s - sz, ez - sz);
                        int32_t nx = s16(sx + ((int64_t(ex - sx) * f) >> 16));
                        stack[sp++] = {nd.a, x0, x1, z0, s - 1, nx, ex, s, ez};
                        z0 = s, ez = s, ex = nx;
                    } else {
                        z0 = s;
                    }
                    node = ~nd.b;
                }
            }
            continue;
        }
        if (leaf_sweep(ch, size_t(uint16_t(nd.b)), size_t(~nd.a), start, end, miny, maxy, best)) {
            out = best;
            return true;
        }
        if (sp == 0) return false;
        const Half& h = stack[--sp];
        node = h.node, x0 = h.x0, x1 = h.x1, z0 = h.z0, z1 = h.z1, sx = h.sx, ex = h.ex, sz = h.sz, ez = h.ez;
    }
}

}  // namespace

bool World::sweep(const int32_t from[3], const int32_t to[3], int32_t hit[3], int16_t normal[3]) const {
    int64_t best = 0x7FFF;
    bool any = false;
    for (const CollChunk* ch : slots) {
        if (!ch) continue;
        if (!(in_rect(ch->rect, from[0], from[2]) || in_rect(ch->rect, to[0], to[2]) || crosses(ch->rect, from, to)))
            continue;
        SweepHit h;
        if (chunk_sweep(*ch, to, from, h) && h.t < best) {
            best = h.t, any = true;
            std::copy(h.p, h.p + 3, hit);
            std::copy(h.n, h.n + 3, normal);
        }
    }
    return any;
}

bool CollChunk::parse(const std::vector<uint8_t>& d) {
    if (d.size() < 0x40) return false;
    size_t off = rd32(d, 0x3C);
    if (off + 0x12 > d.size()) return false;
    for (int i = 0; i < 4; i++) rect[i] = int16_t(rd16(d, off + 2 * i));
    uint16_t n_nodes = rd16(d, off + 8), n_idx = rd16(d, off + 10), n_verts = rd16(d, off + 12),
             n_norms = rd16(d, off + 14), n_faces = rd16(d, off + 16);
    size_t p = off + 0x12;
    size_t need = p + 8 * n_nodes + 2 * n_idx + 4 + 8 * n_verts + 8 * n_norms + 10 * n_faces;
    if (need > d.size() + 4) return false;
    nodes.resize(n_nodes);
    for (int i = 0; i < n_nodes; i++, p += 8)
        nodes[i] = {int16_t(rd16(d, p)), int16_t(rd16(d, p + 2)), rd16(d, p + 4), rd16(d, p + 6)};
    idx.resize(n_idx);
    for (int i = 0; i < n_idx; i++, p += 2) idx[i] = rd16(d, p);
    p += (4 - p % 4) % 4;
    verts.resize(n_verts);
    for (int i = 0; i < n_verts; i++, p += 8)
        verts[i] = {int16_t(rd16(d, p)), int16_t(rd16(d, p + 2)), int16_t(rd16(d, p + 4))};
    norms.resize(n_norms);
    for (int i = 0; i < n_norms; i++, p += 8)
        norms[i] = {int16_t(rd16(d, p)), int16_t(rd16(d, p + 2)), int16_t(rd16(d, p + 4))};
    faces.resize(n_faces);
    for (int i = 0; i < n_faces; i++, p += 10)
        faces[i] = {{rd16(d, p), rd16(d, p + 2), rd16(d, p + 4)}, rd16(d, p + 6), int16_t(rd16(d, p + 8))};
    return p <= d.size();
}

bool World::load(const std::string& data_dir, int m, int l, std::string* err) {
    chunks.clear();
    slots.clear();
    for (int n = 1;; n++) {
        char rel[96];
        std::snprintf(rel, sizeof rel, "/MSN%d/LVL%d/TSP0/%d_%d_C%d.TSP", m, l, m, l, n);
        std::vector<uint8_t> bytes;
        if (!read_file(data_dir + rel, bytes)) break;
        CollChunk ch;
        ch.number = n;
        ch.name = std::string(rel).substr(std::string(rel).rfind('/') + 1);
        if (!ch.parse(bytes)) {
            if (err) *err = "bad TSP collision block: " + data_dir + rel;
            return false;
        }
        chunks.push_back(std::move(ch));
    }
    if (chunks.empty()) {
        if (err) *err = "no TSP chunks under " + data_dir;
        return false;
    }
    set_all_chunks();
    return true;
}

void World::set_slots_by_rect(const int16_t rects[4][4]) {
    slots.assign(4, nullptr);
    for (int i = 0; i < 4; i++)
        for (const CollChunk& ch : chunks)
            if (!std::memcmp(ch.rect, rects[i], sizeof ch.rect)) {
                slots[i] = &ch;
                break;
            }
}

void World::set_slots_by_number(const int numbers[4]) {
    slots.assign(4, nullptr);
    for (int i = 0; i < 4; i++)
        if (numbers[i] >= 1 && size_t(numbers[i]) <= chunks.size()) slots[i] = &chunks[numbers[i] - 1];
}

void World::set_all_chunks() {
    slots.clear();
    for (const CollChunk& ch : chunks) slots.push_back(&ch);
}

// 0x800603DC -> floor_search 0x8006028C.
Floor World::floor_find(const int32_t pos[3]) const {
    int32_t x = s16(pos[0] >> 16), y = s16(pos[1] >> 16), z = s16(pos[2] >> 16);
    int64_t best = kNoFloor;
    uint16_t zone = 0xFFFF;
    for (const CollChunk* ch : slots) {
        if (!ch) continue;
        const int16_t* r = ch->rect;
        if (x < r[0] || r[2] < x || z < r[1] || r[3] < z) continue;
        Floor f = floor_leaf(*ch, x, y, z);
        int64_t h = f.height;
        bool take = false;
        if (best == kNoFloor) {
            take = true;
        } else if (h != kNoFloor) {
            // The game compares the feet's 16.16 y with whole-unit heights here.
            int64_t a0 = pos[1];
            if (a0 < h)
                take = a0 < best ? h < best : (h - a0) < iabs(best - a0);
            else
                take = (h - a0) < iabs(best - a0);
        }
        if (take) best = h, zone = f.zone;
    }
    return {int32_t(best), zone};
}

void World::walls_gather(const int32_t c[3], int32_t r, int32_t h, const int32_t delta[3],
                         std::vector<WallPlane>& out, int maxn, bool use_delta) const {
    out.clear();
    int total = 0;
    for (const CollChunk* ch : slots) {
        if (!ch) continue;
        if (chunk_near(*ch, c[0], c[2], r)) total += gather_chunk(*ch, c, r, h, delta, out, maxn, use_delta);
        if (total >= maxn) break;
    }
}

bool wall_tri_near(const CollChunk& ch, const CollFace& f, const int32_t c[3], int32_t r, int32_t h) {
    return tri_near(ch, f, c, r, h);
}
int32_t wall_plane_dot(const V3s& n, int32_t x, int32_t y, int32_t z) { return plane_dot(n, x, y, z); }
bool wall_plane_crosses(const int32_t c[3], int32_t r, int32_t h, const V3s& n, int32_t d) {
    return plane_crosses(c, r, h, n, d);
}

}  // namespace valor
