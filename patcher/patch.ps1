# Hogs of War fix - patcher. Works on the player's own copy of the game; ships no game code.
#
#   patch.cmd             install or update the fix
#   patch.cmd -Restore    put the original files back
#   patch.cmd -GameDir "D:\Games\Hogs of War"   game folder (default: the folder above this one)
#   patch.cmd -DgVoodoo / -NoDgVoodoo           install dgVoodoo2 without asking / skip it
#
# Windows PowerShell 5.1 compatible (ships with Windows 10/11).
param([string]$GameDir, [switch]$Restore, [switch]$DgVoodoo, [switch]$NoDgVoodoo)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$files = Join-Path $here 'files'
$patch = Get-Content (Join-Path $here 'warhogs_v12.json') -Raw | ConvertFrom-Json

# Known third-party file: the 2014 ogg-winmm build that deadlocks the Windows 10/11 loader.
$badWinmm = '9ACA7D1B2649256970D38ECF346CB90CAE3545DD2DC73F63BC1030E1CCE4CEA4'
# dgVoodoo2 (not redistributable: downloaded from the author's GitHub release on request)
$dgvUrl = 'https://github.com/dege-diosg/dgVoodoo2/releases/download/v2.87.5/dgVoodoo2_87_5.zip'
$dgvSha = '5FFDE6927F7355CA3FDD5D785B581256A8E6539FA13E395A891ADE6BA1040850'
$dgvFiles = 'DDraw.dll', 'D3DImm.dll', 'dgVoodoo.conf', 'dgVoodooCpl.exe'
$dgvMarker = 'dgVoodoo.installed-by-hogsfix'

function Say($msg, $color = 'Gray') { Write-Host $msg -ForegroundColor $color }
function Sha($path) { (Get-FileHash $path -Algorithm SHA256).Hash }
function HexBytes($s) { [byte[]]($s -split '(..)' -ne '' | ForEach-Object { [Convert]::ToByte($_, 16) }) }

if (-not $GameDir) {
    $GameDir = Split-Path $here -Parent
    if (-not (Test-Path (Join-Path $GameDir $patch.file))) { $GameDir = $here }
}
$exe = Join-Path $GameDir $patch.file
if (-not (Test-Path $exe)) { throw "$($patch.file) not found in '$GameDir'. Put this folder inside the game folder or pass -GameDir." }
Say "Game folder: $GameDir"

$exeOrig = "$exe.orig"
$winmm = Join-Path $GameDir 'winmm.dll'

if ($Restore) {
    if (Test-Path $exeOrig) { Move-Item $exeOrig $exe -Force; Say 'warhogs_.exe restored' 'Green' }
    if (Test-Path "$winmm.orig") {
        Move-Item "$winmm.orig" $winmm -Force
        Remove-Item (Join-Path $GameDir 'winmm.ini') -ErrorAction SilentlyContinue
        Say 'winmm.dll restored' 'Green'
    }
    foreach ($f in 'hogs.dll', 'hogs.ini', 'hogs.log') { Remove-Item (Join-Path $GameDir $f) -ErrorAction SilentlyContinue }
    if (Test-Path (Join-Path $GameDir $dgvMarker)) {
        foreach ($f in $dgvFiles + $dgvMarker) { Remove-Item (Join-Path $GameDir $f) -ErrorAction SilentlyContinue }
        Say 'dgVoodoo2 removed' 'Green'
    }
    Say 'Done.' 'Green'
    return
}

# 1. warhogs_.exe: remove LaserLock, load hogs.dll
$hash = Sha $exe
if ($hash -eq $patch.output_sha256) {
    Say 'warhogs_.exe is already patched'
} elseif ($hash -ne $patch.input_sha256) {
    throw "Unknown warhogs_.exe (SHA-256 $hash). Supported: $($patch.game), SHA-256 $($patch.input_sha256)."
} else {
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
    if ((Sha $tmp) -ne $patch.output_sha256) { Remove-Item $tmp; throw 'Patched file has a wrong checksum; nothing was changed.' }
    if (-not (Test-Path $exeOrig)) { Copy-Item $exe $exeOrig }
    Move-Item $tmp $exe -Force
    Say "warhogs_.exe patched: $($patch.changes.Count) changes (original kept as warhogs_.exe.orig)" 'Green'
}

# 2. hogs.dll: the runtime fixes; keep the player's hogs.ini if there is one
Copy-Item (Join-Path $files 'hogs.dll') $GameDir -Force
if (-not (Test-Path (Join-Path $GameDir 'hogs.ini'))) { Copy-Item (Join-Path $files 'hogs.ini') $GameDir }
Say 'hogs.dll installed (settings: hogs.ini)' 'Green'

# 3. CD music: replace the ogg-winmm build that hangs the game on start
if ((Test-Path $winmm) -and (Sha $winmm) -eq $badWinmm) {
    if (Test-Path (Join-Path $files 'winmm.dll')) {
        Move-Item $winmm "$winmm.orig" -Force
        Copy-Item (Join-Path $files 'winmm.dll'), (Join-Path $files 'winmm.ini') $GameDir -Force
        Say 'winmm.dll replaced with a working ogg-winmm build (old one kept as winmm.dll.orig)' 'Green'
    } else {
        Move-Item $winmm "$winmm.orig" -Force
        Say 'winmm.dll that hangs the game was moved to winmm.dll.orig: no CD music until a working ogg-winmm is installed' 'Yellow'
    }
}

# 4. dgVoodoo2: runs the game's DirectDraw/Direct3D on Direct3D 11. Needed until the new
#    renderer is ready: the game's windowed mode expects a 16-bit desktop and divides by zero
#    on native DirectDraw (idiv [ecx+0x44C] at 0x44E1C5).
if (-not (Test-Path (Join-Path $GameDir 'DDraw.dll')) -and -not $NoDgVoodoo) {
    $answer = if ($DgVoodoo) { 'y' } else { Read-Host 'Download dgVoodoo2 2.87.5 (9 MB) from github.com/dege-diosg/dgVoodoo2? It is needed for the game to render. [Y/n]' }
    if ($answer -notmatch '^[nN]') {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $zip = Join-Path $env:TEMP 'dgVoodoo2_87_5.zip'
        $tmp = Join-Path $env:TEMP 'dgVoodoo2_87_5'
        Invoke-WebRequest $dgvUrl -OutFile $zip -UseBasicParsing
        if ((Sha $zip) -ne $dgvSha) { Remove-Item $zip; throw 'dgVoodoo2 download has a wrong checksum; not installed.' }
        Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
        Expand-Archive $zip $tmp
        Copy-Item (Join-Path $tmp 'MS\x86\DDraw.dll'), (Join-Path $tmp 'MS\x86\D3DImm.dll'), (Join-Path $tmp 'dgVoodooCpl.exe') $GameDir -Force
        # Windowed, centred, 2x internal resolution, 16-bit desktop for the game's windowed path,
        # follow the game's own windowed/fullscreen choice (hogs.ini), no watermark.
        $conf = Get-Content (Join-Path $tmp 'dgVoodoo.conf') -Raw
        $conf = $conf -replace '(?m)^FullScreenMode(\s+)= true', 'FullScreenMode$1= false' `
                      -replace '(?m)^ScalingMode(\s+)= unspecified', 'ScalingMode$1= stretched_ar' `
                      -replace '(?m)^CenterAppWindow(\s+)= false', 'CenterAppWindow$1= true' `
                      -replace '(?m)^DesktopBitDepth(\s+)= *\r?$', "DesktopBitDepth`$1= 16`r" `
                      -replace '(?m)^dgVoodooWatermark(\s+)= true', 'dgVoodooWatermark$1= false' `
                      -replace '(?m)^(\[DirectX\][\s\S]*?^)Resolution(\s+)= unforced', '${1}Resolution$2= 2x'
        Set-Content (Join-Path $GameDir 'dgVoodoo.conf') $conf -NoNewline -Encoding ascii
        Set-Content (Join-Path $GameDir $dgvMarker) 'dgVoodoo2 2.87.5 files here were installed by the Hogs of War fix (patch.cmd -Restore removes them).'
        Remove-Item $zip, $tmp -Recurse -Force
        Say 'dgVoodoo2 installed (settings: dgVoodoo.conf / dgVoodooCpl.exe)' 'Green'
    } else {
        Say 'dgVoodoo2 skipped: without it the game shows nothing or crashes in windowed mode.' 'Yellow'
    }
}

Say 'Done. Start the game as usual.' 'Green'
