// The collision rules of docs/re/levels.md (Collision), ported from tools/sim/move_sim.py, which
// matches the original to the bit. Comments name the original's functions.
#include "valor/world.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "valor/fixed.h"
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
                const int32_t delta[3], std::vector<WallPlane>& out, size_t base, int maxn) {
    int added = 0;
    size_t end = std::min(start + count, ch.idx.size());
    for (size_t i = start; i < end; i++) {
        uint16_t fi = ch.idx[i];
        const CollFace& f = ch.faces[fi];
        const V3s& n = ch.norms[f.n];
        if (!tri_near(ch, f, c, r, h)) continue;
        if (!(plane_dot(n, delta[0], delta[1], delta[2]) < 0)) continue;  // only walls the move goes into
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
                 std::vector<WallPlane>& out, int maxn) {
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
        int k = gather_leaf(ch, size_t(uint16_t(nd.b)), size_t(~nd.a), c, r, h, delta, out, base, maxn);
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

}  // namespace

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
                         std::vector<WallPlane>& out, int maxn) const {
    out.clear();
    int total = 0;
    for (const CollChunk* ch : slots) {
        if (!ch) continue;
        if (chunk_near(*ch, c[0], c[2], r)) total += gather_chunk(*ch, c, r, h, delta, out, maxn);
        if (total >= maxn) break;
    }
}

}  // namespace valor
