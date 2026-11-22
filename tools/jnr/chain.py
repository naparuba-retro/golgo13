"""Reconstruct the encrypted game image block order from a MAME RAM dump (oracle) : follows the decrypter stream block by block."""
import sys, struct, copy, time
sys.path.insert(0, 'tools/jnr')
import nand
n = nand.Nand(nand.load('roms/g13jnr.zip'))
ram = open('work/jnr/ram_3600.bin', 'rb').read()
dec = nand.g13jnr_decrypter(); dec.init(0)
raw_first = {}
def block_words(dev, blk):
    for p in range(32):
        yield n.raw_words(dev, blk * 32 + p)
def first_words(dev, blk, k=16): return n.raw_words(dev, blk * 32)[:k]
off = 0x10000; order = []
used = set()
t0 = time.time()
# first block is known: dev 0 block 1023
cands = [(0, 1023)]
while off < 0x110000:
    found = None
    for (dev, blk) in (cands if cands else [(d, b) for d in range(6) for b in range(1024)]):
        if (dev, blk) in used: continue
        st = (dec.pc, dec.pp, dec.mask)
        ok = True
        want = ram[off:off + 32]
        out = b''.join(struct.pack('<H', dec.decrypt(w)) for w in first_words(dev, blk))
        dec.pc, dec.pp, dec.mask = st
        if out == want: found = (dev, blk); break
    if not found: print('chain broken at RAM offset', hex(off + 0x80000000), 'after', len(order), 'blocks'); break
    dev, blk = found
    # verify the whole block while advancing
    good = True
    st = (dec.pc, dec.pp, dec.mask)
    for pg, words in enumerate(block_words(dev, blk)):
        out = b''.join(struct.pack('<H', dec.decrypt(w)) for w in words)
        if out != ram[off + pg * 512: off + pg * 512 + 512]: good = False; break
    if not good:
        dec.pc, dec.pp, dec.mask = st; used.add(found); print('first page matched but block', found, 'differs at page', pg, 'RAM', hex(off + 0x80000000)); continue
    order.append(found); used.add(found); off += 0x4000; cands = None
    if len(order) % 8 == 0: print(len(order), hex(0x80000000 + off), round(time.time() - t0)); sys.stdout.flush()
print(order)
open('work/jnr/block_order.txt', 'w').write(repr(order))
