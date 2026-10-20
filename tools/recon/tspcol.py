#!/usr/bin/env python3
"""Reads the collision block of TSP world chunks (docs/re/levels.md, Collision).

  tspcol.py FILE.TSP...      summary and consistency checks per file
  tspcol.py --obj FILE.TSP   the collision triangles as a Wavefront OBJ on stdout

The block is at the offset in the TSP header's word 15 (+0x3C):
  +0x00 s16 x min, z min, x max, z max      the chunk's rectangle
  +0x08 u16 nodes, face indices, vertices, normals, faces
  +0x12 nodes (8 bytes: s16 a, s16 b, u16 split, pad), the k-d tree over x/z, root 0:
          a < 0            leaf: ~a faces from the face index list at b
          a >= 0, b >= 0   split on x at x min + split: children a, b
          a >= 0, b < 0    split on z at z min + split: children a, ~b
        face indices (u16), padded to 4 bytes
        vertices (8 bytes: s16 x, y, z, pad)
        normals (8 bytes: s16 x, y, z (1.0 = 0x8000?), pad)
        faces (10 bytes: u16 v0, v1, v2, normal, s16 d)
"""
import struct
import sys


def parse(data):
    off = struct.unpack_from("<I", data, 0x3C)[0]
    xmin, zmin, xmax, zmax = struct.unpack_from("<4h", data, off)
    n_nodes, n_idx, n_verts, n_norms, n_faces = struct.unpack_from("<5H", data, off + 8)
    p = off + 0x12
    nodes = [struct.unpack_from("<hhH", data, p + 8 * i) for i in range(n_nodes)]
    p += 8 * n_nodes
    idx = list(struct.unpack_from(f"<{n_idx}H", data, p))
    p += 2 * n_idx
    p += (-p) % 4
    verts = [struct.unpack_from("<3h", data, p + 8 * i) for i in range(n_verts)]
    p += 8 * n_verts
    norms = [struct.unpack_from("<3h", data, p + 8 * i) for i in range(n_norms)]
    p += 8 * n_norms
    faces = [struct.unpack_from("<4Hh", data, p + 10 * i) for i in range(n_faces)]
    p += 10 * n_faces
    return dict(rect=(xmin, zmin, xmax, zmax), nodes=nodes, idx=idx, verts=verts, norms=norms,
                faces=faces, start=off, end=p)


def check(c):
    """Problems found walking the tree and the indices (empty: consistent)."""
    problems = []
    seen_faces = set()
    leaves = 0
    stack = [(0, 0)]
    visited = set()
    while stack:
        n, depth = stack.pop()
        if n in visited or not 0 <= n < len(c["nodes"]):
            problems.append(f"node {n} out of range or revisited")
            continue
        visited.add(n)
        a, b, split = c["nodes"][n]
        if a < 0:
            leaves += 1
            count = ~a
            if b < 0 or b + count > len(c["idx"]):
                problems.append(f"leaf {n}: list {b}+{count} beyond {len(c['idx'])}")
                continue
            seen_faces.update(c["idx"][b:b + count])
        else:
            stack.append((a, depth + 1))
            stack.append((b if b >= 0 else ~b, depth + 1))
    for f in seen_faces:
        if f >= len(c["faces"]):
            problems.append(f"face {f} beyond {len(c['faces'])}")
    for i, (v0, v1, v2, nrm, d) in enumerate(c["faces"]):
        if max(v0, v1, v2) >= len(c["verts"]) or nrm >= len(c["norms"]):
            problems.append(f"face {i} indexes beyond the arrays")
            break
    return problems, leaves, len(visited), len(seen_faces)


def main():
    args = sys.argv[1:]
    if args[:1] == ["--obj"]:
        c = parse(open(args[1], "rb").read())
        for x, y, z in c["verts"]:
            print(f"v {x} {-y} {z}")
        for v0, v1, v2, _, _ in c["faces"]:
            print(f"f {v0 + 1} {v1 + 1} {v2 + 1}")
        return
    for path in args:
        data = open(path, "rb").read()
        c = parse(data)
        problems, leaves, nodes, used = check(c)
        print(f"{path}: rect {c['rect']}, {len(c['nodes'])} nodes ({nodes} reached, {leaves} leaves), "
              f"{len(c['faces'])} faces ({used} in leaves), {len(c['verts'])} vertices, "
              f"{len(c['norms'])} normals; block {c['start']:X}..{c['end']:X} of {len(data):X}"
              + ("" if not problems else f"; PROBLEMS: {problems[:3]}"))


if __name__ == "__main__":
    main()
