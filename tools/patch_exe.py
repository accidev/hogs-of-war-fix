"""Apply compatibility patches to the LaserLock-free exe: patch_exe.py <in.exe> <out.exe>

Each patch checks the original bytes first, so it fails loudly on a different build.
"""
import struct, sys

PATCHES = [
    # EnumWindows callback (0x44CE80) disables/enables every window but the game's own.
    # Turn `jz skip` into `jmp skip`: the game never touches foreign windows, so a crash
    # or Alt+Tab can no longer leave the whole desktop disabled.
    (0x44CE9A, '74 12', 'eb 12', 'never EnableWindow() foreign windows'),
    # SystemParametersInfoA(SPI_SCREENSAVERRUNNING=0x61, ...) is a Win9x trick to block
    # Alt+Tab / Ctrl+Alt+Del. Replace the stdcall with `add esp,16` (pops its 4 args).
    (0x44CFC1, 'ff 15 e8 f5 54 00', '83 c4 10 90 90 90', 'drop SPI_SCREENSAVERRUNNING on start'),
    (0x47EA97, 'ff 15 e8 f5 54 00', '83 c4 10 90 90 90', 'drop SPI_SCREENSAVERRUNNING on exit'),
]

BASE = 0x400000
exe = bytearray(open(sys.argv[1], 'rb').read())
pe = struct.unpack_from('<I', exe, 0x3c)[0]
nsec = struct.unpack_from('<H', exe, pe + 6)[0]
optsz = struct.unpack_from('<H', exe, pe + 20)[0]
secs = [struct.unpack_from('<8sIIII', exe, pe + 24 + optsz + i * 40) for i in range(nsec)]


def off(va):
    rva = va - BASE
    for _, vsz, sva, rsz, raw in secs:
        if sva <= rva < sva + rsz:
            return rva - sva + raw
    raise ValueError(hex(va))


for va, old, new, why in PATCHES:
    old, new = bytes.fromhex(old), bytes.fromhex(new)
    o = off(va)
    assert exe[o:o + len(old)] == old, f'{va:#x}: expected {old.hex(" ")}, found {exe[o:o + len(old)].hex(" ")}'
    exe[o:o + len(new)] = new
    print(f'{va:#x}: {old.hex(" ")} -> {new.hex(" ")}  ({why})')
open(sys.argv[2], 'wb').write(exe)
