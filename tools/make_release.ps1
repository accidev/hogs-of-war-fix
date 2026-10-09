# Assemble the player package: dist\HogsFix (and dist\HogsFix.zip).
# Needs: a Release build of hogs.dll (cmake --build build --config Release) and
# downloads\ogg-winmm_binary.zip (ayuanx/ogg-winmm v2025.01.16).
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'dist\HogsFix'
$files = Join-Path $out 'files'

cmake --build (Join-Path $root 'build') --config Release | Out-Null
if ($LASTEXITCODE) { throw 'build failed' }

Remove-Item (Join-Path $root 'dist') -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $files | Out-Null

Copy-Item (Join-Path $root 'patcher\patch.cmd'), (Join-Path $root 'patcher\patch.ps1'), (Join-Path $root 'patcher\warhogs_v12.json') $out
Copy-Item (Join-Path $root 'build\src\hogs\Release\hogs.dll'), (Join-Path $root 'src\hogs\hogs.ini') $files

$ogg = Join-Path $root 'downloads\ogg-winmm_binary.zip'
if ((Get-FileHash $ogg).Hash -ne 'F77B2C01E408BF5D1445C927F54D5932CD9101F46CD0CB4593BD20EF4B1AF04B') { throw "unexpected $ogg" }
$tmp = Join-Path $root 'dist\ogg'
Expand-Archive $ogg $tmp
Copy-Item (Join-Path $tmp 'winmm.dll'), (Join-Path $tmp 'winmm.ini') $files
Copy-Item (Join-Path $root 'third_party\ogg-winmm\LICENSE.txt') (Join-Path $files 'ogg-winmm-LICENSE.txt')
Copy-Item (Join-Path $root 'third_party\ogg-winmm\README.md') (Join-Path $files 'ogg-winmm-README.md')
Remove-Item $tmp -Recurse

Compress-Archive (Join-Path $root 'dist\HogsFix') (Join-Path $root 'dist\HogsFix.zip')
Get-ChildItem $out -Recurse -File | ForEach-Object { '{0,8}  {1}' -f $_.Length, $_.FullName.Substring($out.Length + 1) }
