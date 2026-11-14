# Build the player package: dist\HogsFix and dist\HogsFix.zip. Run it with PowerShell 7 (pwsh):
# Windows PowerShell 5.1 writes backslashes into the zip entry names.
# Needs Visual Studio with C++ and CMake. Configures the build folder if it is not there (cmake -A Win32),
# builds the Release ddraw.dll (target hogsdraw: the fixes and the renderer), and downloads ogg-winmm
# (ayuanx/ogg-winmm v2025.01.16) into downloads\ once. The GitHub workflow .github\workflows\build.yml runs this same script.
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Run this with PowerShell 7 (pwsh): Windows PowerShell 5.1 writes backslashes into the zip entry names.' }
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$out = Join-Path $root 'dist\HogsFix'
$files = Join-Path $out 'files'

# the one input from outside the repo: check it before anything is built or wiped
$ogg = Join-Path $root 'downloads\ogg-winmm_binary.zip'
if (-not (Test-Path -LiteralPath $ogg)) {
    New-Item -ItemType Directory -Force (Split-Path $ogg) | Out-Null
    Invoke-WebRequest 'https://github.com/ayuanx/ogg-winmm/releases/download/v2025.01.16/ogg-winmm_binary.zip' -OutFile $ogg
}
if ((Get-FileHash -LiteralPath $ogg).Hash -ne 'F77B2C01E408BF5D1445C927F54D5932CD9101F46CD0CB4593BD20EF4B1AF04B') { throw "unexpected $ogg (delete it to download it again)" }

if (-not (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))) {
    cmake -S $root -B $build -A Win32
    if ($LASTEXITCODE) { throw 'cmake configure failed' }
}
cmake --build $build --config Release --target hogsdraw
if ($LASTEXITCODE) { throw 'build failed' }

Remove-Item -LiteralPath (Join-Path $root 'dist') -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $files | Out-Null

foreach ($f in 'patch.cmd', 'uninstall.cmd', 'patch.ps1', 'warhogs_v12.json', 'russian.cmd', 'russian-remove.cmd', 'russian.ps1') { Copy-Item (Join-Path $root "patcher\$f") $out }
# the player scripts run in Windows PowerShell 5.1, which reads a file without a BOM in the ANSI code page of the PC
foreach ($f in 'patch.ps1', 'russian.ps1') {
    if ([IO.File]::ReadAllBytes((Join-Path $out $f)) -gt 127) { throw "$f is not ASCII" }
}
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $out 'LICENSE.txt')
Copy-Item (Join-Path $build 'src\ddraw\Release\ddraw.dll'), (Join-Path $root 'src\hogs\hogs.ini') $files

# patch.ps1 recognises its own ddraw.dll by this text ($ddrawSig there), and hogs.ini is the players' file:
# patch.ps1 adds its sections and settings to older ones, so it must hold nothing developer-only
$ddText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $files 'ddraw.dll')))
if (-not $ddText.Contains('hogsdraw.log')) { throw 'ddraw.dll does not contain "hogsdraw.log": patch.ps1 could not recognise it' }
# the patched exe imports CallDLL from ddraw.dll and does not start without it (the name stands between NULs in the export table)
if (-not $ddText.Contains("`0CallDLL`0")) { throw 'ddraw.dll does not export CallDLL: the patched warhogs_.exe would not start' }
$ini = Get-Content -LiteralPath (Join-Path $files 'hogs.ini') -Raw
if ($ini -notmatch '(?m)^\[Render\]') { throw 'hogs.ini has no [Render] section' }
if ($ini -match '(?im)^\s*\[Debug\]|FrameDump') { throw 'hogs.ini has developer-only settings ([Debug], FrameDump): not for players' }

$tmp = Join-Path $root 'dist\ogg'
Expand-Archive -LiteralPath $ogg -DestinationPath $tmp
Copy-Item (Join-Path $tmp 'winmm.dll'), (Join-Path $tmp 'winmm.ini') $files
Copy-Item (Join-Path $root 'third_party\ogg-winmm\LICENSE.txt') (Join-Path $files 'ogg-winmm-LICENSE.txt')
Copy-Item (Join-Path $root 'third_party\ogg-winmm\README.md') (Join-Path $files 'ogg-winmm-README.md')
# ddraw.dll holds code ported from HogsFix (MIT): its license goes with every copy
Copy-Item (Join-Path $root 'third_party\HogsFix\LICENSE.txt') (Join-Path $files 'HogsFix-LICENSE.txt')
Remove-Item -LiteralPath $tmp -Recurse

Compress-Archive -LiteralPath (Join-Path $root 'dist\HogsFix') -DestinationPath (Join-Path $root 'dist\HogsFix.zip')
Get-ChildItem $out -Recurse -File | ForEach-Object { '{0,8}  {1}' -f $_.Length, $_.FullName.Substring($out.Length + 1) }
