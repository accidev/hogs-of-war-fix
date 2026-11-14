# Helpers for tools\test_patcher.ps1 (dot-sourced by it, not meant to be run on its own). PowerShell 7.
# $repo is the parent of this folder, $T the scratch folder described in test_patcher.ps1.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$T = Join-Path ([IO.Path]::GetTempPath()) 'hogsfix-patcher-tests'
$dgvDir = Join-Path $T '_dgv'                 # dgVoodoo files, extracted from downloads\dgVoodoo2_87_5.zip
$dist = Join-Path $repo 'dist\HogsFix'     # the package under test, built by tools\make_release.ps1
$ps51 = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$prevRev = 'fdf30cb'                       # v1.1.1, the previous release: its exe imports hogs.dll
$dgvRev = '6943c20'                        # the last commit before the patcher shipped ddraw.dll: it installed dgVoodoo2

# Inputs that are not in git (listed in test_patcher.ps1): stop and name the missing one.
function Need([string]$rel, [string]$what) {
    if (-not [IO.File]::Exists((Join-Path $repo $rel))) { throw "test_patcher: $rel is missing ($what)" }
}
foreach ($f in 'patch.cmd', 'patch.ps1', 'russian.cmd', 'russian.ps1', 'warhogs_v12.json', 'files\ddraw.dll', 'files\hogs.ini', 'files\winmm.dll', 'files\winmm.ini') {
    Need "dist\HogsFix\$f" 'build the package first: tools\make_release.ps1'
}
Need 'backup\warhogs_.exe.orig' 'a copy of the original Steam v1.2 warhogs_.exe; the folder is not in git'
Need 'backup\winmm.dll.ogg-winmm-2014' 'a copy of the winmm.dll (ogg-winmm 2014) that comes with the game; the folder is not in git'
Need 'downloads\dgVoodoo2_87_5.zip' 'the dgVoodoo2 2.87.5 release zip; the folder is not in git'

function Sha([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }
$json = Get-Content -LiteralPath (Join-Path $dist 'warhogs_v12.json') -Raw | ConvertFrom-Json
# expected hashes: the package as it is now; older = the exe v1.0-v1.1 made (it imports hogs.dll)
$HX = @{ orig = $json.input_sha256; patched = $json.output_sha256; older = $json.older_outputs[0].sha256 }
$HX.bad = Sha (Join-Path $repo 'backup\winmm.dll.ogg-winmm-2014')
$HX.dd = Sha (Join-Path $dist 'files\ddraw.dll')
$HX.winmm = Sha (Join-Path $dist 'files\winmm.dll')

# The Windows ini API the game reads hogs.ini with (GetPrivateProfileStringA): the value of a key, or, with an empty key,
# the names of the section's keys, or, with an empty section too, the names of all sections (first occurrence of each).
# (Empty stands for the null the API wants: PowerShell turns a $null argument into an empty string.)
if (-not ('Ini' -as [type])) {
    Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class Ini {
    [DllImport("kernel32.dll", CharSet=CharSet.Ansi)] static extern int GetPrivateProfileStringA(string s, string k, string d, byte[] r, int n, string f);
    public static string[] Get(string s, string k, string d, string f) {
        if (s == "") s = null;
        if (k == "") k = null;
        byte[] b = new byte[65536];
        int n = GetPrivateProfileStringA(s, k, d, b, b.Length, f);
        string r = Encoding.GetEncoding(28591).GetString(b, 0, n);
        return (s == null || k == null) ? r.Split(new[] { '\0' }, StringSplitOptions.RemoveEmptyEntries) : new[] { r };
    }
}
'@
}
$none = '<<none>>'   # what IniGet returns for a key the API does not find
function IniGet([string]$f, $sec, $key) { [Ini]::Get($sec, $key, $none, $f)[0] }
function IniList([string]$f, $sec = $null) { @([Ini]::Get($sec, $null, '', $f)) }   # section names, or the key names of one section

# What patch.ps1 must have done to a player's hogs.ini, worked out from the template text and from what the API says about
# the files, not with patch.ps1's own parsing, so it holds for whatever sections and keys the template has. Returns the
# list of problems, empty when all is right.
#   $beforeFile / $afterFile   the player's file before and after the run (the before copy lives outside the game folder)
#   $out                       what the run printed
#   -Append                    the file has no section of the template, so the old bytes must be an exact prefix
function IniVerdict([string]$beforeFile, [string]$afterFile, [string]$out, [switch]$Append) {
    $tplFile = Join-Path $dist 'files\hogs.ini'
    $lat = [Text.Encoding]::Latin1
    $eol = '\r\n|\r|\n'
    $bb = [IO.File]::ReadAllBytes($beforeFile); $ab = [IO.File]::ReadAllBytes($afterFile)
    $bt = $lat.GetString($bb); $at = $lat.GetString($ab)
    $bn = $bt -replace $eol, "`n"; $an = $at -replace $eol, "`n"; $tn = $lat.GetString([IO.File]::ReadAllBytes($tplFile)) -replace $eol, "`n"
    $o = Short $out
    $problems = New-Object Collections.Generic.List[string]
    $isKey = '^[ \t]*[^;\s=\[][^=\n]*='     # a line that sets a key (not a comment, not a header)
    $isHdr = '^[ \t]*\[([^\]\n]*)'
    $hdrs = { param($n) @($n -split "`n" | ForEach-Object { if ($_ -match $isHdr) { $Matches[1].Trim(" `t").ToLowerInvariant() } }) }
    # each template key line with the comment lines right above it, by section
    $snip = @{}; $sec = ''; $above = @()
    foreach ($l in $tn -split "`n") {
        if ($l -match $isHdr) { $sec = $Matches[1].Trim(" `t").ToLowerInvariant(); $above = @() }
        elseif ($l -match '^[ \t]*;') { $above += $l }
        elseif ($l -match '^[ \t]*([^=\s]+)[ \t]*=') { $snip["$sec|$($Matches[1].ToLowerInvariant())"] = (@($above) + $l.TrimEnd()) -join "`n"; $above = @() }
        else { $above = @() }
    }
    $had = & $hdrs $bn
    $missing = 0
    foreach ($s in IniList $tplFile) {
        foreach ($k in IniList $tplFile $s) {
            $was = IniGet $beforeFile $s $k
            if ($was -ceq $none) {
                $missing++
                $want = IniGet $tplFile $s $k
                $sn = $snip["$($s.ToLowerInvariant())|$($k.ToLowerInvariant())"]
                if (-not $sn) { $problems.Add("[$s] ${k}: the test did not find the key line in the template") }
                elseif (-not $an.Contains($sn)) { $problems.Add("[$s] ${k}: its comment lines and line are not together in the file") }
                # the message names the section when the file lacked it, else the key
                $named = if ($had -contains $s.ToLowerInvariant()) { $k } else { "[$s]" }
                if (-not $o.Contains($named)) { $problems.Add("message does not name $named") }
            } else { $want = $was }
            $got = IniGet $afterFile $s $k
            if ($got -cne $want) { $problems.Add("[$s] $k is '$got', want '$want' ($(if ($was -ceq $none) { 'template default' } else { 'the player''s value' }))") }
        }
        $n0 = @($had | Where-Object { $_ -eq $s.ToLowerInvariant() }).Count
        $n1 = @(& $hdrs $an | Where-Object { $_ -eq $s.ToLowerInvariant() }).Count
        if ($n1 -ne [Math]::Max($n0, 1)) { $problems.Add("[$s] has $n1 headers, was $n0") }
    }
    # a key line was added for every key the API did not find, and not one more
    $kb = [regex]::Matches($bn, $isKey, 'Multiline').Count; $ka = [regex]::Matches($an, $isKey, 'Multiline').Count
    if ($ka - $kb -ne $missing) { $problems.Add("$($ka - $kb) key lines added, $missing keys were missing") }
    # message: exactly one, only when something was added
    $msgs = [regex]::Matches($o, 'hogs\.ini: added').Count
    if ($msgs -ne [int]($missing -gt 0)) { $problems.Add("$msgs 'added' messages, $missing keys were missing") }
    if ($missing -eq 0 -and -not [Linq.Enumerable]::SequenceEqual($ab, $bb)) { $problems.Add('nothing was missing but the file changed') }
    # every old line is still there, in the same order, byte for byte (the last line may have got its line end)
    $lines = { param($t) @([regex]::Matches($t, '[^\r\n]*(?:\r\n|\r|\n)|[^\r\n]+$') | ForEach-Object Value) }
    $A = & $lines $at; $j = 0
    foreach ($l in (& $lines $bt)) {
        $last = $l -notmatch '[\r\n]$'
        while ($j -lt $A.Count -and -not $(if ($last) { $A[$j].StartsWith($l, [StringComparison]::Ordinal) } else { $A[$j] -ceq $l })) { $j++ }
        if ($j -ge $A.Count) { $problems.Add("old line lost or moved: '$($l.TrimEnd())'"); break }
        $j++
    }
    # the blank line that separated a section from the next one is still there
    $blankHdr = '\n[ \t]*\n(?=[ \t]*\[)'
    if ([regex]::Matches($an, $blankHdr).Count -lt [regex]::Matches($bn, $blankHdr).Count) { $problems.Add('a blank line before a section header is gone') }
    if ($Append -and -not ($ab.Length -ge $bb.Length -and [Convert]::ToBase64String($ab, 0, $bb.Length) -ceq [Convert]::ToBase64String($bb))) { $problems.Add('old bytes are not an exact prefix') }
    , $problems.ToArray()
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

# A file as it was committed in $rev (raw bytes: PowerShell would re-encode text output).
function GitShow([string]$rev, [string]$path) {
    $psi = New-Object Diagnostics.ProcessStartInfo 'git'
    foreach ($a in '-C', $repo, 'show', "${rev}:$path") { [void]$psi.ArgumentList.Add($a) }
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.UseShellExecute = $false
    try { $p = [Diagnostics.Process]::Start($psi) } catch { throw "test_patcher: scenario B needs git on PATH to read the previous release from history ($($_.Exception.Message))" }
    $ms = New-Object IO.MemoryStream
    $p.StandardOutput.BaseStream.CopyTo($ms)
    $err = $p.StandardError.ReadToEnd(); $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "test_patcher: git show ${rev}:$path failed ($err). Scenario B needs this repository's history." }
    , $ms.ToArray()
}

# An older package, built from git as of $rev. Its DLLs are stand-ins: hogs.dll is any bytes, ddraw.dll the current one
# plus 3 bytes (so it differs, and both patchers still take it for theirs).
function BuildPrev([string]$rev = $prevRev) {
    $d = Join-Path $T "_prev-$rev\HogsFix"
    if (-not [IO.Directory]::Exists($d)) {
        [void][IO.Directory]::CreateDirectory((Join-Path $d 'files'))
        foreach ($f in 'patch.ps1', 'patch.cmd', 'warhogs_v12.json') { [IO.File]::WriteAllBytes((Join-Path $d $f), (GitShow $rev "patcher/$f")) }
        [IO.File]::WriteAllBytes((Join-Path $d 'files\hogs.ini'), (GitShow $rev 'src/hogs/hogs.ini'))
        [IO.File]::WriteAllBytes((Join-Path $d 'files\hogs.dll'), [Text.Encoding]::ASCII.GetBytes("stand-in for the hogs.dll of $rev"))
        [IO.File]::WriteAllBytes((Join-Path $d 'files\ddraw.dll'), ([IO.File]::ReadAllBytes((Join-Path $dist 'files\ddraw.dll')) + [byte[]](0x4F, 0x4C, 0x44)))
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
function RunPatch([string]$pkg, [string[]]$extra = @(), [string]$cwd = '', [string]$script = 'patch.ps1') {
    $here = Get-Location
    if ($cwd) { Set-Location -LiteralPath $cwd }
    $ErrorActionPreference = 'Continue'
    try {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        # the empty input stands for "no keyboard": patch.ps1 asks its question (the Russian translation) only of a person
        $out = '' | & $ps51 -NoProfile -ExecutionPolicy Bypass -File (Join-Path $pkg $script) @extra 2>&1 | Out-String
        $code = $LASTEXITCODE
    } finally { Set-Location -LiteralPath $here; $ErrorActionPreference = 'Stop' }
    [pscustomobject]@{ Code = $code; Out = $out; Ms = $sw.ElapsedMilliseconds }
}
# RunPatch while $file is held open: with -Share Read the patcher can still read and hash it, but not replace or delete
# it, so the run fails exactly at the step that moves it (an interrupted run).
function RunLocked([string]$file, [string]$pkg, [string[]]$extra = @(), [string]$share = 'Read') {
    $lock = [IO.File]::Open($file, 'Open', 'Read', $share)
    try { RunPatch $pkg $extra } finally { $lock.Dispose() }
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
