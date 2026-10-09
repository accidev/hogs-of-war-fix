# Hogs of War fix - patcher. Works on the player's own copy of the game; ships no game code.
#
#   patch.cmd             install or update the fix
#   patch.cmd -Restore    put the original files back
#   patch.cmd -GameDir "D:\Games\Hogs of War"   game folder (default: the folder above this one)
#
# Windows PowerShell 5.1 compatible (ships with Windows 10/11). Keep this file ASCII.
# CmdletBinding makes an unknown switch (e.g. the old -NoDgVoodoo) an error instead of silently ignoring it.
[CmdletBinding()] param([string]$GameDir, [switch]$Restore)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$files = Join-Path $here 'files'
$patch = Get-Content -LiteralPath (Join-Path $here 'warhogs_v12.json') -Raw | ConvertFrom-Json

# Known third-party file: the 2014 ogg-winmm build that deadlocks the Windows 10/11 loader.
$badWinmm = '9ACA7D1B2649256970D38ECF346CB90CAE3545DD2DC73F63BC1030E1CCE4CEA4'
# dgVoodoo2 was the graphics layer before ddraw.dll. Its files were installed either by an older
# version of this patcher (then the marker file exists) or by the player. DDraw.dll is the same
# path as our ddraw.dll (file names are case-insensitive).
$dgvSiblings = 'D3DImm.dll', 'dgVoodoo.conf', 'dgVoodooCpl.exe'
$dgvFiles = @('DDraw.dll') + $dgvSiblings
$dgvMarker = 'dgVoodoo.installed-by-hogsfix'
# Our ddraw.dll is recognised by this text inside it (its log file name), whatever its version.
$ddrawSig = 'hogsdraw.log'

function Say($msg, $color = 'Gray') { Write-Host $msg -ForegroundColor $color }
function Sha($path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
function HexBytes($s) { [byte[]]($s -split '(..)' -ne '' | ForEach-Object { [Convert]::ToByte($_, 16) }) }
function Has($path) { Test-Path -LiteralPath $path }
function Drop($path) { if (Has $path) { Remove-Item -LiteralPath $path -Force } }
function OwnDdraw($path) { (Has $path) -and [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($path)).Contains($ddrawSig) }

if (-not $GameDir) {
    $GameDir = Split-Path $here -Parent
    if (-not (Has (Join-Path $GameDir $patch.file))) { $GameDir = $here }
}
# absolute: cmdlets and [IO.File] resolve a relative path against different folders
$GameDir = (Resolve-Path -LiteralPath $GameDir).ProviderPath
$exe = Join-Path $GameDir $patch.file
if (-not (Has $exe)) { throw "$($patch.file) not found in '$GameDir'. Put this folder inside the game folder or pass -GameDir." }
Say "Game folder: $GameDir"
# a running game holds warhogs_.exe, hogs.dll, ddraw.dll and winmm.dll open
if (Get-Process -Name warhogs_ -ErrorAction SilentlyContinue) { throw 'Hogs of War is running. Close it and run this again.' }

$exeOrig = "$exe.orig"
$winmm = Join-Path $GameDir 'winmm.dll'
$dd = Join-Path $GameDir 'ddraw.dll'
$marker = Join-Path $GameDir $dgvMarker

if ($Restore) {
    $origOk = (Has $exeOrig) -and (Sha $exeOrig) -eq $patch.input_sha256
    $hash = Sha $exe
    if ($origOk) {
        Move-Item -LiteralPath $exeOrig -Destination $exe -Force
        Say 'warhogs_.exe restored' 'Green'
    } elseif ($hash -eq $patch.output_sha256) {
        # the patched exe needs hogs.dll: remove nothing while there is no original to go back to
        throw 'warhogs_.exe.orig is missing or is not the original. In Steam: game > Properties > Installed Files > Verify integrity of game files, then run -Restore again.'
    } elseif ($hash -eq $patch.input_sha256) {
        Drop $exeOrig
    } else {
        Say 'warhogs_.exe is not a version this fix knows: left as it is' 'Yellow'
    }
    if (Has "$winmm.orig") {
        Move-Item -LiteralPath "$winmm.orig" -Destination $winmm -Force
        Drop (Join-Path $GameDir 'winmm.ini')
        Say 'winmm.dll restored' 'Green'
    }
    foreach ($f in 'hogs.dll', 'hogs.ini', 'hogs.log', 'hogsdraw.log') { Drop (Join-Path $GameDir $f) }
    if (Has $marker) {
        # dgVoodoo2 from an older version of this patcher (its DDraw.dll may already be ours)
        foreach ($f in $dgvFiles + $dgvMarker) { Drop (Join-Path $GameDir $f) }
        Say 'dgVoodoo2 installed by an older version removed' 'Green'
    } elseif (OwnDdraw $dd) {
        Drop $dd
        Say 'ddraw.dll removed' 'Green'
    }
    # a dgVoodoo2 the player had installed by hand was renamed to *.orig on install
    foreach ($f in $dgvFiles) {
        $p = Join-Path $GameDir $f
        if (Has "$p.orig") {
            if (Has $p) { Say "$f.orig left in place: there is another $f" 'Yellow' }
            else { Move-Item -LiteralPath "$p.orig" -Destination $p; Say "$f restored" 'Green' }
        }
    }
    Say 'Done.' 'Green'
    return
}

# Everything that can refuse is checked before the first change.
foreach ($f in 'hogs.dll', 'ddraw.dll', 'hogs.ini', 'winmm.dll', 'winmm.ini') {
    if (-not (Has (Join-Path $files $f))) { throw "files\$f is missing. Extract the whole zip, then run patch.cmd again." }
}
# A dgVoodoo2 the player installed by hand (no marker) is not ours to delete: it is kept as *.orig.
$keep = @()
if ((Has $dd) -and -not (OwnDdraw $dd) -and -not (Has $marker)) {
    $keep = @($dgvFiles | Where-Object { Has (Join-Path $GameDir $_) })
    foreach ($f in $keep) {
        if (Has (Join-Path $GameDir "$f.orig")) { throw "$f.orig is in the way. Move it away and run patch.cmd again; nothing was changed." }
    }
}

# 1. warhogs_.exe: remove LaserLock, load hogs.dll
$hash = Sha $exe
if ($hash -eq $patch.output_sha256) {
    Say 'warhogs_.exe is already patched'
} elseif ($hash -ne $patch.input_sha256) {
    throw "Unknown warhogs_.exe (SHA-256 $hash). Supported: $($patch.game), SHA-256 $($patch.input_sha256)."
} else {
    # keep the original; replace a missing or damaged copy (an interrupted earlier run)
    if (-not ((Has $exeOrig) -and (Sha $exeOrig) -eq $patch.input_sha256)) {
        Copy-Item -LiteralPath $exe -Destination $exeOrig -Force
        if ((Sha $exeOrig) -ne $patch.input_sha256) { throw 'Could not copy the original warhogs_.exe; nothing was changed.' }
    }
    $bytes = [IO.File]::ReadAllBytes($exe)
    foreach ($c in $patch.changes) {
        $old = HexBytes $c.old
        $new = HexBytes $c.new
        for ($i = 0; $i -lt $old.Length; $i++) {
            if ($bytes[$c.offset + $i] -ne $old[$i]) { throw "Unexpected bytes at offset $($c.offset) ($($c.note))." }
        }
        [Array]::Copy($new, 0, $bytes, $c.offset, $new.Length)
    }
    $tmp = "$exe.new"
    [IO.File]::WriteAllBytes($tmp, $bytes)
    if ((Sha $tmp) -ne $patch.output_sha256) { Drop $tmp; throw 'Patched file has a wrong checksum; nothing was changed.' }
    Move-Item -LiteralPath $tmp -Destination $exe -Force
    Say "warhogs_.exe patched: $($patch.changes.Count) changes (original kept as warhogs_.exe.orig)" 'Green'
}

# 2. hogs.dll: the runtime fixes. hogs.ini is the player's: keep it, and add the sections a newer version needs.
Copy-Item -LiteralPath (Join-Path $files 'hogs.dll') -Destination $GameDir -Force
$ini = Join-Path $GameDir 'hogs.ini'
if (-not (Has $ini)) {
    Copy-Item -LiteralPath (Join-Path $files 'hogs.ini') -Destination $ini
} else {
    $b = [IO.File]::ReadAllBytes($ini)
    if ($b.Length -ge 2 -and (($b[0] -eq 0xFF -and $b[1] -eq 0xFE) -or ($b[0] -eq 0xFE -and $b[1] -eq 0xFF))) {
        Say 'hogs.ini is saved as UTF-16: copy the [Render] section from files\hogs.ini into it by hand' 'Yellow'
    } elseif ([IO.File]::ReadAllText($ini) -notmatch '(?im)^[ \t]*\[Render\]') {
        # append only: the player's text, values and encoding stay exactly as they are
        $block = [regex]::Match([IO.File]::ReadAllText((Join-Path $files 'hogs.ini')), '(?ms)^\[Render\].*?(?=^\[|\z)').Value
        $nl = if ($b.Length -gt 0 -and $b[$b.Length - 1] -ne 10) { "`r`n" } else { '' }
        [IO.File]::AppendAllText($ini, $nl + "`r`n" + ($block -replace '\r?\n', "`r`n"), [Text.Encoding]::ASCII)
        Say 'hogs.ini: added the [Render] section (your settings are unchanged)' 'Green'
    }
}
Say 'hogs.dll installed (settings: hogs.ini)' 'Green'

# 3. CD music: replace the ogg-winmm build that hangs the game on start
if ((Has $winmm) -and (Sha $winmm) -eq $badWinmm) {
    Move-Item -LiteralPath $winmm -Destination "$winmm.orig" -Force
    Copy-Item -LiteralPath (Join-Path $files 'winmm.dll'), (Join-Path $files 'winmm.ini') -Destination $GameDir -Force
    Say 'winmm.dll replaced with a working ogg-winmm build (old one kept as winmm.dll.orig)' 'Green'
}

# 4. ddraw.dll: the built-in Direct3D 11 renderer (it replaces dgVoodoo2)
foreach ($f in $keep) { Move-Item -LiteralPath (Join-Path $GameDir $f) -Destination (Join-Path $GameDir "$f.orig") }
if ($keep) { Say "Found dgVoodoo2/ddraw files, renamed to *.orig (patch.cmd -Restore puts them back): $($keep -join ', ')" 'Yellow' }
Copy-Item -LiteralPath (Join-Path $files 'ddraw.dll') -Destination $dd -Force
if (Has $marker) {
    # installed by an older version of this patcher: DDraw.dll was just replaced, the rest is not needed
    foreach ($f in $dgvSiblings + $dgvMarker) { Drop (Join-Path $GameDir $f) }
    Say 'dgVoodoo2 installed by an older version removed' 'Green'
}
Say 'ddraw.dll installed (Direct3D 11 renderer, settings: hogs.ini [Render])' 'Green'

Say 'Done. Start the game as usual.' 'Green'
