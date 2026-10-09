"""Print bytes around named exports: peexp_bytes.py <dll> <name>..."""
import struct, sys

d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
opt = pe + 24
dd = opt + (96 if struct.unpack_from('<H', d, opt)[0] == 0x10b else 112)
secs = [struct.unpack_from('<8sIIII', d, opt + optsz + i * 40) for i in range(nsec)]

def off(rva):
    for _, vsz, va, rsz, raw in secs:
        if va <= rva < va + max(vsz, rsz):
            return rva - va + raw
    raise ValueError(hex(rva))

e = off(struct.unpack_from('<I', d, dd)[0])
nfun, nnames, afun, aname, aord = struct.unpack_from('<IIIII', d, e + 20)
for i in range(nnames):
    n = off(struct.unpack_from('<I', d, off(aname) + 4 * i)[0])
    name = d[n:d.index(b'\0', n)].decode()
    if name in sys.argv[2:]:
        rva = struct.unpack_from('<I', d, off(afun) + 4 * struct.unpack_from('<H', d, off(aord) + 2 * i)[0])[0]
        o = off(rva)
        print(f'{name} rva={rva:#x} before={d[o-6:o].hex(" ")} start={d[o:o+12].hex(" ")}')
