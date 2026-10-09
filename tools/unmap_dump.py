"""Turn a memory dump of a PE module into a loadable PE: unmap_dump.py <dump.bin> <out.dll>

Each section's raw offset/size is set to its virtual address/size, so a loader
sees the in-memory (decrypted) bytes at the right addresses.
"""
import struct, sys

d = bytearray(open(sys.argv[1], 'rb').read())
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
sec = pe + 24 + optsz
for i in range(nsec):
    o = sec + i * 40
    vsz, va = struct.unpack_from('<II', d, o + 8)
    struct.pack_into('<II', d, o + 16, min(vsz, len(d) - va), va)  # SizeOfRawData, PointerToRawData
    print(d[o:o + 8].rstrip(b'\0').decode(), hex(va), hex(vsz))
open(sys.argv[2], 'wb').write(d)
