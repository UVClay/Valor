#!/usr/bin/env python3
"""Check TSP world chunks the way the game reads them (formats.md, TSP).

  fmt_tsp.py FILE.TSP...        one summary line per file, then totals
  fmt_tsp.py -v FILE.TSP        also the dynamic (animated face) blocks

Header words, the render tree (0x24-byte inner nodes, 0x1C-byte leaves), 16-byte faces,
8-byte vertices, per-vertex colours (red < 40: an animated light), dynamic blocks.
The collision block after them is tspcol.py's.
"""
import struct
import sys


def parse(d):
    (ident, ver, n_nodes, o_nodes, n_faces, o_faces, n_verts, o_verts, n_u1, o_u1, n_cols, o_cols,
     n_u2, o_u2, n_dyn, o_dyn, o_col) = struct.unpack_from("<HH15I", d, 0)
    r = dict(ident=ident, ver=ver, nodes=n_nodes, faces_hdr=n_faces, verts=n_verts, cols=n_cols,
             u1=n_u1, u2=n_u2, dyn=n_dyn, o_col=o_col)
    leaves, inner, leaf_faces, max_face = 0, 0, 0, 0
    masks = set()
    seen = set()
    stack = [0]
    while stack:
        off = stack.pop()
        if off in seen:
            continue
        seen.add(off)
        p = o_nodes + off
        bbox = struct.unpack_from("<6h", d, p)
        nf, m0, m1, a = struct.unpack_from("<IIIi", d, p + 12)
        masks.add((m0, m1))
        if nf:
            leaves += 1
            leaf_faces += nf
            assert a % 16 == 0
            max_face = max(max_face, a // 16 + nf)
        else:
            inner += 1
            for c in (a,) + struct.unpack_from("<ii", d, p + 0x1C):
                if c >= 0:
                    stack.append(c)
    r.update(leaves=leaves, inner=inner, leaf_faces=leaf_faces, faces_stored=(o_verts - o_faces) // 16,
             max_face=max_face, mask_pairs=len(masks))
    lights = sum(1 for i in range(n_cols) if d[o_cols + 4 * i] < 40)
    r["light_cols"] = lights
    blocks, o = [], o_dyn
    for _ in range(n_dyn):
        size, unk4, kind, group, frames, nface, o_idx, o_frames = struct.unpack_from("<4I4H", d, o)
        idx = struct.unpack_from(f"<{nface}H", d, o + o_idx)
        blocks.append(dict(at=o, size=size, unk4=unk4, kind=kind, group=group, frames=frames, faces=idx,
                           end_ok=o_frames + frames * nface * 10 <= size))
        o += size
    r["blocks"] = blocks
    r["dyn_end"] = o
    return r


def main():
    args = [a for a in sys.argv[1:] if a != "-v"]
    verbose = "-v" in sys.argv
    tot = dict(files=0, faces=0, bad=0)
    for f in args:
        d = open(f, "rb").read()
        r = parse(d)
        ok = (r["leaf_faces"] == r["faces_stored"] == r["max_face"] and r["dyn_end"] == r["o_col"]
              and all(b["end_ok"] for b in r["blocks"]) and r["u1"] == r["u2"] == 0)
        tot["files"] += 1
        tot["faces"] += r["leaf_faces"]
        tot["bad"] += not ok
        print(f"{f.split('/')[-1]:16} v{r['ver']} nodes {r['nodes']:4} ({r['inner']} inner, {r['leaves']} leaves) "
              f"faces {r['leaf_faces']:5} (header {r['faces_hdr']}) verts {r['verts']:5} light-colours "
              f"{r['light_cols']:4} pvs-pairs {r['mask_pairs']:3} dyn {len(r['blocks'])}{'' if ok else '  MISMATCH'}")
        if verbose:
            for b in r["blocks"]:
                print(f"    @0x{b['at']:X} size {b['size']} +4 {b['unk4']} kind {b['kind']} group {b['group']} "
                      f"frames {b['frames']} faces {list(b['faces'])}")
    print(f"{tot['files']} files, {tot['faces']} faces, {tot['bad']} mismatches")


if __name__ == "__main__":
    main()
