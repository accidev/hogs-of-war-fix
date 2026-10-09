"""Build the distributable patch description: make_patch.py <original.exe> <patched.exe> <ll_sites.json> <out.json>

The JSON holds only offsets and the few bytes around each change (old and new), plus the
SHA-256 of the input and output files, so the patcher can verify both ends. No game code.
"""
import hashlib, json, struct, sys

BASE = 0x400000
orig = open(sys.argv[1], 'rb').read()
new = open(sys.argv[2], 'rb').read()
sites = {int(s['site'], 16): s for s in json.load(open(sys.argv[3]))}
assert len(orig) == len(new)

pe = struct.unpack_from('<I', orig, 0x3c)[0]
nsec = struct.unpack_from('<H', orig, pe + 6)[0]
optsz = struct.unpack_from('<H', orig, pe + 20)[0]
secs = [struct.unpack_from('<8sIIII', orig, pe + 24 + optsz + i * 40) for i in range(nsec)]


def to_va(fo):
    for _, vsz, va, rsz, raw in secs:
        if raw <= fo < raw + rsz:
            return BASE + va + fo - raw
    return None


def to_fo(va):
    for _, vsz, sva, rsz, raw in secs:
        if sva <= va - BASE < sva + rsz:
            return va - BASE - sva + raw
    raise ValueError(hex(va))


# contiguous runs of differing bytes
runs, i = [], 0
while i < len(orig):
    if orig[i] != new[i]:
        j = i
        while j < len(orig) and orig[j] != new[j]:
            j += 1
        runs.append([i, j])
        i = j
    else:
        i += 1

changes = []
for start, end in runs:
    va = to_va(start)
    site = next((s for s in sites if va is not None and s <= va < s + 6), None)
    if site is not None:  # widen to the whole 6-byte call/jmp instruction for readability
        start, end = to_fo(site), to_fo(site) + 6
        s = sites[site]
        note = f"LaserLock: {s['op']} [CallDLL] -> {s['op']} [{s['api']}]"
    elif va is None:
        note = 'PE header: clear bound-import directory'
    elif orig[start:end].lower().startswith(b'wh32') or new[start:end].startswith(b'hogs'):
        note = 'import wh32lib.dll (LaserLock) -> hogs.dll (fixes)'
    else:
        note = 'unexplained change'
    if changes and changes[-1]['offset'] == start:
        continue
    changes.append({'offset': start, 'va': f'{va:#x}' if va else None, 'old': orig[start:end].hex(),
                    'new': new[start:end].hex(), 'note': note})

assert not any(c['note'] == 'unexplained change' for c in changes), [c for c in changes if c['note'] == 'unexplained change']
out = {
    'game': 'Hogs of War v1.2 (Steam)',
    'file': 'warhogs_.exe',
    'input_sha256': hashlib.sha256(orig).hexdigest().upper(),
    'output_sha256': hashlib.sha256(new).hexdigest().upper(),
    'changes': changes,
}
json.dump(out, open(sys.argv[4], 'w'), indent=1)
print(f"{len(changes)} changes, input {out['input_sha256'][:16]}, output {out['output_sha256'][:16]}")
