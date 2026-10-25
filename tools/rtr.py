"""Read and merge rondo tracer (.rtr) files.

usage: python rtr.py summary file1.rtr [file2.rtr ...]
"""
import struct
import sys

SNAP = 4 + 8 + 0x2000 + 0x400


def load_full(path):
    """Returns (snapshots, volatile) where volatile maps bank -> 1024-byte bitmap
    of operand bytes seen being rewritten at run time (version 2 traces)."""
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
    vol = {}
    if ver >= 2 and o + 4 <= len(d):
        nv, = struct.unpack_from('<I', d, o)
        o += 4
        for _ in range(nv):
            b = d[o]
            vol[b] = d[o + 4:o + 4 + 0x400]
            o += 4 + 0x400
    return out, vol


def load(path):
    return load_full(path)[0]


def merge_full(paths):
    """Merge snapshots from many sessions, keyed by (bank, hash), plus volatile bitmaps."""
    m = {}
    vol = {}
    for p in paths:
        snaps, v = load_full(p)
        for b, bits in v.items():
            if b in vol:
                vol[b] = bytes(x | y for x, y in zip(vol[b], bits))
            else:
                vol[b] = bits
        for s in snaps:
            k = (s['bank'], s['hash'])
            if k in m:
                a = m[k]
                a['exec'] = bytes(x | y for x, y in zip(a['exec'], s['exec']))
                a['pages'] |= s['pages']
            else:
                m[k] = dict(s)
    return list(m.values()), vol


def merge(paths):
    """Merge snapshots from many sessions, keyed by (bank, hash)."""
    return merge_full(paths)[0]


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
