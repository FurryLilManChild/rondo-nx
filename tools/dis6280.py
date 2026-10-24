"""Minimal HuC6280 disassembler (65C02 + HuC6280 extensions)."""
import sys

# mode: imp, imm, zp, zpx, zpy, abs, absx, absy, ind, indx, indy, zpind, rel,
#       absindx, acc, zprel (bbr/bbs), blk (tii etc), tst_zp, tst_abs, tst_zpx, tst_absx, imm_st
OPS = {}
def op(code, mn, mode): OPS[code] = (mn, mode)

alu = [('ORA', 0x00), ('AND', 0x20), ('EOR', 0x40), ('ADC', 0x60), ('STA', 0x80), ('LDA', 0xA0), ('CMP', 0xC0), ('SBC', 0xE0)]
for mn, b in alu:
    op(b | 0x01, mn, 'indx'); op(b | 0x05, mn, 'zp'); op(b | 0x0D, mn, 'abs')
    op(b | 0x11, mn, 'indy'); op(b | 0x12, mn, 'zpind'); op(b | 0x15, mn, 'zpx')
    op(b | 0x19, mn, 'absy'); op(b | 0x1D, mn, 'absx')
    if mn != 'STA': op(b | 0x09, mn, 'imm')
sh = [('ASL', 0x00), ('ROL', 0x20), ('LSR', 0x40), ('ROR', 0x60)]
for mn, b in sh:
    op(b | 0x06, mn, 'zp'); op(b | 0x0A, mn, 'acc'); op(b | 0x0E, mn, 'abs')
    op(b | 0x16, mn, 'zpx'); op(b | 0x1E, mn, 'absx')
for i in range(8):
    op(0x07 + i * 0x10, 'RMB%d' % i, 'zp'); op(0x87 + i * 0x10, 'SMB%d' % i, 'zp')
    op(0x0F + i * 0x10, 'BBR%d' % i, 'zprel'); op(0x8F + i * 0x10, 'BBS%d' % i, 'zprel')
br = {0x10: 'BPL', 0x30: 'BMI', 0x50: 'BVC', 0x70: 'BVS', 0x90: 'BCC', 0xB0: 'BCS', 0xD0: 'BNE', 0xF0: 'BEQ', 0x80: 'BRA', 0x44: 'BSR'}
for c, m in br.items(): op(c, m, 'rel')
imp = {0x00: 'BRK', 0x08: 'PHP', 0x18: 'CLC', 0x28: 'PLP', 0x38: 'SEC', 0x40: 'RTI', 0x48: 'PHA', 0x58: 'CLI',
       0x60: 'RTS', 0x68: 'PLA', 0x78: 'SEI', 0x88: 'DEY', 0x8A: 'TXA', 0x98: 'TYA', 0x9A: 'TXS', 0xA8: 'TAY',
       0xAA: 'TAX', 0xB8: 'CLV', 0xBA: 'TSX', 0xC8: 'INY', 0xCA: 'DEX', 0xD8: 'CLD', 0xE8: 'INX', 0xEA: 'NOP',
       0xF8: 'SED', 0x5A: 'PHY', 0x7A: 'PLY', 0xDA: 'PHX', 0xFA: 'PLX', 0x1A: 'INC', 0x3A: 'DEC',
       0x02: 'SXY', 0x22: 'SAX', 0x42: 'SAY', 0x62: 'CLA', 0x82: 'CLX', 0xC2: 'CLY', 0xD4: 'CSH', 0x54: 'CSL', 0xF4: 'SET'}
for c, m in imp.items(): op(c, m, 'imp')
op(0x20, 'JSR', 'abs'); op(0x4C, 'JMP', 'abs'); op(0x6C, 'JMP', 'ind'); op(0x7C, 'JMP', 'absindx')
op(0x24, 'BIT', 'zp'); op(0x2C, 'BIT', 'abs'); op(0x34, 'BIT', 'zpx'); op(0x3C, 'BIT', 'absx'); op(0x89, 'BIT', 'imm')
op(0x84, 'STY', 'zp'); op(0x8C, 'STY', 'abs'); op(0x94, 'STY', 'zpx')
op(0x86, 'STX', 'zp'); op(0x8E, 'STX', 'abs'); op(0x96, 'STX', 'zpy')
op(0x64, 'STZ', 'zp'); op(0x74, 'STZ', 'zpx'); op(0x9C, 'STZ', 'abs'); op(0x9E, 'STZ', 'absx')
op(0xA0, 'LDY', 'imm'); op(0xA4, 'LDY', 'zp'); op(0xAC, 'LDY', 'abs'); op(0xB4, 'LDY', 'zpx'); op(0xBC, 'LDY', 'absx')
op(0xA2, 'LDX', 'imm'); op(0xA6, 'LDX', 'zp'); op(0xAE, 'LDX', 'abs'); op(0xB6, 'LDX', 'zpy'); op(0xBE, 'LDX', 'absy')
op(0xC0, 'CPY', 'imm'); op(0xC4, 'CPY', 'zp'); op(0xCC, 'CPY', 'abs')
op(0xE0, 'CPX', 'imm'); op(0xE4, 'CPX', 'zp'); op(0xEC, 'CPX', 'abs')
op(0xC6, 'DEC', 'zp'); op(0xCE, 'DEC', 'abs'); op(0xD6, 'DEC', 'zpx'); op(0xDE, 'DEC', 'absx')
op(0xE6, 'INC', 'zp'); op(0xEE, 'INC', 'abs'); op(0xF6, 'INC', 'zpx'); op(0xFE, 'INC', 'absx')
op(0x04, 'TSB', 'zp'); op(0x0C, 'TSB', 'abs'); op(0x14, 'TRB', 'zp'); op(0x1C, 'TRB', 'abs')
op(0x43, 'TMA', 'imm'); op(0x53, 'TAM', 'imm')
op(0x03, 'ST0', 'imm'); op(0x13, 'ST1', 'imm'); op(0x23, 'ST2', 'imm')
for c, m in {0x73: 'TII', 0xC3: 'TDD', 0xD3: 'TIN', 0xE3: 'TIA', 0xF3: 'TAI'}.items(): op(c, m, 'blk')
op(0x83, 'TST', 'tst_zp'); op(0x93, 'TST', 'tst_abs'); op(0xA3, 'TST', 'tst_zpx'); op(0xB3, 'TST', 'tst_absx')

SIZE = {'imp': 1, 'acc': 1, 'imm': 2, 'zp': 2, 'zpx': 2, 'zpy': 2, 'indx': 2, 'indy': 2, 'zpind': 2, 'rel': 2,
        'abs': 3, 'absx': 3, 'absy': 3, 'ind': 3, 'absindx': 3, 'zprel': 3, 'blk': 7,
        'tst_zp': 3, 'tst_zpx': 3, 'tst_abs': 4, 'tst_absx': 4}

def decode(mem, pc, base):
    o = pc - base
    c = mem[o]
    if c not in OPS:
        return 1, '.db $%02X' % c, None
    mn, m = OPS[c]
    n = SIZE[m]
    b = mem[o + 1:o + n]
    w = lambda i: b[i] | (b[i + 1] << 8)
    tgt = None
    if m in ('imp',): s = ''
    elif m == 'acc': s = 'A'
    elif m == 'imm': s = '#$%02X' % b[0]
    elif m == 'zp': s = '$%02X' % b[0]
    elif m == 'zpx': s = '$%02X,X' % b[0]
    elif m == 'zpy': s = '$%02X,Y' % b[0]
    elif m == 'indx': s = '($%02X,X)' % b[0]
    elif m == 'indy': s = '($%02X),Y' % b[0]
    elif m == 'zpind': s = '($%02X)' % b[0]
    elif m == 'rel':
        tgt = (pc + 2 + (b[0] - 256 if b[0] > 127 else b[0])) & 0xFFFF; s = '$%04X' % tgt
    elif m == 'abs': tgt = w(0); s = '$%04X' % tgt
    elif m == 'absx': s = '$%04X,X' % w(0)
    elif m == 'absy': s = '$%04X,Y' % w(0)
    elif m == 'ind': s = '($%04X)' % w(0)
    elif m == 'absindx': s = '($%04X,X)' % w(0)
    elif m == 'zprel':
        tgt = (pc + 3 + (b[1] - 256 if b[1] > 127 else b[1])) & 0xFFFF; s = '$%02X,$%04X' % (b[0], tgt)
    elif m == 'blk': s = '$%04X,$%04X,$%04X' % (w(0), w(2), w(4))
    elif m == 'tst_zp': s = '#$%02X,$%02X' % (b[0], b[1])
    elif m == 'tst_zpx': s = '#$%02X,$%02X,X' % (b[0], b[1])
    elif m == 'tst_abs': s = '#$%02X,$%04X' % (b[0], b[1] | b[2] << 8)
    elif m == 'tst_absx': s = '#$%02X,$%04X,X' % (b[0], b[1] | b[2] << 8)
    return n, (mn + ' ' + s).strip(), (mn, m, tgt)

def listing(mem, base, start, count):
    pc = start
    out = []
    for _ in range(count):
        if pc - base >= len(mem): break
        n, txt, _ = decode(mem, pc, base)
        raw = ' '.join('%02X' % x for x in mem[pc - base:pc - base + n])
        out.append('%04X  %-20s %s' % (pc, raw, txt))
        pc += n
    return '\n'.join(out)

if __name__ == '__main__':
    f, off, base, cnt = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0), int(sys.argv[4])
    d = open(f, 'rb').read()
    print(listing(d[off:off + 0x10000], base, base, cnt))
