"""Distinct executed instructions keyed by (bank, offset, instruction bytes).

usage: python coverage.py old1.rtr ... -- new1.rtr ...
Prints how many distinct instructions the new files add over the old ones.
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import rtr
from dis6280 import OPS, SIZE


def instrs(paths):
    out = set()
    for p in paths:
        for s in rtr.load(p):
            d, ex = s['data'], s['exec']
            for o in range(0x2000):
                if ex[o >> 3] >> (o & 7) & 1:
                    op = d[o]
                    n = SIZE[OPS[op][1]] if op in OPS else 1
                    if s['bank'] >= 0xF8:
                        n = 1
                    out.add((s['bank'], o, bytes(d[o:o + n])))
    return out


if __name__ == '__main__':
    a = sys.argv[1:]
    i = a.index('--') if '--' in a else len(a)
    old, new = instrs(a[:i]), instrs(a[i + 1:])
    add = new - old
    print('old', len(old), 'new', len(new), 'added', len(add))
    by = {}
    for b, o, _ in add:
        by[b] = by.get(b, 0) + 1
    print(' '.join('%02X:%d' % kv for kv in sorted(by.items())))
