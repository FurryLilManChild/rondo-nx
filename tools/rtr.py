"""Read and merge rondo tracer (.rtr) files.

usage: python rtr.py summary file1.rtr [file2.rtr ...]
"""
import struct
import sys

SNAP = 4 + 8 + 0x2000 + 0x400


def load(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'RTRC', path
    ver, n = struct.unpack_from('<II', d, 4)
    out = []
    o = 12
    for _ in range(n):
        bank, pmask = d[o], d[o + 1]
        h, = struct.unpack_from('<Q', d, o + 4)
        data = d[o + 12:o + 12 + 0x2000]
        ex = d[o + 12 + 0x2000:o + SNAP]
        out.append({'bank': bank, 'pages': pmask, 'hash': h, 'data': data, 'exec': ex})
        o += SNAP
    return out


def merge(paths):
    """Merge snapshots from many sessions, keyed by (bank, hash)."""
    m = {}
    for p in paths:
        for s in load(p):
            k = (s['bank'], s['hash'])
            if k in m:
                a = m[k]
                a['exec'] = bytes(x | y for x, y in zip(a['exec'], s['exec']))
                a['pages'] |= s['pages']
            else:
                m[k] = dict(s)
    return list(m.values())


def popcount(b):
    return sum(bin(x).count('1') for x in b)


def kind(bank):
    if bank < 0x40:
        return 'BIOS ROM'
    if 0x68 <= bank < 0x80:
        return 'Super RAM'
    if 0x80 <= bank < 0x88:
        return 'CD RAM'
    if bank >= 0xF8:
        return 'Work RAM'
    return 'other'


if __name__ == '__main__':
    snaps = merge(sys.argv[2:])
    total = 0
    by_kind = {}
    for s in sorted(snaps, key=lambda s: (s['bank'], s['hash'])):
        c = popcount(s['exec'])
        total += c
        k = kind(s['bank'])
        by_kind[k] = by_kind.get(k, 0) + c
        pages = ','.join(str(i) for i in range(8) if s['pages'] >> i & 1)
        print('bank %02X %-9s pages %-6s hash %016X  instr %5d' % (s['bank'], k, pages, s['hash'], c))
    print('images', len(snaps), 'instructions', total, by_kind)
