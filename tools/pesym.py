"""Resolve RVAs to nearest preceding export: pesym.py <dll> <rva_hex>..."""
import struct, sys

d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
opt = pe + 24
magic = struct.unpack_from('<H', d, opt)[0]
dd = opt + (96 if magic == 0x10b else 112)
exp_rva = struct.unpack_from('<I', d, dd)[0]
secs = [struct.unpack_from('<8sIIII', d, opt + optsz + i * 40) for i in range(nsec)]

def off(rva):
    for _, vsz, va, rsz, raw in secs:
        if va <= rva < va + max(vsz, rsz):
            return rva - va + raw
    raise ValueError(hex(rva))

e = off(exp_rva)
nfun, nnames, afun, aname, aord = struct.unpack_from('<IIIII', d, e + 20)
funcs = [struct.unpack_from('<I', d, off(afun) + 4 * i)[0] for i in range(nfun)]
syms = []
for i in range(nnames):
    nrva = struct.unpack_from('<I', d, off(aname) + 4 * i)[0]
    o = struct.unpack_from('<H', d, off(aord) + 2 * i)[0]
    n = off(nrva)
    syms.append((funcs[o], d[n:d.index(b'\0', n)].decode()))
syms.sort()
for a in sys.argv[2:]:
    r = int(a, 16)
    best = max((s for s in syms if s[0] <= r), default=(0, '?'))
    print(f'{a}: {best[1]}+{r - best[0]:#x}')
