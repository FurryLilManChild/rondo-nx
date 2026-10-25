"""rondo-nx translator: turns merged coverage traces into C++ code blocks.

usage: python gen.py <timing.h> <outdir> <trace files or folders...>

Each translated block is a straight run of traced instructions inside one
8 KB bank image. Every instruction is emitted with its operands baked in as
constants and executed through HuC6280::XlatInstruction, which keeps the
exact per-instruction timing of the CPU core. A block is only entered when
the bytes in memory match the bytes it was generated from (overlays reuse
the same addresses), and it exits as soon as control leaves the expected
path (taken branch, jump, interrupt), handing back to the dispatcher.

The output contains game code bytes, so it is generated locally and never
committed.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import rtr  # noqa: E402

MAX_BLOCK = 24
SKIP_OPS = {0x00, 0x73, 0xC3, 0xD3, 0xE3, 0xF3}  # BRK, block transfers
BRANCH = {0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0, 0x80, 0x44}
BBX = {0x0F + 0x10 * i for i in range(16)}
JUMPS = {0x4C, 0x6C, 0x7C, 0x20, 0x60, 0x40}
ENDERS = BRANCH | BBX | JUMPS | {0x53}  # TAM can remap the running page


def load_sizes(timing_h):
    s = open(timing_h).read()
    body = s[s.index('k_huc6280_opcode_sizes'):]
    body = body[body.index('{') + 1:body.index('}')]
    body = re.sub(r'/\*.*?\*/', '', body)
    vals = [int(x) for x in re.findall(r'\d+', body)]
    assert len(vals) == 256
    return vals


# Absolute-addressed writes and read-modify-writes: a constant target we can check
# against the code itself (self-modifying code).
ABS_WRITES = {0x8D, 0x8E, 0x8C, 0x9C, 0xEE, 0xCE, 0x0E, 0x4E, 0x2E, 0x6E, 0x0C, 0x1C}


def build(sizes, paths):
    snaps, traced_vol = rtr.merge_full(paths)
    blocks = {}  # (bank, off, masked sig) -> (seq, mask)
    stats = {'live_operands': 0, 'skipped_written_opcodes': 0}
    for s in snaps:
        bank = s['bank']
        if bank >= 0xF8:          # work RAM routines patch their own operands
            continue
        d, ex = s['data'], s['exec']
        executed = [o for o in range(0x2000) if ex[o >> 3] >> (o & 7) & 1]
        ins = {}
        for o in executed:
            op = d[o]
            n = sizes[op]
            if o + n > 0x2000:
                continue
            ins[o] = (op, n)

        # Bytes this code rewrites: from the tracer, plus constant stores into its own page.
        vol = set()
        tv = traced_vol.get(bank)
        if tv:
            vol.update(o for o in range(0x2000) if tv[o >> 3] >> (o & 7) & 1)
        pages = [p for p in range(8) if s['pages'] >> p & 1]
        for o, (op, n) in ins.items():
            if op in ABS_WRITES and n == 3:
                t = d[o + 1] | (d[o + 2] << 8)
                if (t >> 13) in pages:
                    vol.add(t & 0x1FFF)

        leaders = set()
        for o, (op, n) in ins.items():
            prev_ok = False
            for k in range(1, 8):
                p = o - k
                if p in ins and p + ins[p][1] == o and ins[p][0] not in ENDERS:
                    prev_ok = True
                    break
            if not prev_ok:
                leaders.add(o)
            if op in ENDERS or op in SKIP_OPS or o in vol:
                nxt = o + n
                if nxt in ins:
                    leaders.add(nxt)
            if op in BRANCH:
                t = o + n + (d[o + 1] - 256 if d[o + 1] > 127 else d[o + 1])
                if 0 <= t < 0x2000 and t in ins:
                    leaders.add(t)
            if op in BBX:
                t = o + n + (d[o + 2] - 256 if d[o + 2] > 127 else d[o + 2])
                if 0 <= t < 0x2000 and t in ins:
                    leaders.add(t)
        for L in leaders:
            seq = []
            o = L
            while o in ins and len(seq) < MAX_BLOCK:
                op, n = ins[o]
                if op in SKIP_OPS:
                    break
                if o in vol:              # the opcode itself gets rewritten: leave to the interpreter
                    stats['skipped_written_opcodes'] += 1
                    break
                live = any((o + k) in vol for k in range(1, n))
                if live:
                    stats['live_operands'] += 1
                seq.append((o, op, bytes(d[o + 1:o + n]), live))
                if op in ENDERS:
                    break
                o += n
            if not seq:
                continue
            end = seq[-1][0] + sizes[seq[-1][1]]
            mask = bytes(0 if (L + i) in vol else 0xFF for i in range(end - L))
            sig = bytes(b & m for b, m in zip(d[L:end], mask))
            blocks[(bank, L, sig)] = (seq, mask if 0 in mask else None)
    return blocks, stats


def emit(blocks, outdir, per_file=400):
    os.makedirs(outdir, exist_ok=True)
    for f in os.listdir(outdir):
        if f.startswith('xlat_') and f.endswith('.cpp'):
            os.remove(os.path.join(outdir, f))
    keys = sorted(blocks)
    files = []
    for fi in range(0, len(keys), per_file):
        chunk = keys[fi:fi + per_file]
        name = 'xlat_%03d.cpp' % (fi // per_file)
        files.append(name)
        with open(os.path.join(outdir, name), 'w') as w:
            w.write('// generated by tools/xlat/gen.py - do not edit, do not commit\n')
            w.write('#include "rondo_xlat_rt.h"\n\n')
            for bi, key in enumerate(chunk):
                gid = fi + bi
                bank, L, sig = key
                seq, mask = blocks[key]
                w.write('XLAT_BLOCK(%d) // bank %02X +%04X, %d instructions\n{\n' % (gid, bank, L, len(seq)))
                for i, (o, op, opr, live) in enumerate(seq):
                    if opr and not live:
                        w.write('    static const u8 o%d[] = {%s};\n' % (i, ','.join('0x%02X' % b for b in opr)))
                for i, (o, op, opr, live) in enumerate(seq):
                    # live: operands are rewritten at run time, so read them from memory
                    ops = 'o%d' % i if (opr and not live) else 'NULL'
                    nxt = o + len(opr) + 1
                    last = (i == len(seq) - 1)
                    w.write('    XLAT_STEP(0x%04X, 0x%02X, %s, 0x%04X, %d);%s\n' % (
                        o, op, ops, nxt & 0xFFFF, 1 if last else 0, ' // live operands' if live else ''))
                w.write('    return 0;\n}\n\n')
    with open(os.path.join(outdir, 'xlat_table.inc'), 'w') as w:
        w.write('// generated by tools/xlat/gen.py - do not edit, do not commit\n')
        for gid in range(len(keys)):
            w.write('XLAT_DECL(%d);\n' % gid)
        w.write('\nstatic const XlatEntry k_xlat_entries[] = {\n')
        for gid, key in enumerate(keys):
            bank, L, sig = key
            mask = blocks[key][1]
            ms = '(const u8*)"%s"' % ''.join('\\x%02X' % b for b in mask) if mask else 'NULL'
            w.write('    {0x%02X, 0x%04X, %d, (const u8*)"%s", %s, XLAT_NAME(%d)},\n' % (
                bank, L, len(sig), ''.join('\\x%02X' % b for b in sig), ms, gid))
        w.write('};\n')
        w.write('static const int k_xlat_count = %d;\n' % len(keys))
    with open(os.path.join(outdir, 'xlat_files.txt'), 'w') as w:
        w.write('\n'.join(files) + '\n')
    return len(keys), sum(len(v[0]) for v in blocks.values()), files


def expand(args):
    """Accept .rtr files and folders (searched recursively)."""
    out = []
    for a in args:
        if os.path.isdir(a):
            for root, _, files in os.walk(a):
                out += [os.path.join(root, f) for f in files if f.endswith('.rtr')]
        elif a.endswith('.rtr') and os.path.exists(a):
            out.append(a)
    return sorted(out)


if __name__ == '__main__':
    sizes = load_sizes(sys.argv[1])
    paths = expand(sys.argv[3:])
    print('traces', len(paths))
    if not paths:
        sys.exit('no .rtr trace files found')
    blocks, stats = build(sizes, paths)
    n, ninstr, files = emit(blocks, sys.argv[2])
    print('blocks', n, 'instructions', ninstr, 'files', len(files),
          'live-operand instructions', stats['live_operands'], 'rewritten opcodes left to interpreter', stats['skipped_written_opcodes'])
