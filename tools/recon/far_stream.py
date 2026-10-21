#!/usr/bin/env python3
"""Chunk streaming in a RAM dump, for reaching parts of a level outside the start's chunks
(docs/re/enemy_types.md, Warped runs; formats.md, chunk streaming records).

  far_stream.py DUMP                 the streaming records (class 0x4D078F23): index, record
                                     address, centre, shape and sizes, the four chunks; the
                                     resident chunks per slot and the current record
  far_stream.py DUMP X Y Z           the same, sorted by distance from (X, Y, Z)

`chunk_stream` (0x8005439C, run from the idle job every few frames) loads, one at a time, the
chunks that the current streaming record (0x80099070, set on entering one) names and no slot
holds, into slots holding chunks it doesn't name. Poking 0x80099070 with another record's
address streams that record's chunks without going there.
"""
import math
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from lvl_records import Ram, records  # noqa: E402

CUR = 0x80099070
SLOTS = 0x80098970
LOADING = 0x800AA184


def main():
    ram = Ram(sys.argv[1])
    at = tuple(int(v) for v in sys.argv[2:5]) if len(sys.argv) >= 5 else None
    cur = ram.u32(CUR)
    rows = []
    for i, pair, state, rec in records(ram):
        if ram.u32(rec) != 0x4D078F23:
            continue
        c = tuple(ram.s16(rec + 0x10 + 2 * k) for k in range(3))
        shape = [ram.s16(rec + 0x28 + 2 * k) for k in range(4)]
        ang = [ram.s16(rec + 0x18 + 2 * k) for k in range(3)]
        chunks = [ram.s16(rec + 0x30 + 2 * k) for k in range(4)]
        d = math.dist(c, at) if at else 0
        rows.append((d, i, rec, c, shape, ang, chunks))
    if at:
        rows.sort()
    for d, i, rec, c, shape, ang, chunks in rows:
        mark = ' <- current' if rec == cur else ''
        dist = f' d {d:6.0f}' if at else ''
        print(f'{i:3} rec {rec:08X} at {c} shape {shape[0]} size {shape[1:]} ang {ang} '
              f'chunks {chunks}{dist}{mark}')
    print('slots', [ram.s32(SLOTS + 4 * k) for k in range(4)], 'loading', ram.s32(LOADING),
          f'current {cur:08X}')


if __name__ == '__main__':
    main()
