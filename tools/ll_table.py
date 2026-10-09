"""Dump the LaserLock import-redirection table from an in-memory wh32lib image.

ll_table.py <wh32lib dump (mapped, base 0x10000000)> <warhogs_ dump (mapped, base 0x400000)> <out.json>

Table layout (from the decrypted lookup at 0x10001DAD):
  count  : u32 @ 0x10012114
  entries: 9 bytes each @ 0x10012118 -> {u32 ret_rva, u32 enc_ptr, u8 is_jmp}
ret_rva is the return address relative to the exe delta at 0x10017A48.
"""
import json, struct, sys

LIB = 0x10000000
EXE = 0x400000
lib = open(sys.argv[1], 'rb').read()
exe = open(sys.argv[2], 'rb').read()

count = struct.unpack_from('<I', lib, 0x10012114 - LIB)[0]
delta = struct.unpack_from('<i', lib, 0x10017A48 - LIB)[0]
print(f'count={count} exe_delta={delta:#x}')

rows = []
for i in range(count):
    rva, enc, is_jmp = struct.unpack_from('<IIB', lib, 0x10012118 - LIB + 9 * i)
    ret = rva + delta
    site = ret - 6
    insn = exe[site - EXE:ret - EXE]
    row = {'i': i, 'ret': ret, 'site': site, 'is_jmp': is_jmp, 'enc': enc, 'bytes': insn.hex()}
    if insn[2:6] != struct.pack('<I', 0x550238):  # LaserLock already rewrote this site at runtime
        ptr = struct.unpack_from('<I', insn, 2)[0]
        row['ptr'] = ptr
        row['key'] = (ptr - delta) ^ enc
    rows.append(row)

kinds = {}
for r in rows:
    kinds[r['bytes'][:4] + '..' + r['bytes'][4:]] = kinds.get(r['bytes'][:4] + '..' + r['bytes'][4:], 0) + 1
print('site byte patterns:', kinds)
print('is_jmp:', sum(r['is_jmp'] == 1 for r in rows), 'other flags:', sorted({r['is_jmp'] for r in rows}))
keys = sorted({r['key'] for r in rows if 'key' in r})
print('resolved:', sum('key' in r for r in rows), 'distinct keys:', [hex(k) for k in keys])
json.dump(rows, open(sys.argv[3], 'w'), indent=1)
