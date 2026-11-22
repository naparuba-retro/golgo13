"""H8/300H (advanced mode) instruction decoder.

Decoding table = MAME's generated disassembler table (src/devices/cpu/h8/h8make.py h8.lst d h -> h8d.hxx, BSD-3-Clause,
Olivier Galibert); operand decoding follows h8d.cpp. Produces Insn objects with typed operands used by the C generator.
"""
import re, os, struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
HXX = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'h8d.hxx')
_ROW = re.compile(r'\{ (\d+), (0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+), "([^"]*)", (DASM_\w+), (DASM_\w+), (\d+)(?: \| ([A-Z_]+))? \}')
TABLE = []
for l in open(HXX):
    m = _ROW.search(l)
    if m:
        slot, val, mask, val0, mask0, name, a1, a2, ln, fl = m.groups()
        TABLE.append((int(slot), int(val, 16), int(mask, 16), int(val0, 16), int(mask0, 16), name, a1[5:], a2[5:], int(ln), fl))
    elif '"illegal"' in l:
        pass

R8 = ["r0h", "r1h", "r2h", "r3h", "r4h", "r5h", "r6h", "r7h", "r0l", "r1l", "r2l", "r3l", "r4l", "r5l", "r6l", "r7l"]
R16 = ["r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7"]
R32 = ["er0", "er1", "er2", "er3", "er4", "er5", "er6", "sp"]

class Op:
    """kind: r8 r16 r32 ind(reg32,disp) postinc predec abs imm ccr rel one/two/four (value)"""
    __slots__ = ('kind', 'reg', 'val')
    def __init__(self, kind, reg=None, val=None): self.kind = kind; self.reg = reg; self.val = val
    def __repr__(self):
        k = self.kind
        if k == 'r8': return R8[self.reg]
        if k == 'r16': return R16[self.reg]
        if k == 'r32': return R32[self.reg]
        if k == 'ind': return f"@({self.val:#x},{R32[self.reg]})" if self.val else f"@{R32[self.reg]}"
        if k == 'postinc': return f"@{R32[self.reg]}+"
        if k == 'predec': return f"@-{R32[self.reg]}"
        if k == 'abs': return f"@{self.val:#08x}"
        if k == 'imm': return f"#{self.val:#x}"
        if k == 'rel': return f"{self.val:#06x}"
        if k == 'ccr': return "ccr"
        if k == 'memind': return f"@@{self.val:#x}"
        return f"{k}:{self.val}"

class Insn:
    __slots__ = ('pc', 'size', 'name', 'ops', 'flow', 'word')
    def __init__(self, pc, size, name, ops, flow, word):
        self.pc = pc; self.size = size; self.name = name; self.ops = ops; self.flow = flow; self.word = word
    def __repr__(self): return f"{self.pc:06x}: {self.name} {', '.join(map(repr, self.ops))}"

def s8(v): return v - 0x100 if v & 0x80 else v
def s16(v): return v - 0x10000 if v & 0x8000 else v

def decode(code, pc, advanced=True):
    """code: bytes of the whole ROM (big endian). Returns Insn."""
    def r16(a): return (code[a] << 8) | code[a + 1] if a + 1 < len(code) else 0
    def r32(a): return (r16(a) << 16) | r16(a + 2)
    slot = [r16(pc), r32(pc), (r16(pc) << 16) | r16(pc + 4), (r16(pc) << 16) | r16(pc + 6), r32(pc + 2)]
    for (sl, val, mask, val0, mask0, name, a1, a2, ln, fl) in TABLE:
        if (slot[sl] & mask) == val and (slot[0] & mask0) == val0:
            break
    else:
        return Insn(pc, 2, 'illegal', [], None, slot[0])
    opc = slot[sl]; epc = pc + ln
    def am(a):
        if a == 'none': return None
        if a == 'r8l': return Op('r8', opc & 15)
        if a == 'r8h': return Op('r8', (opc >> 4) & 15)
        if a == 'r8u': return Op('r8', (opc >> 8) & 15)
        if a == 'r16l': return Op('r16', opc & 15)
        if a == 'r16h': return Op('r16', (opc >> 4) & 15)
        if a == 'r32l': return Op('r32', opc & 7)
        if a == 'r32h': return Op('r32', (opc >> 4) & 7)
        if a == 'r32ih': return Op('ind', (opc >> 4) & 7, 0)
        if a == 'r32ihh': return Op('ind', (opc >> 20) & 7, 0)
        if a == 'pr32h': return Op('predec', (opc >> 4) & 7)
        if a == 'r32pl': return Op('postinc', opc & 7)
        if a == 'r32ph': return Op('postinc', (opc >> 4) & 7)
        if a == 'r32d16h': return Op('ind', (opc >> 4) & 7, r16(epc - 2))
        if a == 'r32d32hh': return Op('ind', (opc >> 20) & 7, r32(epc - 4))
        if a == 'psp': return Op('predec', 7)
        if a == 'spp': return Op('postinc', 7)
        if a == 'abs8': return Op('abs', None, 0xffff00 | code[pc + 1])
        if a == 'abs16':
            v = r16(epc - 4) if sl == 2 else r16(epc - 2)
            return Op('abs', None, s16(v) & 0xffffff)
        if a == 'abs32':
            v = r32(epc - 6) if sl == 3 else r32(epc - 4)
            return Op('abs', None, v & 0xffffff)
        if a == 'abs8i': return Op('memind', None, code[pc + 1])
        if a == 'abs24e': return Op('abs', None, r32(pc) & 0xffffff)
        if a == 'rel8': return Op('rel', None, (pc + 2 + s8(code[pc + 1])) & 0xffffff)
        if a == 'rel16': return Op('rel', None, (pc + 4 + s16(r16(pc + 2))) & 0xffffff)
        if a == 'one': return Op('imm', None, 1)
        if a == 'two': return Op('imm', None, 2)
        if a == 'four': return Op('imm', None, 4)
        if a == 'imm2': return Op('imm', None, (opc >> 4) & 3)
        if a == 'imm3': return Op('imm', None, (opc >> 4) & 7)
        if a == 'imm8': return Op('imm', None, code[epc - 1])
        if a == 'imm16': return Op('imm', None, r16(pc + 2))
        if a == 'imm32': return Op('imm', None, r32(pc + 2))
        if a == 'ccr': return Op('ccr')
        raise ValueError("unhandled addressing mode " + a)
    ops = [o for o in (am(a1), am(a2)) if o is not None]
    return Insn(pc, ln, name, ops, fl, slot[0])

if __name__ == '__main__':
    import sys
    rom = open(os.path.join(__import__('gamecfg').REGIONS, 'sub.bin'), 'rb').read()
    pc = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x100
    n = int(sys.argv[2], 0) if len(sys.argv) > 2 else 40
    for _ in range(n):
        i = decode(rom, pc); print(i); pc += i.size
