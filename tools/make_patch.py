"""Build the distributable patch description:
make_patch.py <original.exe> <patched.exe> <ll_sites.json> <out.json> [<exe without LaserLock> ...]

The JSON holds only offsets and the few bytes around each change (old and new), plus the
SHA-256 of the input and output files, so the patcher can verify both ends. No game code.

older_outputs: the exe that v1.0-v1.1 of the fix made, with the changes that turn it into the
new output, so the patcher updates such an exe in place (also when warhogs_.exe.orig is gone).
Those versions named the descriptor hogs.dll, which then held the fixes; nothing else differs.

other_inputs: exes of the same build that came without LaserLock (disc versions, named warhogs.exe),
each with the changes that turn it into the same output. Their calls are direct already; they differ
in where the imports are stored and in how the disc check is switched off.
"""
import hashlib, json, struct, sys

BASE = 0x400000
IMPORT_NOTE = 'import wh32lib.dll (LaserLock) -> ddraw.dll (renderer and fixes)'
# SHA-256 of the exe v1.0-v1.1 made: rebuilding it from the new exe must give exactly this
V11_SHA256 = 'CF03028F3D5A023281667D77AF26DAC3A92D3C5F13A4831385F15F4D64C222D6'
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


def sha(b):
    return hashlib.sha256(b).hexdigest().upper()


def section(fo):
    for name, vsz, va, rsz, raw in secs:
        if raw <= fo < raw + rsz:
            return name.rstrip(bytes(1)).decode()
    return None


def diff_runs(a, b):
    runs, i = [], 0
    while i < len(a):
        if a[i] != b[i]:
            j = i
            while j < len(a) and a[j] != b[j]:
                j += 1
            runs.append([i, j])
            i = j
        else:
            i += 1
    return runs


changes = []
for start, end in diff_runs(orig, new):
    va = to_va(start)
    site = next((s for s in sites if va is not None and s <= va < s + 6), None)
    if site is not None:  # widen to the whole 6-byte call/jmp instruction for readability
        start, end = to_fo(site), to_fo(site) + 6
        s = sites[site]
        note = f"LaserLock: {s['op']} [CallDLL] -> {s['op']} [{s['api']}]"
    elif va is None:
        note = 'PE header: clear bound-import directory'
    elif orig[start:end].lower().startswith(b'wh32') and new[start:end].startswith(b'ddraw.dll'):
        note = IMPORT_NOTE
    else:
        note = 'unexplained change'
    if changes and changes[-1]['offset'] == start:
        continue
    changes.append({'offset': start, 'va': f'{va:#x}' if va else None, 'old': orig[start:end].hex(),
                    'new': new[start:end].hex(), 'note': note})

assert not any(c['note'] == 'unexplained change' for c in changes), [c for c in changes if c['note'] == 'unexplained change']

imp = next(c for c in changes if c['note'] == IMPORT_NOTE)
was = b'hogs.dll'.ljust(len(imp['new']) // 2, b'\0')
older = bytearray(new)
older[imp['offset']:imp['offset'] + len(was)] = was
assert sha(older) == V11_SHA256, sha(older)

# a run anywhere else (game data, resources) is a KeyError: that exe is another build
OTHER_NOTES = {None: 'PE header: imports and sections as in the patched exe',
               '.text': 'disc check and start-up as in the patched exe',
               '.idata': 'import CallDLL from ddraw.dll (renderer and fixes)'}
others = []
for path in sys.argv[5:]:
    src = open(path, 'rb').read()
    assert len(src) == len(new), path
    others.append({
        'game': 'Hogs of War v1.2 without LaserLock (warhogs.exe)',
        'sha256': sha(src),
        'changes': [{'offset': s, 'va': f'{to_va(s):#x}' if to_va(s) else None, 'old': src[s:e].hex(),
                     'new': new[s:e].hex(), 'note': OTHER_NOTES[section(s)]} for s, e in diff_runs(src, new)],
    })

out = {
    'game': 'Hogs of War v1.2 (Steam)',
    'file': 'warhogs_.exe',
    'input_sha256': sha(orig),
    'output_sha256': sha(new),
    'older_outputs': [{
        'sha256': sha(older),
        'note': 'v1.0-v1.1 of the fix (the exe imports hogs.dll)',
        'changes': [{'offset': imp['offset'], 'va': imp['va'], 'old': was.hex(), 'new': imp['new'],
                     'note': 'import hogs.dll (fixes of v1.0-v1.1) -> ddraw.dll (renderer and fixes)'}],
    }],
    'changes': changes,
    'other_inputs': others,
}
json.dump(out, open(sys.argv[4], 'w'), indent=1)
print(f"{len(changes)} changes, input {out['input_sha256'][:16]}, output {out['output_sha256'][:16]}, "
      f"older outputs {', '.join(o['sha256'][:16] for o in out['older_outputs'])}, "
      f"other inputs {', '.join('%s (%d changes)' % (o['sha256'][:16], len(o['changes'])) for o in others) or 'none'}")
