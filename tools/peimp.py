"""List the imports of PE files: peimp.py <file>..."""
import struct, sys, datetime

def imports(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    machine, nsec, ts = struct.unpack_from('<HHI', d, pe + 4)
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    ddoff = opt + (96 if magic == 0x10b else 112)
    imp_rva = struct.unpack_from('<I', d, ddoff + 8)[0]
    secs = [struct.unpack_from('<8sIIII', d, opt + optsz + i * 40) for i in range(nsec)]

    def off(rva):
        for name, vsz, va, rsz, raw in secs:
            if va <= rva < va + max(vsz, rsz):
                return rva - va + raw
        raise ValueError(hex(rva))

    def cstr(o):
        return d[o:d.index(b'\0', o)].decode('latin1')

    built = datetime.datetime.fromtimestamp(ts, datetime.timezone.utc)
    names = [s[0].rstrip(b'\0').decode() for s in secs]
    print(f'== {path}\n machine={machine:#x} built={built} sections={names}')
    o = off(imp_rva)
    while True:
        oft, _, _, name, ft = struct.unpack_from('<IIIII', d, o)
        if not name:
            break
        lst, t = [], off(oft or ft)
        while True:
            v = struct.unpack_from('<I', d, t)[0]
            if not v:
                break
            lst.append(f'#{v & 0xffff}' if v & 0x80000000 else cstr(off(v) + 2))
            t += 4
        print(f' {cstr(off(name))}: {", ".join(lst)}')
        o += 20

for p in sys.argv[1:]:
    try:
        imports(p)
    except Exception as e:
        print('==', p, 'ERR', e)
