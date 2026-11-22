"""Static flow discovery for the H8/3002 firmware (glg1vera.11s as seen by the H8: big-endian, base 0).
Roots = non-zero entries of the 4-byte vector table at 0x000-0x0ff (advanced mode). Output: work/h8/flow.json"""
import sys, os, json, struct
sys.path.insert(0, os.path.dirname(__file__))
from h8dec import decode, ROOT
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import gamecfg

rom = open(os.path.join(gamecfg.REGIONS, 'sub.bin'), 'rb').read()
SIZE = len(rom)
code = {}      # pc -> Insn
funcs = set(); work = []; unres = []; vec_roots = {}
def add(a, fn=False):
    if 0x100 <= a < SIZE and a % 2 == 0:
        if fn: funcs.add(a)
        work.append(a)
for i in range(0, 0x100, 4):
    v = struct.unpack_from('>I', rom, i)[0] & 0xffffff
    if v and i != 0 or i == 0:
        if 0x100 <= v < SIZE: vec_roots[i] = v; add(v, True)
_er = os.path.join(gamecfg.H8FLOW, 'extra_roots.json')
if os.path.exists(_er):
    for a in json.load(open(_er)): add(a, True)
COND = {'bhi', 'bls', 'bcc', 'bcs', 'bne', 'beq', 'bvc', 'bvs', 'bpl', 'bmi', 'bge', 'blt', 'bgt', 'ble', 'bf'}
def mem32(a): return struct.unpack_from('>I', rom, a)[0] & 0xffffff
while work:
    pc = work.pop()
    while 0 <= pc < SIZE and pc not in code:
        i = decode(rom, pc); code[pc] = i
        n = i.name; nxt = pc + i.size
        if n == 'illegal': break
        tgt = [o for o in i.ops if o.kind in ('rel', 'abs', 'memind') ]
        if n in COND: add(i.ops[0].val); pc = nxt; continue
        if n == 'bt':  add(i.ops[0].val); break           # bra
        if n == 'bsr': add(i.ops[0].val, True); pc = nxt; continue
        if n in ('jmp', 'jsr'):
            o = i.ops[0]
            if o.kind == 'abs': add(o.val, n == 'jsr')
            elif o.kind == 'memind':
                t = mem32(o.val * 4) if o.val * 4 + 4 <= SIZE else 0
                if t: add(t, n == 'jsr')
                else: unres.append(pc)
            else: unres.append(pc)
            if n == 'jmp': break
            pc = nxt; continue
        if n in ('rts', 'rte', 'sleep'): break
        if n == 'trapa': pc = nxt; continue
        pc = nxt
# pass 2: code pointers built as immediates (mov.l #imm, ern ; mov.w #imm ...) -> function roots, iterate to a fixed point
def drain():
    while work:
        pc = work.pop()
        while 0 <= pc < SIZE and pc not in code:
            i = decode(rom, pc); code[pc] = i
            n = i.name; nxt = pc + i.size
            if n == 'illegal': break
            if n in COND: add(i.ops[0].val); pc = nxt; continue
            if n == 'bt': add(i.ops[0].val); break
            if n == 'bsr': add(i.ops[0].val, True); pc = nxt; continue
            if n in ('jmp', 'jsr'):
                o = i.ops[0]
                if o.kind == 'abs': add(o.val, n == 'jsr')
                elif o.kind == 'memind':
                    t = mem32(o.val * 4) if o.val * 4 + 4 <= SIZE else 0
                    if t: add(t, n == 'jsr')
                if n == 'jmp': break
                pc = nxt; continue
            if n in ('rts', 'rte', 'sleep'): break
            pc = nxt
imm_roots = 0
for rnd in range(8):
    new = 0
    for pc, i in sorted(code.items()):
        if i.name in ('mov.l', 'mov.w') and i.ops and i.ops[0].kind == 'imm':
            v = i.ops[0].val
            if i.name == 'mov.w' and i.ops[1].kind == 'r16' and i.ops[1].reg >= 8: continue
            if 0x100 <= v < 0x60000 and v % 2 == 0 and v not in code and v not in funcs:
                funcs.add(v); work.append(v); new += 1
    imm_roots += new
    if not new: break
    drain()
print("immediate-pointer roots:", imm_roots)
cover = sum(i.size for i in code.values())
print(f"vectors: {len(vec_roots)} roots; code instrs {len(code)}, bytes {cover} of {SIZE} ({cover/SIZE:.1%}); funcs {len(funcs)}; unresolved indirect {len(unres)}")
print("unresolved:", [hex(x) for x in unres][:20])
print("vector table:", {hex(k): hex(v) for k, v in sorted(vec_roots.items())})
json.dump({'code': sorted(code), 'funcs': sorted(funcs), 'unresolved': unres, 'vectors': {str(k): v for k, v in vec_roots.items()}},
          open(os.path.join(gamecfg.H8FLOW, 'flow.json'), 'w'))
