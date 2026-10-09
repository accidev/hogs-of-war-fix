# Helpers for tools\test_patcher.ps1 (dot-sourced by it, not meant to be run on its own). PowerShell 7.
#
# Paths: repo = the parent of this folder. The tests work in <temp>\hogsfix-patcher-tests, which
# test_patcher.ps1 creates and deletes; nothing else is written and no game folder is used.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$T = Join-Path ([IO.Path]::GetTempPath()) 'hogsfix-patcher-tests'
$dgvDir = Join-Path $T '_dgv'                 # dgVoodoo files, extracted from downloads\dgVoodoo2_87_5.zip
$dist = Join-Path $repo 'dist\HogsFix'     # the package under test, built by tools\make_release.ps1
$ps51 = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$oldRev = '6943c20'                        # the last commit before ddraw.dll: its patcher\ is the previous release

# Files that are not in git (backup\ and downloads\ are ignored, dist\ is built): stop and say which one is missing.
function Need([string]$rel, [string]$what) {
    if (-not [IO.File]::Exists((Join-Path $repo $rel))) { throw "test_patcher: $rel is missing ($what)" }
}
foreach ($f in 'patch.cmd', 'patch.ps1', 'warhogs_v12.json', 'files\hogs.dll', 'files\ddraw.dll', 'files\hogs.ini', 'files\winmm.dll', 'files\winmm.ini') {
    Need "dist\HogsFix\$f" 'build the package first: tools\make_release.ps1'
}
Need 'backup\warhogs_.exe.orig' 'a copy of the original Steam v1.2 warhogs_.exe; the folder is not in git'
Need 'backup\winmm.dll.ogg-winmm-2014' 'a copy of the winmm.dll (ogg-winmm 2014) that comes with the game; the folder is not in git'
Need 'downloads\dgVoodoo2_87_5.zip' 'the dgVoodoo2 2.87.5 release zip; the folder is not in git'

function Sha([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }
$json = Get-Content -LiteralPath (Join-Path $dist 'warhogs_v12.json') -Raw | ConvertFrom-Json
# expected hashes: the package as it is now
$HX = @{ orig = $json.input_sha256; patched = $json.output_sha256 }
$HX.bad = Sha (Join-Path $repo 'backup\winmm.dll.ogg-winmm-2014')
$HX.hogs = Sha (Join-Path $dist 'files\hogs.dll')
$HX.dd = Sha (Join-Path $dist 'files\ddraw.dll')
$HX.winmm = Sha (Join-Path $dist 'files\winmm.dll')

if (-not ('Ini' -as [type])) {
    Add-Type -TypeDefinition 'using System.Runtime.InteropServices; public static class Ini { [DllImport("kernel32.dll", CharSet=CharSet.Ansi)] public static extern int GetPrivateProfileIntA(string s, string k, int d, string f); }'
}
function IniVals([string]$f) {
    'Windowed={0} Resizable={1} Scale={2} VSync={3}' -f [Ini]::GetPrivateProfileIntA('Display', 'Windowed', 99, $f), [Ini]::GetPrivateProfileIntA('Display', 'Resizable', 99, $f),
        [Ini]::GetPrivateProfileIntA('Render', 'Scale', 99, $f), [Ini]::GetPrivateProfileIntA('Render', 'VSync', 99, $f)
}

function CleanDir([string]$p) {   # only ever inside the test folder
    if (-not $p.StartsWith($T, [StringComparison]::OrdinalIgnoreCase)) { throw "refusing to delete outside the test folder: $p" }
    if ([IO.Directory]::Exists($p)) { [IO.Directory]::Delete($p, $true) }
}

# The dgVoodoo files the "hand-installed" scenarios start from, taken from the release zip (once).
function PrepareFixtures {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $z = [IO.Compression.ZipFile]::OpenRead((Join-Path $repo 'downloads\dgVoodoo2_87_5.zip'))
    try {
        foreach ($n in 'MS/x86/DDraw.dll', 'MS/x86/D3DImm.dll', 'dgVoodoo.conf', 'dgVoodooCpl.exe') {
            $e = $z.GetEntry($n)
            if (-not $e) { throw "test_patcher: downloads\dgVoodoo2_87_5.zip has no $n" }
            $to = Join-Path $dgvDir ($n -replace '/', '\')
            [void][IO.Directory]::CreateDirectory((Split-Path $to -Parent))
            [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $to)
        }
    } finally { $z.Dispose() }
    $HX.dgvDD = Sha (Join-Path $dgvDir 'MS\x86\DDraw.dll')
}

# A file as it was committed in $oldRev (raw bytes: PowerShell would re-encode text output).
function GitShow([string]$path) {
    $psi = New-Object Diagnostics.ProcessStartInfo 'git'
    foreach ($a in '-C', $repo, 'show', "${oldRev}:$path") { [void]$psi.ArgumentList.Add($a) }
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.UseShellExecute = $false
    try { $p = [Diagnostics.Process]::Start($psi) } catch { throw "test_patcher: scenario B needs git on PATH to read the previous release from history ($($_.Exception.Message))" }
    $ms = New-Object IO.MemoryStream
    $p.StandardOutput.BaseStream.CopyTo($ms)
    $err = $p.StandardError.ReadToEnd(); $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "test_patcher: git show ${oldRev}:$path failed ($err). Scenario B needs this repository's history." }
    , $ms.ToArray()
}

# The previous release package, built from git; its hogs.dll is a stand-in (the current one plus 3 bytes, so it differs).
function BuildPrev {
    $d = Join-Path $T '_prev\HogsFix'
    if (-not [IO.Directory]::Exists($d)) {
        [void][IO.Directory]::CreateDirectory((Join-Path $d 'files'))
        foreach ($f in 'patch.ps1', 'patch.cmd', 'warhogs_v12.json') { [IO.File]::WriteAllBytes((Join-Path $d $f), (GitShow "patcher/$f")) }
        [IO.File]::WriteAllBytes((Join-Path $d 'files\hogs.ini'), (GitShow 'src/hogs/hogs.ini'))
        [IO.File]::WriteAllBytes((Join-Path $d 'files\hogs.dll'), ([IO.File]::ReadAllBytes((Join-Path $dist 'files\hogs.dll')) + [byte[]](0x4F, 0x4C, 0x44)))
        foreach ($f in 'winmm.dll', 'winmm.ini', 'ogg-winmm-LICENSE.txt', 'ogg-winmm-README.md') {
            Copy-Item -LiteralPath (Join-Path $dist "files\$f") -Destination (Join-Path $d 'files')
        }
    }
    $d
}

function NewGame([string]$name, [string]$leaf = 'game', [switch]$NoWinmm, [switch]$Dgv) {
    CleanDir (Join-Path $T $name)
    $g = Join-Path (Join-Path $T $name) $leaf
    [void][IO.Directory]::CreateDirectory($g)
    Copy-Item -LiteralPath (Join-Path $repo 'backup\warhogs_.exe.orig') -Destination (Join-Path $g 'warhogs_.exe')
    if (-not $NoWinmm) { Copy-Item -LiteralPath (Join-Path $repo 'backup\winmm.dll.ogg-winmm-2014') -Destination (Join-Path $g 'winmm.dll') }
    if ($Dgv) {
        Copy-Item -LiteralPath (Join-Path $dgvDir 'MS\x86\DDraw.dll') -Destination (Join-Path $g 'DDraw.dll')
        Copy-Item -LiteralPath (Join-Path $dgvDir 'MS\x86\D3DImm.dll') -Destination (Join-Path $g 'D3DImm.dll')
        Copy-Item -LiteralPath (Join-Path $dgvDir 'dgVoodoo.conf') -Destination $g
        Copy-Item -LiteralPath (Join-Path $dgvDir 'dgVoodooCpl.exe') -Destination $g
    }
    $g
}
function AddPkg([string]$game, [string]$src = $dist, [string]$into = '') {
    $d = if ($into) { $into } else { Join-Path $game 'HogsFix' }
    CleanDir $d
    [void][IO.Directory]::CreateDirectory((Split-Path $d -Parent))
    Copy-Item -LiteralPath $src -Destination $d -Recurse
    $d
}
function Snap([string]$game) {   # every file under the game folder except the HogsFix package: name|size|sha256
    $root = (Resolve-Path -LiteralPath $game).ProviderPath.TrimEnd('\')
    Get-ChildItem -LiteralPath $root -Recurse -File -Force | ForEach-Object {
        $rel = $_.FullName.Substring($root.Length + 1)
        if ($rel -notmatch '^HogsFix(\\|$)') { '{0}|{1}|{2}' -f $rel.ToLowerInvariant(), $_.Length, (Sha $_.FullName) }
    } | Sort-Object
}
function SameSnap($a, $b) { (($a -join "`n") -ceq ($b -join "`n")) }
function DiffSnap($a, $b) { (Compare-Object $a $b | ForEach-Object { '{0} {1}' -f $_.SideIndicator, $_.InputObject }) -join '; ' }
function RunPatch([string]$pkg, [string[]]$extra = @(), [string]$cwd = '') {
    $here = Get-Location
    if ($cwd) { Set-Location -LiteralPath $cwd }
    $ErrorActionPreference = 'Continue'
    try {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $out = & $ps51 -NoProfile -ExecutionPolicy Bypass -File (Join-Path $pkg 'patch.ps1') @extra 2>&1 | Out-String
        $code = $LASTEXITCODE
    } finally { Set-Location -LiteralPath $here; $ErrorActionPreference = 'Stop' }
    [pscustomobject]@{ Code = $code; Out = $out; Ms = $sw.ElapsedMilliseconds }
}
function RunCmd([string]$wrapper) {   # run a .cmd wrapper with cmd.exe from its own folder: no quoting trouble with spaces in the path
    Push-Location -LiteralPath (Split-Path $wrapper -Parent)
    # as .\name: cmd would otherwise look a bare name up on PATH first (a run.cmd somewhere else ran instead)
    try { & cmd.exe /c ('.\' + (Split-Path $wrapper -Leaf)) 2>&1 | Out-String } finally { Pop-Location }
}
function Short([string]$s) { ($s -replace '\s+', ' ').Trim() }
$script:Results = New-Object System.Collections.ArrayList
function Check([string]$sc, [string]$what, [bool]$ok, [string]$ev = '') {
    [void]$script:Results.Add([pscustomobject]@{ Scenario = $sc; Check = $what; Pass = $ok; Evidence = $ev })
    '{0,-5}{1}  {2}{3}' -f $sc, $(if ($ok) { 'PASS' } else { 'FAIL' }), $what, $(if ($ev) { "   [$ev]" } else { '' })
}
function Has([string]$p) { [IO.File]::Exists($p) }
function FileSha([string]$g, [string]$name) { $p = Join-Path $g $name; if (Has $p) { Sha $p } else { '(absent)' } }
