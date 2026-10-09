# Build the player package: dist\HogsFix and dist\HogsFix.zip. Run it with PowerShell 7 (pwsh):
# Windows PowerShell 5.1 writes backslashes into the zip entry names.
# Needs Visual Studio with C++ and CMake. Configures the build folder if it is not there (cmake -A Win32),
# builds the Release hogs.dll and ddraw.dll, and downloads ogg-winmm (ayuanx/ogg-winmm v2025.01.16) into
# downloads\ once. The GitHub workflow .github\workflows\build.yml runs this same script.
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
cmake --build $build --config Release --target hogs hogsdraw
if ($LASTEXITCODE) { throw 'build failed' }

Remove-Item -LiteralPath (Join-Path $root 'dist') -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $files | Out-Null

Copy-Item (Join-Path $root 'patcher\patch.cmd'), (Join-Path $root 'patcher\uninstall.cmd'), (Join-Path $root 'patcher\patch.ps1'), (Join-Path $root 'patcher\warhogs_v12.json') $out
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $out 'LICENSE.txt')
Copy-Item (Join-Path $build 'src\hogs\Release\hogs.dll'), (Join-Path $build 'src\ddraw\Release\ddraw.dll'), (Join-Path $root 'src\hogs\hogs.ini') $files

# patch.ps1 recognises its own ddraw.dll by this text ($ddrawSig there), and hogs.ini is the players' file:
# it must hold the [Render] section that is appended to older ones, and nothing developer-only
if (-not [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $files 'ddraw.dll'))).Contains('hogsdraw.log')) { throw 'ddraw.dll does not contain "hogsdraw.log": patch.ps1 could not recognise it' }
$ini = Get-Content -LiteralPath (Join-Path $files 'hogs.ini') -Raw
if ($ini -notmatch '(?m)^\[Render\]') { throw 'hogs.ini has no [Render] section' }
if ($ini -match '(?im)^\s*\[Debug\]|FrameDump') { throw 'hogs.ini has developer-only settings ([Debug], FrameDump): not for players' }

$tmp = Join-Path $root 'dist\ogg'
Expand-Archive -LiteralPath $ogg -DestinationPath $tmp
Copy-Item (Join-Path $tmp 'winmm.dll'), (Join-Path $tmp 'winmm.ini') $files
Copy-Item (Join-Path $root 'third_party\ogg-winmm\LICENSE.txt') (Join-Path $files 'ogg-winmm-LICENSE.txt')
Copy-Item (Join-Path $root 'third_party\ogg-winmm\README.md') (Join-Path $files 'ogg-winmm-README.md')
Remove-Item -LiteralPath $tmp -Recurse

Compress-Archive -LiteralPath (Join-Path $root 'dist\HogsFix') -DestinationPath (Join-Path $root 'dist\HogsFix.zip')
Get-ChildItem $out -Recurse -File | ForEach-Object { '{0,8}  {1}' -f $_.Length, $_.FullName.Substring($out.Length + 1) }
