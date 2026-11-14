# Regression test for the player patcher (patcher\patch.ps1, patch.cmd) against the package tools\make_release.ps1
# builds. Developer tool, not shipped.
#   pwsh tools\test_patcher.ps1              all scenarios
#   pwsh tools\test_patcher.ps1 -Which B     one scenario: A B C D E F G H I R X or Y
# Needs PowerShell 7 (pwsh), Windows PowerShell 5.1 (the patcher runs in it), git (scenarios B and I build older
# releases from history) and in the repo (backup\ and downloads\ are not in git):
#   dist\HogsFix                       run tools\make_release.ps1 first
#   backup\warhogs_.exe.orig           the original Steam v1.2 warhogs_.exe
#   backup\winmm.dll.ogg-winmm-2014    the winmm.dll that comes with the game
#   downloads\dgVoodoo2_87_5.zip       the dgVoodoo files of the scenarios
# Close Hogs of War first: the patcher refuses to run while a warhogs_ process exists.
# Every scenario works on fresh copies in <temp>\hogsfix-patcher-tests (created and deleted here); no game folder is
# touched. One line per check, then "--- N checks, M failed" (exit code 1 if M > 0).
#   A  patch.cmd, re-run, -Restore, uninstall.cmd     F  game folder with [ ] and spaces, relative -GameDir
#   B  update from v1.1.1 and from the dgVoodoo2      G  game running (a dummy warhogs_ process)
#      era, no .orig, -Restore old/new both ways      H  Steam "verify files", missing or damaged .orig
#   C  hand-installed dgVoodoo kept as *.orig         I  interrupted update and -Restore (locked files)
#   D  refuses when a *.orig is in the way            X  missing package file, unknown exe, old switches, patch.cmd
#   E  hogs.ini variants: what the update adds        Y  foreign ddraw.dll later, leftovers, logs, locked file
#   R  russian.ps1 with a stand-in zip: install, second run, -Remove, refused zips, patch.ps1 -Restore
[CmdletBinding()]
param([ValidateSet('All', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'R', 'X', 'Y')][string]$Which = 'All')
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'test_patcher: run it with PowerShell 7: pwsh tools\test_patcher.ps1' }
. (Join-Path $PSScriptRoot 'test_patcher_lib.ps1')

function P8([string]$h) { if ($h.Length -ge 8) { $h.Substring(0, 8) } else { $h } }

function InstalledChecks([string]$sc, [string]$g, [string]$tag) {
    Check $sc "$tag exe is the patched one" ((FileSha $g 'warhogs_.exe') -eq $HX.patched) (P8 (FileSha $g 'warhogs_.exe'))
    Check $sc "$tag warhogs_.exe.orig is the original" ((FileSha $g 'warhogs_.exe.orig') -eq $HX.orig) (P8 (FileSha $g 'warhogs_.exe.orig'))
    Check $sc "$tag no hogs.dll, ddraw.dll = package" ((-not (Has (Join-Path $g 'hogs.dll'))) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd))
    Check $sc "$tag winmm.dll = ayuanx, winmm.dll.orig = the 2014 one" (((FileSha $g 'winmm.dll') -eq $HX.winmm) -and ((FileSha $g 'winmm.dll.orig') -eq $HX.bad)) ''
    Check $sc "$tag winmm.ini present" (Has (Join-Path $g 'winmm.ini'))
}

# the end state of an update: new exe, hogs.dll gone, our ddraw.dll, no *.new left
function Updated([string]$g) {
    ((FileSha $g 'warhogs_.exe') -eq $HX.patched) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd) -and
        -not ((Has (Join-Path $g 'hogs.dll')) -or (Has (Join-Path $g 'warhogs_.exe.new')) -or (Has (Join-Path $g 'ddraw.dll.new')))
}

# A game folder as the previous release (v1.1.1, from git) leaves it: exe importing hogs.dll, hogs.dll, its ddraw.dll.
function OldInstall([string]$name) {
    $g = NewGame $name
    $r = RunPatch (BuildPrev) @('-GameDir', $g)
    if ($r.Code -ne 0) { throw "test_patcher: the previous release did not install in $name (exit $($r.Code)): $(Short $r.Out)" }
    $g
}

function ScenA {
    $g = NewGame 'A'; $pkg = AddPkg $g; $s0 = Snap $g
    Check 'A' 'package has no files\hogs.dll' (-not (Has (Join-Path $pkg 'files\hogs.dll')))
    $wrap = Join-Path $T 'A\run.cmd'
    [IO.File]::WriteAllText($wrap, "@echo off`r`ncall `"$pkg\patch.cmd`" < nul`r`n")
    $o = RunCmd $wrap
    Check 'A' 'run 1 through patch.cmd finished' ($o -match 'Done\. Start the game as usual\.') (Short $o).Substring(0, [Math]::Min(120, (Short $o).Length))
    InstalledChecks 'A' $g 'run 1:'
    Check 'A' 'run 1: hogs.ini = template (fresh copy)' ((FileSha $g 'hogs.ini') -eq (Sha (Join-Path $dist 'files\hogs.ini')))
    $s1 = Snap $g
    $r = RunPatch $pkg
    Check 'A' 'run 2 (re-run): exit 0, "already patched"' (($r.Code -eq 0) -and ($r.Out -match 'already patched')) "exit $($r.Code)"
    $s2 = Snap $g
    Check 'A' 'run 2: every file hash identical to after run 1' (SameSnap $s1 $s2) (DiffSnap $s1 $s2)
    $r = RunPatch $pkg @('-Restore')
    $s3 = Snap $g
    Check 'A' '-Restore: exit 0, tree identical to the untouched original (names, sizes, SHA-256)' (($r.Code -eq 0) -and (SameSnap $s0 $s3)) "exit $($r.Code) $(DiffSnap $s0 $s3)"
    $r = RunPatch $pkg @('-Restore')
    Check 'A' '-Restore again: exit 0, still identical' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"
    $r = RunPatch $pkg
    Check 'A' 'install after restore works and equals run 1' (($r.Code -eq 0) -and (SameSnap $s1 (Snap $g))) "exit $($r.Code)"
    $wrap = Join-Path $T 'A\run-uninstall.cmd'
    [IO.File]::WriteAllText($wrap, "@echo off`r`ncall `"$pkg\uninstall.cmd`" < nul`r`n")
    $o = RunCmd $wrap
    Check 'A' 'uninstall.cmd: Done, tree identical to the untouched original' (($o -match 'Done\.') -and (SameSnap $s0 (Snap $g))) (DiffSnap $s0 (Snap $g))
}

function ScenB {
    $g = NewGame 'B'; $s0 = Snap $g
    $r = RunPatch (BuildPrev) @('-GameDir', $g)
    $oldDd = Sha (Join-Path (BuildPrev) 'files\ddraw.dll')
    Check 'B' 'previous release (v1.1.1 patch.ps1 and JSON) installed: its exe (imports hogs.dll), hogs.dll, its ddraw.dll' (($r.Code -eq 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.older) -and (Has (Join-Path $g 'hogs.dll')) -and ((FileSha $g 'ddraw.dll') -eq $oldDd)) "exit $($r.Code) $(P8 (FileSha $g 'warhogs_.exe'))"
    # the player's file: made before [Render] existed (so without it), and two settings changed
    $ini = Join-Path $g 'hogs.ini'
    $iniText = [regex]::Replace([IO.File]::ReadAllText($ini), '(?ms)^\[Render\].*?(?=^\[|\z)', '')
    [IO.File]::WriteAllText($ini, $iniText.Replace('Windowed=1', 'Windowed=0').Replace('Resizable=1', 'Resizable=0'))
    $iniBefore = Join-Path $T 'B\hogs.ini.before'; Copy-Item -LiteralPath $ini -Destination $iniBefore
    $winmmBefore = FileSha $g 'winmm.dll'; $origBefore = FileSha $g 'warhogs_.exe.orig'
    $new = AddPkg $g
    $r = RunPatch $new
    Check 'B' 'update: exit 0, exe moved over, hogs.dll reported removed' (($r.Code -eq 0) -and ($r.Out -match 'warhogs_\.exe updated') -and ($r.Out -match 'hogs\.dll removed')) "exit $($r.Code)"
    Check 'B' 'update: exe has the new hash, hogs.dll gone, ddraw.dll = package, no *.new left' (Updated $g) (P8 (FileSha $g 'warhogs_.exe'))
    Check 'B' 'update: exe.orig and winmm.dll untouched' (((FileSha $g 'warhogs_.exe.orig') -eq $origBefore) -and ((FileSha $g 'winmm.dll') -eq $winmmBefore)) ''
    $v = IniVerdict $iniBefore $ini $r.Out
    Check 'B' 'update: hogs.ini keeps every old line, gets the template sections and settings it lacked, one message' (($r.Out -match 'hogs\.ini: added') -and -not $v) ($v -join '; ')
    Check 'B' "update: player's values kept (Windowed=0, Resizable=0)" (((IniGet $ini 'Display' 'Windowed') -eq '0') -and ((IniGet $ini 'Display' 'Resizable') -eq '0')) "$((Get-Item $iniBefore).Length) B -> $((Get-Item $ini).Length) B"
    $sUpd = Snap $g
    $r = RunPatch $new
    Check 'B' 'update again: idempotent (identical tree)' (($r.Code -eq 0) -and (SameSnap $sUpd (Snap $g))) "exit $($r.Code)"
    $r = RunPatch $new @('-Restore')
    Check 'B' '-Restore after update: tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"

    $g = OldInstall 'B2'; $pkg = AddPkg $g
    $r = RunPatch $pkg @('-Restore')
    Check 'B2' '-Restore on a v1.1.1 install: exit 0, tree identical to the original (hogs.dll removed too)' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"

    # the player deleted warhogs_.exe.orig: the update needs no original, -Restore does
    $g = OldInstall 'B3'; $pkg = AddPkg $g
    [IO.File]::Delete((Join-Path $g 'warhogs_.exe.orig')); $sNo = Snap $g
    $r = RunPatch $pkg @('-Restore')
    Check 'B3' 'v1.1.1 install without .orig: -Restore refuses with the Steam hint, removes nothing (hogs.dll stays)' (($r.Code -ne 0) -and ($r.Out -match 'Verify integrity') -and (SameSnap $sNo (Snap $g))) "exit $($r.Code) $(DiffSnap $sNo (Snap $g))"
    $r = RunPatch $pkg
    Check 'B3' 'v1.1.1 install without .orig: update moves the exe over directly (new hash, hogs.dll gone, ddraw.dll = package)' (($r.Code -eq 0) -and (Updated $g) -and -not (Has (Join-Path $g 'warhogs_.exe.orig'))) "exit $($r.Code)"
    $sUpd = Snap $g
    $r = RunPatch $pkg @('-Restore')
    Check 'B3' '... then -Restore refuses with the Steam hint, removes nothing (ddraw.dll stays)' (($r.Code -ne 0) -and ($r.Out -match 'Verify integrity') -and (SameSnap $sUpd (Snap $g))) "exit $($r.Code) $(DiffSnap $sUpd (Snap $g))"

    # the previous release's -Restore (its uninstall.cmd) on a new install
    $g = NewGame 'B4'; $pkg = AddPkg $g
    $r = RunPatch $pkg
    $r = RunPatch (BuildPrev) @('-Restore', '-GameDir', $g)
    Check 'B4' 'old -Restore on a new install with .orig: exit 0, tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
    $r = RunPatch $pkg
    [IO.File]::Delete((Join-Path $g 'warhogs_.exe.orig'))
    $r = RunPatch (BuildPrev) @('-Restore', '-GameDir', $g)
    # cannot be fixed: the old script does not know the new exe, leaves it, and still removes our ddraw.dll and hogs.ini
    Check 'B4' 'old -Restore on a new install without .orig: as expected, exe left patched, ddraw.dll removed (the game does not start)' (($r.Code -eq 0) -and ($r.Out -match 'not a version this fix knows') -and ((FileSha $g 'warhogs_.exe') -eq $HX.patched) -and -not (Has (Join-Path $g 'ddraw.dll'))) "exit $($r.Code)"
    $r = RunPatch $pkg
    Check 'B4' '... the new patch.cmd repairs it (exe already patched, ddraw.dll = package)' (($r.Code -eq 0) -and ($r.Out -match 'already patched') -and ((FileSha $g 'ddraw.dll') -eq $HX.dd)) "exit $($r.Code)"

    # an install by the patcher of the dgVoodoo2 era: its exe imports hogs.dll too, its DDraw.dll is dgVoodoo
    $g = NewGame 'B5'
    $old = AddPkg $g (BuildPrev $dgvRev)
    # the old script, except that its dgVoodoo "download" copies the zip we already have
    $ps = Join-Path $old 'patch.ps1'; $txt = [IO.File]::ReadAllText($ps)
    $line = 'Invoke-WebRequest $dgvUrl -OutFile $zip -UseBasicParsing'
    if (-not $txt.Contains($line)) { throw 'old script changed?' }
    $zipSrc = (Join-Path $repo 'downloads\dgVoodoo2_87_5.zip') -replace "'", "''"
    [IO.File]::WriteAllText($ps, $txt.Replace($line, "Copy-Item -LiteralPath '$zipSrc' -Destination `$zip"))
    $tmpd = Join-Path $T 'B5\tmp'; [void][IO.Directory]::CreateDirectory($tmpd)
    $oldTemp = $env:TEMP; $oldTmp = $env:TMP; $env:TEMP = $tmpd; $env:TMP = $tmpd
    try { $r = RunPatch $old @('-DgVoodoo') } finally { $env:TEMP = $oldTemp; $env:TMP = $oldTmp }
    CleanDir $tmpd
    Check 'B5' 'dgVoodoo2-era release installed: its exe (imports hogs.dll), hogs.dll, dgVoodoo 2.87.5 + marker' (($r.Code -eq 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.older) -and (Has (Join-Path $g 'hogs.dll')) -and ((FileSha $g 'DDraw.dll') -eq $HX.dgvDD) -and (Has (Join-Path $g 'dgVoodoo.installed-by-hogsfix'))) "exit $($r.Code)"
    # copy of this state for B6 (-Restore straight from the old install)
    $g2 = Join-Path $T 'B6\game'; CleanDir (Join-Path $T 'B6'); [void][IO.Directory]::CreateDirectory((Split-Path $g2 -Parent))
    Copy-Item -LiteralPath $g -Destination $g2 -Recurse
    $new = AddPkg $g
    $r = RunPatch $new
    Check 'B5' 'update: exit 0, exe moved over, old dgVoodoo reported removed' (($r.Code -eq 0) -and ($r.Out -match 'warhogs_\.exe updated') -and ($r.Out -match 'dgVoodoo2 installed by an older version removed')) "exit $($r.Code)"
    Check 'B5' 'update: dgVoodoo files and marker gone (D3DImm.dll, dgVoodoo.conf, dgVoodooCpl.exe, marker)' (-not ((Has (Join-Path $g 'D3DImm.dll')) -or (Has (Join-Path $g 'dgVoodoo.conf')) -or (Has (Join-Path $g 'dgVoodooCpl.exe')) -or (Has (Join-Path $g 'dgVoodoo.installed-by-hogsfix')))) ''
    InstalledChecks 'B5' $g 'update:'
    $r = RunPatch $new @('-Restore')
    Check 'B5' '-Restore after update: tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
    $new2 = AddPkg $g2 $dist (Join-Path $g2 'HogsFix')
    $r = RunPatch $new2 @('-Restore')
    Check 'B6' '-Restore on a dgVoodoo2-era install: exit 0, dgVoodoo reported removed' (($r.Code -eq 0) -and ($r.Out -match 'dgVoodoo2 installed by an older version removed')) "exit $($r.Code)"
    Check 'B6' '-Restore on a dgVoodoo2-era install: tree identical to the original' (SameSnap $s0 (Snap $g2)) (DiffSnap $s0 (Snap $g2))
}

function ScenC {
    $g = NewGame 'C' -Dgv; $pkg = AddPkg $g; $s0 = Snap $g
    $orig = @{}; foreach ($n in 'DDraw.dll', 'D3DImm.dll', 'dgVoodoo.conf', 'dgVoodooCpl.exe') { $orig[$n] = FileSha $g $n }
    $r = RunPatch $pkg
    Check 'C' 'install over a hand-installed dgVoodoo: exit 0, reports *.orig' (($r.Code -eq 0) -and ($r.Out -match 'renamed to \*\.orig')) "exit $($r.Code)"
    $same = $true; foreach ($n in $orig.Keys) { if ((FileSha $g "$n.orig") -ne $orig[$n]) { $same = $false } }
    Check 'C' 'all four dgVoodoo files kept as *.orig, byte-identical' $same ((($orig.Keys | Sort-Object) | ForEach-Object { "$_.orig=$(P8 (FileSha $g "$_.orig"))" }) -join ' ')
    Check 'C' 'ddraw.dll is ours; D3DImm.dll / dgVoodoo.conf / dgVoodooCpl.exe moved away' (((FileSha $g 'ddraw.dll') -eq $HX.dd) -and -not ((Has (Join-Path $g 'D3DImm.dll')) -or (Has (Join-Path $g 'dgVoodoo.conf')) -or (Has (Join-Path $g 'dgVoodooCpl.exe')))) ''
    InstalledChecks 'C' $g 'C:'
    $s1 = Snap $g
    $r = RunPatch $pkg
    Check 'C' 're-run: no extra backups, identical tree' (($r.Code -eq 0) -and (SameSnap $s1 (Snap $g)) -and ($r.Out -notmatch 'renamed')) "exit $($r.Code) $(DiffSnap $s1 (Snap $g))"
    $r = RunPatch $pkg @('-Restore')
    Check 'C' '-Restore: dgVoodoo back byte-identical, tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
    $g = NewGame 'C2' -Dgv; $pkg = AddPkg $g
    foreach ($n in 'D3DImm.dll', 'dgVoodoo.conf', 'dgVoodooCpl.exe') { [IO.File]::Delete((Join-Path $g $n)) }
    $s0 = Snap $g
    $r = RunPatch $pkg
    Check 'C2' 'lone foreign DDraw.dll: kept as DDraw.dll.orig, ours installed' (($r.Code -eq 0) -and ((FileSha $g 'DDraw.dll.orig') -eq $HX.dgvDD) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd)) "exit $($r.Code)"
    $r = RunPatch $pkg @('-Restore')
    Check 'C2' '-Restore: tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) (DiffSnap $s0 (Snap $g))
}

function ScenD {
    $g = NewGame 'D' -Dgv; $pkg = AddPkg $g
    [IO.File]::WriteAllText((Join-Path $g 'DDraw.dll.orig'), 'someone else''s backup')
    $s0 = Snap $g
    $r = RunPatch $pkg
    Check 'D' 'foreign ddraw.dll + existing DDraw.dll.orig: refuses (exit != 0, message)' (($r.Code -ne 0) -and ($r.Out -match 'DDraw\.dll\.orig is in the way')) "exit $($r.Code)"
    Check 'D' 'nothing changed (tree identical, exe still original, no warhogs_.exe.new)' ((SameSnap $s0 (Snap $g)) -and ((FileSha $g 'warhogs_.exe') -eq $HX.orig)) (DiffSnap $s0 (Snap $g))
    [IO.File]::Delete((Join-Path $g 'DDraw.dll.orig')); [IO.File]::WriteAllText((Join-Path $g 'dgVoodoo.conf.orig'), 'x')
    $s0 = Snap $g
    $r = RunPatch $pkg
    Check 'D2' 'collision on dgVoodoo.conf.orig also refuses before any change' (($r.Code -ne 0) -and ($r.Out -match 'dgVoodoo\.conf\.orig is in the way') -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"
}

# The player's hogs.ini before the update, in the ways such files differ. IniVerdict (in the lib) works out from the
# template what the patcher must do, so these stay valid when the template changes. Display, Windowed, Resizable and
# Render below are only fixture contents.
function ScenE {
    $lat = [Text.Encoding]::Latin1
    $tplFile = Join-Path $dist 'files\hogs.ini'
    $tpl = $lat.GetString([IO.File]::ReadAllBytes($tplFile)) -replace '\r?\n', "`r`n"
    $s1 = (IniList $tplFile)[0]; $k1 = (IniList $tplFile $s1)[0]     # the template's first section, and its first key
    $keyLine = '(?m)^[ \t]*[^;\s=\[][^=\r\n]*=[^\r\n]*\r\n'
    $lastKey = [regex]::Matches($tpl, $keyLine) | Select-Object -Last 1
    $bom = [string]::new([char[]](0xEF, 0xBB, 0xBF))
    $text = [ordered]@{
        'crlf, trailing newline'          = "; mine`r`n[Display]`r`nWindowed=0`r`nResizable=0`r`n"
        'lf, no trailing newline'         = "[Display]`nWindowed=0`nResizable=0"
        'utf8 BOM, comment first'         = $bom + "; mine`r`n[Display]`r`nWindowed=0`r`nResizable=0`r`n"
        'utf8 BOM, [Display] first'       = $bom + "[Display]`r`nWindowed=0`r`nResizable=0`r`n"   # the API ignores that first section: it counts as missing
        'two sections, custom values'     = "[Display]`r`nWindowed=0`r`n[Render]`r`nScale=3`r`n"
        'indented, other case, spaces'    = "[Display]`r`nWindowed=0`r`n  [ render ] ; mine`r`n vsync = 0`r`n"
        'empty file'                      = ''
        'comment only'                    = "; mine`r`n"
        'mac-style trailing CR only'      = "[Display]`r`nWindowed=0`r`nResizable=0`r"
        'all CR line ends'                = "[Display]`rWindowed=0`rResizable=0`r[Render]`rScale=3`r"
        'blank lines at the end'          = "[Display]`r`nWindowed=0`r`n`r`n`r`n"
        'non-ASCII bytes, LF, no newline' = "; caf$([char]0xE9) $([char]0xC3)$([char]0xA9) $([char]0x80)$([char]0xFF)`n[Display]`nWindowed=0 ; $([char]0xE9)"
        'the template itself'             = $tpl
        'the template with LF line ends'  = $tpl -replace '\r\n', "`n"
        'template minus its first key'    = [regex]::new($keyLine).Replace($tpl, '', 1)
        'template minus its last key'     = $tpl.Remove($lastKey.Index, $lastKey.Length)
        'every section, no keys'          = [regex]::Replace($tpl, $keyLine, '')
        'every key commented out'         = [regex]::Replace($tpl, '(?m)^([ \t]*[^;\s=\[][^=\r\n]*=)', '; $1')
        'first key: other case, spaces'   = "  [$($s1.ToUpper())]  `r`n`t$($k1.ToLower())  =  77 `r`n"
        'first key commented out'         = "[$s1]`r`n; $k1=77`r`n"
        'first section twice'             = "[$s1]`r`n; empty`r`n`r`n[Mine]`r`nA=1`r`n`r`n[$s1]`r`n$k1=77`r`n"   # the API reads the first one only
    }
    $bytes = [ordered]@{}
    foreach ($k in $text.Keys) { $bytes[$k] = $lat.GetBytes($text[$k]) }
    $bytes['utf16le'] = [byte[]](0xFF, 0xFE) + [Text.Encoding]::Unicode.GetBytes("[Display]`r`nWindowed=0`r`nResizable=0`r`n")
    $pureAppend = 'utf8 BOM, [Display] first', 'empty file', 'comment only'   # no section of the template in them: the old bytes stay an exact prefix
    $i = 0
    foreach ($k in $bytes.Keys) {
        $i++
        $g = NewGame "E$i"; $pkg = AddPkg $g
        $ini = Join-Path $g 'hogs.ini'; $before = Join-Path $T "E$i\hogs.ini.before"
        [IO.File]::WriteAllBytes($ini, $bytes[$k]); [IO.File]::WriteAllBytes($before, $bytes[$k])
        $r = RunPatch $pkg
        $after = [IO.File]::ReadAllBytes($ini)
        $r2 = RunPatch $pkg
        $idem = [Linq.Enumerable]::SequenceEqual([byte[]][IO.File]::ReadAllBytes($ini), [byte[]]$after) -and ($r2.Out -notmatch 'hogs\.ini: added')
        if ($k -eq 'utf16le') {   # not touched, the player is told
            $problems = @(if (-not [Linq.Enumerable]::SequenceEqual($after, [byte[]]$bytes[$k])) { 'file changed' }; if ($r.Out -notmatch 'UTF-16') { 'no UTF-16 warning' })
        } else {
            $problems = IniVerdict $before $ini $r.Out -Append:($k -in $pureAppend)
        }
        Check 'E' "ini '$k'" (($r.Code -eq 0) -and $idem -and -not $problems) "$($bytes[$k].Length)->$($after.Length) B, 2nd run unchanged=$idem$(if ($problems) { ' | ' + ($problems -join '; ') })"
    }
    $g = NewGame 'E0'; $pkg = AddPkg $g
    $r = RunPatch $pkg
    Check 'E' 'no hogs.ini: template copied byte for byte' (($r.Code -eq 0) -and ((FileSha $g 'hogs.ini') -eq (Sha $tplFile))) "exit $($r.Code)"
}

function ScenF {
    $leaf = 'Hogs [GOG] x'
    $g = NewGame 'F' -Leaf $leaf; $pkg = AddPkg $g; $s0 = Snap $g
    $r = RunPatch $pkg
    Check 'F' "game folder '$leaf' (brackets, spaces): install exit 0" ($r.Code -eq 0) ("exit $($r.Code) " + (Short $r.Out).Substring(0, [Math]::Min(100, (Short $r.Out).Length)))
    InstalledChecks 'F' $g 'F:'
    $s1 = Snap $g
    $r = RunPatch $pkg
    Check 'F' 're-run identical' (($r.Code -eq 0) -and (SameSnap $s1 (Snap $g))) "exit $($r.Code)"
    $r = RunPatch $pkg @('-Restore')
    Check 'F' '-Restore identical to original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
    $g = NewGame 'F2' -Leaf $leaf; $pkg = AddPkg $g $dist (Join-Path $T 'F2\pkg\HogsFix'); $s0 = Snap $g
    $r = RunPatch $pkg @() (Join-Path $T 'F2')
    Check 'F2' 'package outside the game folder, no -GameDir: clear error' (($r.Code -ne 0) -and ($r.Out -match 'warhogs_\.exe not found') -and ($r.Out -match 'pass -GameDir')) ((Short $r.Out).Substring(0, [Math]::Min(110, (Short $r.Out).Length)))
    $r = RunPatch $pkg @('-GameDir', ".\$leaf") (Join-Path $T 'F2')
    Check 'F2' 'relative -GameDir ".\Hogs [GOG] x" from the sandbox folder: install exit 0' (($r.Code -eq 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.patched) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd)) "exit $($r.Code)"
    $r = RunPatch $pkg @('-GameDir', ".\$leaf", '-Restore') (Join-Path $T 'F2')
    Check 'F2' 'relative -GameDir -Restore: identical to original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"
    $r = RunPatch $pkg @('-GameDir', (Join-Path $T 'F2\nowhere'))
    Check 'F2' '-GameDir that does not exist: error, exit != 0' (($r.Code -ne 0) -and ($r.Out -match 'PathNotFound')) "exit $($r.Code)"
}

function ScenG {
    $g = NewGame 'G'; $pkg = AddPkg $g; $s0 = Snap $g
    $dummyDir = Join-Path $T 'G\dummy'; [void][IO.Directory]::CreateDirectory($dummyDir)
    $dummy = Join-Path $dummyDir 'warhogs_.exe'
    Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32\ping.exe') -Destination $dummy
    $p = Start-Process -FilePath $dummy -ArgumentList '-n', '300', '127.0.0.1' -WindowStyle Hidden -PassThru
    try {
        Start-Sleep -Milliseconds 800
        Check 'G' 'dummy process "warhogs_" is running' ([bool](Get-Process -Name warhogs_ -ErrorAction SilentlyContinue)) "pid $($p.Id)"
        $r = RunPatch $pkg
        Check 'G' 'install refused while running (exit != 0, message)' (($r.Code -ne 0) -and ($r.Out -match 'Hogs of War is running')) "exit $($r.Code)"
        Check 'G' 'install: nothing changed' (SameSnap $s0 (Snap $g)) (DiffSnap $s0 (Snap $g))
        $r = RunPatch $pkg @('-Restore')
        Check 'G' '-Restore refused while running' (($r.Code -ne 0) -and ($r.Out -match 'Hogs of War is running')) "exit $($r.Code)"
    } finally { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500 }
    Check 'G' 'dummy process stopped' (-not (Get-Process -Name warhogs_ -ErrorAction SilentlyContinue))
    $r = RunPatch $pkg
    Check 'G' 'after stopping it the install goes through' (($r.Code -eq 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.patched)) "exit $($r.Code)"
}

function ScenH {
    $g = NewGame 'H'; $pkg = AddPkg $g; $s0 = Snap $g
    $r = RunPatch $pkg; $s1 = Snap $g
    Check 'H' 'first install ok' ($r.Code -eq 0) "exit $($r.Code)"
    Copy-Item -LiteralPath (Join-Path $repo 'backup\warhogs_.exe.orig') -Destination (Join-Path $g 'warhogs_.exe') -Force
    Copy-Item -LiteralPath (Join-Path $repo 'backup\winmm.dll.ogg-winmm-2014') -Destination (Join-Path $g 'winmm.dll') -Force
    $r = RunPatch $pkg
    Check 'H1' 'after "Steam verify" (original exe + 2014 winmm back): re-run repatches exe and swaps winmm' (($r.Code -eq 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.patched) -and ((FileSha $g 'winmm.dll') -eq $HX.winmm)) "exit $($r.Code)"
    Check 'H1' 'tree converges to the same state as the first install' (SameSnap $s1 (Snap $g)) (DiffSnap $s1 (Snap $g))
    [IO.File]::Delete((Join-Path $g 'warhogs_.exe.orig'))
    $sNo = Snap $g
    $r = RunPatch $pkg @('-Restore')
    Check 'H2' 'patched exe + missing .orig: -Restore refuses with the Steam hint' (($r.Code -ne 0) -and ($r.Out -match 'Verify integrity')) "exit $($r.Code)"
    Check 'H2' '... and removed nothing (ddraw.dll, ini, winmm still there)' ((SameSnap $sNo (Snap $g)) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd)) (DiffSnap $sNo (Snap $g))
    [IO.File]::WriteAllBytes((Join-Path $g 'warhogs_.exe.orig'), [byte[]](1..100))
    $sBad = Snap $g
    $r = RunPatch $pkg @('-Restore')
    Check 'H3' 'patched exe + damaged .orig: -Restore refuses, nothing removed' (($r.Code -ne 0) -and ($r.Out -match 'not the original') -and (SameSnap $sBad (Snap $g))) "exit $($r.Code)"
    Copy-Item -LiteralPath (Join-Path $repo 'backup\warhogs_.exe.orig') -Destination (Join-Path $g 'warhogs_.exe') -Force
    $r = RunPatch $pkg @('-Restore')
    Check 'H2' 'after Steam verify, -Restore completes and the tree equals the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
    $r = RunPatch $pkg; $null = $r
    Copy-Item -LiteralPath (Join-Path $repo 'backup\warhogs_.exe.orig') -Destination (Join-Path $g 'warhogs_.exe') -Force
    $r = RunPatch $pkg @('-Restore')
    Check 'H4' 'original exe back + valid .orig still there: -Restore ok, tree equals the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
    [IO.File]::WriteAllBytes((Join-Path $g 'warhogs_.exe.orig'), [byte[]](1..100))
    $r = RunPatch $pkg
    Check 'H5' 'damaged .orig + original exe: install replaces .orig by a good copy' (($r.Code -eq 0) -and ((FileSha $g 'warhogs_.exe.orig') -eq $HX.orig) -and ((FileSha $g 'warhogs_.exe') -eq $HX.patched)) "exit $($r.Code)"
}

# A run that fails half-way must leave a game that starts. Each check locks the file one step replaces or deletes, so
# the run stops there; it looks at what is left, then runs again without the lock.
function ScenI {
    $oldDd = Sha (Join-Path (BuildPrev) 'files\ddraw.dll')
    $g = OldInstall 'I1'; $pkg = AddPkg $g
    $r = RunLocked (Join-Path $g 'ddraw.dll') $pkg
    Check 'I1' 'update, ddraw.dll cannot be replaced: fails, exe still the v1.1 one with hogs.dll and its ddraw.dll' (($r.Code -ne 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.older) -and (Has (Join-Path $g 'hogs.dll')) -and ((FileSha $g 'ddraw.dll') -eq $oldDd)) "exit $($r.Code)"
    $r = RunPatch $pkg
    Check 'I1' '... re-run finishes the update' (($r.Code -eq 0) -and (Updated $g)) "exit $($r.Code)"
    $g = OldInstall 'I2'; $pkg = AddPkg $g
    $r = RunLocked (Join-Path $g 'warhogs_.exe') $pkg
    Check 'I2' 'update, exe cannot be replaced: fails after ddraw.dll, v1.1 exe + hogs.dll + new ddraw.dll (fixes twice, safe)' (($r.Code -ne 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.older) -and (Has (Join-Path $g 'hogs.dll')) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd)) "exit $($r.Code)"
    $r = RunPatch $pkg
    Check 'I2' '... re-run finishes the update' (($r.Code -eq 0) -and (Updated $g)) "exit $($r.Code)"
    $g = OldInstall 'I3'; $pkg = AddPkg $g
    $r = RunLocked (Join-Path $g 'hogs.dll') $pkg
    Check 'I3' 'update, hogs.dll cannot be deleted: fails with the new exe and ddraw.dll in place (hogs.dll unused)' (($r.Code -ne 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.patched) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd) -and (Has (Join-Path $g 'hogs.dll'))) "exit $($r.Code)"
    $r = RunPatch $pkg
    Check 'I3' '... re-run removes it' (($r.Code -eq 0) -and (Updated $g)) "exit $($r.Code)"
    $g = NewGame 'I4'; $pkg = AddPkg $g; $s0 = Snap $g
    $r = RunLocked (Join-Path $g 'warhogs_.exe') $pkg
    Check 'I4' 'fresh install, exe cannot be replaced: fails with the original exe and our ddraw.dll' (($r.Code -ne 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.orig) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd)) "exit $($r.Code)"
    $r = RunPatch $pkg
    Check 'I4' '... re-run finishes the install' (($r.Code -eq 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.patched) -and -not (Has (Join-Path $g 'warhogs_.exe.new'))) "exit $($r.Code)"
    $r = RunLocked (Join-Path $g 'ddraw.dll') $pkg @('-Restore')
    Check 'I5' '-Restore, ddraw.dll cannot be deleted: fails with the original exe already back' (($r.Code -ne 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.orig) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd)) "exit $($r.Code)"
    $r = RunPatch $pkg @('-Restore')
    Check 'I5' '... re-run -Restore: tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
    $r = RunPatch $pkg; $s1 = Snap $g
    $r = RunLocked (Join-Path $g 'warhogs_.exe') $pkg @('-Restore')
    Check 'I6' '-Restore, exe cannot be put back: fails and removes nothing' (($r.Code -ne 0) -and (SameSnap $s1 (Snap $g))) "exit $($r.Code) $(DiffSnap $s1 (Snap $g))"
    $r = RunPatch $pkg @('-Restore')
    Check 'I6' '... re-run -Restore: tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
}

function ScenX {
    $g = NewGame 'X1'; $pkg = AddPkg $g
    [IO.File]::Delete((Join-Path $pkg 'files\ddraw.dll')); $s0 = Snap $g
    $r = RunPatch $pkg
    Check 'X1' 'package without files\ddraw.dll: refuses before any change' (($r.Code -ne 0) -and ($r.Out -match 'files\\ddraw\.dll is missing') -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"
    $g = NewGame 'X2'; $pkg = AddPkg $g
    $b = [IO.File]::ReadAllBytes((Join-Path $g 'warhogs_.exe')); $b[1000] = $b[1000] -bxor 1; [IO.File]::WriteAllBytes((Join-Path $g 'warhogs_.exe'), $b); $s0 = Snap $g
    $r = RunPatch $pkg
    Check 'X2' 'unknown exe: refuses, nothing changed' (($r.Code -ne 0) -and ($r.Out -match 'Unknown warhogs_\.exe') -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"
    $r = RunPatch $pkg @('-Restore')
    Check 'X2' '-Restore on an unknown exe: says so, removes only our files (none here)' (($r.Code -eq 0) -and ($r.Out -match 'not a version this fix knows') -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"
    $g = NewGame 'X4'; $pkg = AddPkg $g
    $r = RunPatch $pkg @('-NoDgVoodoo')
    # error ids, not messages: Windows PowerShell prints messages in the language of Windows
    Check 'X4' 'old switch -NoDgVoodoo fails loudly (exit != 0, unknown parameter)' (($r.Code -ne 0) -and ($r.Out -match 'NamedParameterNotFound')) "exit $($r.Code)"
    $r = RunPatch $pkg @('-DgVoodoo')
    Check 'X4' 'old switch -DgVoodoo fails loudly' (($r.Code -ne 0) -and ($r.Out -match 'NamedParameterNotFound')) "exit $($r.Code)"
    $g = NewGame 'X5'; $pkg = AddPkg $g; $s0 = Snap $g
    $r = RunPatch $pkg; $s1 = Snap $g
    foreach ($n in 'D3DImm.dll', 'dgVoodoo.conf', 'dgVoodooCpl.exe') { [IO.File]::WriteAllText((Join-Path $g $n), 'old') }
    [IO.File]::WriteAllText((Join-Path $g 'dgVoodoo.installed-by-hogsfix'), 'marker')
    $r = RunPatch $pkg
    Check 'X5' 'marker + siblings next to our ddraw.dll: install cleans them, ddraw.dll stays ours' (($r.Code -eq 0) -and (SameSnap $s1 (Snap $g))) "exit $($r.Code) $(DiffSnap $s1 (Snap $g))"
    foreach ($n in 'D3DImm.dll', 'dgVoodoo.conf', 'dgVoodooCpl.exe') { [IO.File]::WriteAllText((Join-Path $g $n), 'old') }
    [IO.File]::WriteAllText((Join-Path $g 'dgVoodoo.installed-by-hogsfix'), 'marker')
    $r = RunPatch $pkg @('-Restore')
    Check 'X5' '-Restore in that state: tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) (DiffSnap $s0 (Snap $g))
    $g = NewGame 'X6'; $pkg = AddPkg $g; $s0 = Snap $g
    $r = RunPatch $pkg @('-Restore')
    Check 'X6' '-Restore on a never-patched folder: exit 0, nothing changed' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"
    $g = NewGame 'X7'; $pkg = AddPkg $g
    $dd = [IO.File]::ReadAllBytes((Join-Path $dist 'files\ddraw.dll')) + [byte[]](0, 0, 0, 0)
    [IO.File]::WriteAllBytes((Join-Path $g 'ddraw.dll'), $dd)
    $r = RunPatch $pkg
    Check 'X7' 'older build of our ddraw.dll: replaced, no .orig made' (($r.Code -eq 0) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd) -and -not (Has (Join-Path $g 'ddraw.dll.orig')) -and -not (Has (Join-Path $g 'DDraw.dll.orig'))) "exit $($r.Code)"
    $lone = Join-Path $T 'X8'; CleanDir $lone; [void][IO.Directory]::CreateDirectory($lone)
    Copy-Item -LiteralPath (Join-Path $dist 'patch.cmd') -Destination $lone
    [IO.File]::WriteAllText((Join-Path $lone 'run.cmd'), "@echo off`r`ncall `"$lone\patch.cmd`" < nul`r`necho EXIT=%errorlevel%`r`n")
    $o = RunCmd (Join-Path $lone 'run.cmd')
    Check 'X8' 'patch.cmd alone (no patch.ps1): friendly message, exit 1' (($o -match 'patch\.ps1 is missing') -and ($o -match 'EXIT=1')) (Short $o).Substring(0, [Math]::Min(140, (Short $o).Length))
    $g = NewGame 'X9' -Leaf 'Hogs [GOG] x'; $pkg = AddPkg $g $dist (Join-Path $T 'X9\pkg\HogsFix'); $s0 = Snap $g
    [IO.File]::WriteAllText((Join-Path $T 'X9\run.cmd'), "@echo off`r`ncall `"$pkg\patch.cmd`" -GameDir `"$g`" < nul`r`n")
    $o = RunCmd (Join-Path $T 'X9\run.cmd')
    Check 'X9' 'patch.cmd -GameDir "<path with spaces and brackets>" installs' (((FileSha $g 'warhogs_.exe') -eq $HX.patched) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd)) (Short $o).Substring(0, [Math]::Min(80, (Short $o).Length))
    [IO.File]::WriteAllText((Join-Path $T 'X9\run2.cmd'), "@echo off`r`ncall `"$pkg\patch.cmd`" -Restore -GameDir `"$g`" < nul`r`n")
    $o = RunCmd (Join-Path $T 'X9\run2.cmd')
    Check 'X9' 'patch.cmd -Restore -GameDir ... restores' (SameSnap $s0 (Snap $g)) (DiffSnap $s0 (Snap $g))
}

function ScenY {
    $g = NewGame 'Y1' -Dgv; $pkg = AddPkg $g
    $r = RunPatch $pkg
    [IO.File]::WriteAllText((Join-Path $g 'ddraw.dll'), 'some other wrapper')
    $other = FileSha $g 'ddraw.dll'
    $r = RunPatch $pkg @('-Restore')
    Check 'C3' 'foreign ddraw.dll put in later: -Restore leaves it, keeps DDraw.dll.orig, warns' (($r.Code -eq 0) -and ((FileSha $g 'ddraw.dll') -eq $other) -and ((FileSha $g 'DDraw.dll.orig') -eq $HX.dgvDD) -and ($r.Out -match 'DDraw\.dll\.orig left in place')) "exit $($r.Code)"
    Check 'C3' '... and the rest is restored (exe original, D3DImm.dll / dgVoodoo.conf / dgVoodooCpl.exe back)' (((FileSha $g 'warhogs_.exe') -eq $HX.orig) -and (Has (Join-Path $g 'D3DImm.dll')) -and (Has (Join-Path $g 'dgVoodoo.conf')) -and (Has (Join-Path $g 'dgVoodooCpl.exe'))) ''
    # the patched exe does not start with dgVoodoo's DDraw.dll in place of ours: a re-run puts ours back and says why
    $g = NewGame 'Y4'; $pkg = AddPkg $g
    $r = RunPatch $pkg
    foreach ($n in 'MS\x86\DDraw.dll', 'MS\x86\D3DImm.dll', 'dgVoodoo.conf', 'dgVoodooCpl.exe') { Copy-Item -LiteralPath (Join-Path $dgvDir $n) -Destination $g -Force }
    $r = RunPatch $pkg
    Check 'C4' 'dgVoodoo put over ours later: re-run puts ours back, keeps dgVoodoo as *.orig, says it does not start with it' (($r.Code -eq 0) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd) -and ((FileSha $g 'DDraw.dll.orig') -eq $HX.dgvDD) -and ($r.Out -match 'with another one \(dgVoodoo2\) it does not start')) "exit $($r.Code)"
    $g = NewGame 'Y2'; $pkg = AddPkg $g; $s0 = Snap $g
    foreach ($n in 'D3DImm.dll', 'dgVoodoo.conf', 'dgVoodooCpl.exe') { [IO.File]::WriteAllText((Join-Path $g $n), 'old') }
    [IO.File]::WriteAllText((Join-Path $g 'dgVoodoo.installed-by-hogsfix'), 'marker')
    $r = RunPatch $pkg
    Check 'X10' 'marker + siblings, no DDraw.dll: install puts ours in and cleans up' (($r.Code -eq 0) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd) -and -not ((Has (Join-Path $g 'D3DImm.dll')) -or (Has (Join-Path $g 'dgVoodoo.installed-by-hogsfix')))) "exit $($r.Code)"
    [IO.File]::WriteAllText((Join-Path $g 'hogs.log'), 'log'); [IO.File]::WriteAllText((Join-Path $g 'hogsdraw.log'), 'log')
    $r = RunPatch $pkg @('-Restore')
    Check 'X11' '-Restore also removes hogs.log and hogsdraw.log: tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) (DiffSnap $s0 (Snap $g))
    $g = NewGame 'Y3'; $pkg = AddPkg $g
    $r = RunPatch $pkg
    $dd2 = [IO.File]::ReadAllBytes((Join-Path $pkg 'files\ddraw.dll')) + [byte[]](7, 7, 7, 7)
    [IO.File]::WriteAllBytes((Join-Path $pkg 'files\ddraw.dll'), $dd2)           # "the new release": a different ddraw.dll
    $newDd = Sha (Join-Path $pkg 'files\ddraw.dll')
    $r = RunLocked (Join-Path $g 'ddraw.dll') $pkg @() 'None'
    Check 'X12' 'update while ddraw.dll is locked: fails loudly (exit != 0), old ddraw.dll intact' (($r.Code -ne 0) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd)) ("exit $($r.Code): " + (Short $r.Out).Substring([Math]::Max(0, (Short $r.Out).Length - 110)))
    $r = RunPatch $pkg
    Check 'X12' 're-run after the lock is gone: finishes the update (ddraw.dll = new one)' (($r.Code -eq 0) -and ((FileSha $g 'ddraw.dll') -eq $newDd)) "exit $($r.Code)"
}

# A stand-in for the translation zip: entry name -> text of the file.
function RuZip([string]$path, [hashtable]$entries) {
    if (Has $path) { [IO.File]::Delete($path) }
    $z = [IO.Compression.ZipFile]::Open($path, 'Create')
    try {
        foreach ($k in $entries.Keys) {
            $s = $z.CreateEntry($k).Open()
            $b = [Text.Encoding]::ASCII.GetBytes($entries[$k])
            $s.Write($b, 0, $b.Length); $s.Dispose()
        }
    } finally { $z.Dispose() }
}

function ScenR {
    $g = NewGame 'R'; $pkg = AddPkg $g
    $text = { param($rel) $p = Join-Path $g $rel; if (Has $p) { [IO.File]::ReadAllText($p) } else { '(absent)' } }
    foreach ($f in 'FEText\BIG.tab', 'Language\Text\fetext.bin', 'Speech\Sku1\Pig01\01AM0101.wav', 'Speech\Sku1\Pig01\untouched.wav') {
        $p = Join-Path $g $f; [void][IO.Directory]::CreateDirectory((Split-Path $p -Parent)); [IO.File]::WriteAllText($p, "english $f")
    }
    $s0 = Snap $g
    $zip = Join-Path $T 'R\ru.zip'
    $good = @{ 'FEText/BIG.tab' = 'russian tab'; 'FEText/macfont.tab' = 'new file'; 'Language/Text/fetext.bin' = 'russian text'; 'Speech/Sku1/Pig01/01AM0101.wav' = 'russian wav' }
    $ru = { param([string[]]$extra) RunPatch $pkg ($extra + @('-GameDir', $g)) '' 'russian.ps1' }

    RuZip $zip $good
    $r = & $ru @('-Zip', $zip)
    Check 'R1' 'russian.ps1 -Zip installs' (($r.Code -eq 0) -and ($r.Out -match 'Russian translation installed')) "exit $($r.Code): $((Short $r.Out).Substring(0, [Math]::Min(100, (Short $r.Out).Length)))"
    Check 'R1' 'the game files are the Russian ones, the new file is there' (((& $text 'FEText\BIG.tab') -eq 'russian tab') -and ((& $text 'Language\Text\fetext.bin') -eq 'russian text') -and
        ((& $text 'Speech\Sku1\Pig01\01AM0101.wav') -eq 'russian wav') -and ((& $text 'FEText\macfont.tab') -eq 'new file'))
    Check 'R1' 'a file the zip does not hold is untouched' ((& $text 'Speech\Sku1\Pig01\untouched.wav') -eq 'english Speech\Sku1\Pig01\untouched.wav')
    Check 'R1' 'english.orig holds the English files' (((& $text 'english.orig\FEText\BIG.tab') -eq 'english FEText\BIG.tab') -and ((& $text 'english.orig\Speech\Sku1\Pig01\01AM0101.wav') -eq 'english Speech\Sku1\Pig01\01AM0101.wav'))
    Check 'R1' 'added.txt names the new file, version.txt is written' (((& $text 'english.orig\added.txt').Trim() -eq 'FEText\macfont.tab') -and ((& $text 'english.orig\version.txt') -match '^\d+\.\d+\.\d+\s*$')) (Short (& $text 'english.orig\added.txt'))

    $good2 = $good.Clone(); $good2['FEText/BIG.tab'] = 'russian tab, newer'
    RuZip $zip $good2
    $r = & $ru @('-Zip', $zip)
    Check 'R2' 'second run with a newer zip: the game file is the newer one' (($r.Code -eq 0) -and ((& $text 'FEText\BIG.tab') -eq 'russian tab, newer')) "exit $($r.Code)"
    Check 'R2' 'second run: english.orig still holds the English file, added.txt has one line' (((& $text 'english.orig\FEText\BIG.tab') -eq 'english FEText\BIG.tab') -and
        (@((& $text 'english.orig\added.txt').Trim() -split '\r?\n').Count -eq 1) -and -not (Has (Join-Path $g 'english.orig\FEText\macfont.tab')))

    $r = & $ru @('-Remove')
    Check 'R3' '-Remove: tree identical to the one before the translation' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) (DiffSnap $s0 (Snap $g))
    $r = & $ru @('-Remove')
    Check 'R3' '-Remove with nothing installed: exit 0 and says so' (($r.Code -eq 0) -and ($r.Out -match 'not installed') -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"

    foreach ($bad in @('a program', 'FEText/run.exe'), @('a file outside the three folders', 'Data/x.bmp'), @('a path that climbs out', 'FEText/../../x.bmp'),
                      @('a file in the game folder itself', 'x.bmp'), @('an absolute path', '/FEText/x.bmp')) {
        $e = $good.Clone(); $e[$bad[1]] = 'x'
        RuZip $zip $e
        $r = & $ru @('-Zip', $zip)
        Check 'R4' "zip with $($bad[0]) ($($bad[1])): refused, nothing changed" (($r.Code -ne 0) -and ($r.Out -match 'not game data') -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
    }
    $r = & $ru @('-Zip', (Join-Path $T 'R\none.zip'))
    Check 'R4' 'a zip that does not exist: fails, nothing changed' (($r.Code -ne 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"

    RuZip $zip $good
    $r = RunPatch $pkg
    Check 'R5' 'patch.ps1 with no keyboard: installs the fix and does not offer the translation' (($r.Code -eq 0) -and ($r.Out -match 'Done\. Start the game') -and -not (Has (Join-Path $g 'english.orig\version.txt'))) "exit $($r.Code)"
    $r = & $ru @('-Zip', $zip)
    $r2 = RunPatch $pkg
    Check 'R5' 'patch.ps1 again over the translation: finishes, translation stays' (($r.Code -eq 0) -and ($r2.Code -eq 0) -and ((& $text 'FEText\BIG.tab') -eq 'russian tab')) "exit $($r.Code), $($r2.Code)"
    [IO.File]::WriteAllText((Join-Path $g 'english.orig\version.txt'), "0.9.0`r`n")
    $r = RunPatch $pkg
    Check 'R5' 'patch.ps1 over an older translation: says that a newer one can be installed' (($r.Code -eq 0) -and ($r.Out -match 'newer Russian translation') -and ($r.Out -match 'the game has 0\.9\.0')) "exit $($r.Code)"
    $r = & $ru @('-Zip', $zip)
    $r2 = RunPatch $pkg
    Check 'R5' 'after russian.ps1 the version is the current one and patch.ps1 says nothing about it' (($r.Code -eq 0) -and ($r2.Out -notmatch 'newer Russian translation')) "exit $($r.Code)"
    $r = RunPatch $pkg @('-Restore')
    Check 'R5' 'patch.ps1 -Restore removes the translation too: tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"

    $dummyDir = Join-Path $T 'R\dummy'; [void][IO.Directory]::CreateDirectory($dummyDir)
    $dummy = Join-Path $dummyDir 'warhogs_.exe'
    Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32\ping.exe') -Destination $dummy
    $p = Start-Process -FilePath $dummy -ArgumentList '-n', '300', '127.0.0.1' -WindowStyle Hidden -PassThru
    try {
        Start-Sleep -Milliseconds 800
        $r = & $ru @('-Zip', $zip)
        Check 'R6' 'refused while the game runs, nothing changed' (($r.Code -ne 0) -and ($r.Out -match 'Hogs of War is running') -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"
    } finally { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500 }
}

if (Get-Process -Name warhogs_ -ErrorAction SilentlyContinue) { throw 'test_patcher: close Hogs of War first (the patcher refuses to run while it is running)' }
$list = if ($Which -eq 'All') { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'R', 'X', 'Y' } else { $Which }
CleanDir $T   # left over from an aborted run
[void][IO.Directory]::CreateDirectory($T)
try {
    PrepareFixtures
    foreach ($s in $list) { & ('Scen' + $s) }
} finally {
    try { CleanDir $T } catch { Write-Warning "could not delete ${T}: $($_.Exception.Message)" }
}
$failed = @($script:Results | Where-Object { -not $_.Pass }).Count
''
'--- {0} checks, {1} failed' -f $script:Results.Count, $failed
if ($failed) { exit 1 }
