"""Remove LaserLock import redirection from warhogs_.exe.

ll_unwrap.py <original warhogs_.exe> <ll_table.json> <out.exe> <out_sites.json>

Every `call [0x550238]` (wh32lib!CallDLL) is rewritten to `call/jmp [IAT slot]` of the
API it really reaches, and the wh32lib.dll import descriptor is dropped so the
protection DLL is never loaded.

Decryption: ptr = (enc ^ key[i % 9]) + exe_delta. The 9 keys come from table rows that
LaserLock had already rewritten in memory (see ll_table.py); entry i uses the
(i mod 9)-th active timer slot of the lookup at wh32lib 0x10001DAD.
"""
import json, struct, sys

BASE = 0x400000
CALLDLL_SLOT = 0x550238

exe = bytearray(open(sys.argv[1], 'rb').read())
rows = json.load(open(sys.argv[2]))

pe = struct.unpack_from('<I', exe, 0x3c)[0]
nsec = struct.unpack_from('<H', exe, pe + 6)[0]
optsz = struct.unpack_from('<H', exe, pe + 20)[0]
opt = pe + 24
imp_dd = opt + 96 + 8  # DataDirectory[1] (import table) for PE32
secs = [struct.unpack_from('<8sIIII', exe, opt + optsz + i * 40) for i in range(nsec)]


def off(rva):
    for _, vsz, va, rsz, raw in secs:
        if va <= rva < va + max(vsz, rsz):
            return rva - va + raw
    raise ValueError(hex(rva))


def cstr(o):
    return exe[o:exe.index(b'\0', o)].decode('latin1')


# IAT slot VA -> "dll!name"
slots = {}
imp_rva, imp_size = struct.unpack_from('<II', exe, imp_dd)
o = off(imp_rva)
descs = []
while True:
    oft, _, _, name, ft = struct.unpack_from('<IIIII', exe, o)
    if not name:
        break
    dll = cstr(off(name))
    descs.append(dll)
    t, k = off(oft or ft), 0
    while True:
        v = struct.unpack_from('<I', exe, t + 4 * k)[0]
        if not v:
            break
        fn = f'#{v & 0xffff}' if v & 0x80000000 else cstr(off(v) + 2)
        slots[BASE + ft + 4 * k] = f'{dll}!{fn}'
        k += 1
    o += 20

keys = {}
for r in rows:
    if 'key' in r:
        assert keys.setdefault(r['i'] % 9, r['key']) == r['key'], 'key period 9 broken'
assert len(keys) == 9, f'only {len(keys)} of 9 keys known'

out = []
for r in rows:
    delta_ptr = ((r['enc'] ^ keys[r['i'] % 9]) + BASE) & 0xFFFFFFFF
    assert delta_ptr in slots, f"entry {r['i']} -> {delta_ptr:#x} is not an IAT slot"
    if 'ptr' in r:
        assert r['ptr'] == delta_ptr, f"entry {r['i']} mismatch"
    fo = off(r['site'] - BASE)
    assert exe[fo:fo + 6] == b'\xff\x15' + struct.pack('<I', CALLDLL_SLOT), f"unexpected bytes at {r['site']:#x}"
    exe[fo:fo + 6] = (b'\xff\x25' if r['is_jmp'] == 1 else b'\xff\x15') + struct.pack('<I', delta_ptr)
    out.append({'site': f"{r['site']:#x}", 'op': 'jmp' if r['is_jmp'] == 1 else 'call', 'slot': f'{delta_ptr:#x}', 'api': slots[delta_ptr]})

# sanity: WinMain loads DDRAW.DLL with the first redirected call
known = {e['site']: e['api'] for e in out}
assert known.get('0x4811bf') == 'KERNEL32.dll!LoadLibraryA', known.get('0x4811bf')

# no call through the CallDLL slot may remain
left = exe.count(b'\xff\x15' + struct.pack('<I', CALLDLL_SLOT)) + exe.count(b'\xff\x25' + struct.pack('<I', CALLDLL_SLOT))
assert left == 0, f'{left} CallDLL references left'

# drop wh32lib.dll: it is the first import descriptor, so skip it
assert descs[0].lower() == 'wh32lib.dll', descs
struct.pack_into('<II', exe, imp_dd, imp_rva + 20, imp_size - 20)
# clear the bound-import directory (DataDirectory[11]) in case it lists wh32lib
struct.pack_into('<II', exe, opt + 96 + 11 * 8, 0, 0)

open(sys.argv[3], 'wb').write(exe)
json.dump(out, open(sys.argv[4], 'w'), indent=1)
apis = {}
for e in out:
    apis[e['api']] = apis.get(e['api'], 0) + 1
print(f'unwrapped {len(out)} sites, {len(apis)} distinct APIs')
for a, c in sorted(apis.items(), key=lambda x: -x[1]):
    print(f'  {c:3} {a}')
