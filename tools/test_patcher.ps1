# Regression test for the player patcher (patcher\patch.ps1 and patch.cmd), run against the package that
# tools\make_release.ps1 builds. Developer tool, not shipped.
#
#   pwsh tools\test_patcher.ps1              all scenarios
#   pwsh tools\test_patcher.ps1 -Which B     one scenario: A B C D E F G H X or Y
#
# Needs: PowerShell 7 (pwsh) to run it, Windows PowerShell 5.1 (the patcher under test runs in it), git
# (scenario B builds the previous release from history) and, in the repo:
#   dist\HogsFix                       run tools\make_release.ps1 first
#   backup\warhogs_.exe.orig           the original Steam v1.2 warhogs_.exe    (backup\ and downloads\ are not in git)
#   backup\winmm.dll.ogg-winmm-2014    the winmm.dll that comes with the game
#   downloads\dgVoodoo2_87_5.zip       where the scenarios take their dgVoodoo files from
# Close Hogs of War first: the patcher refuses to run while a warhogs_ process exists.
#
# Every scenario works on fresh copies in <temp>\hogsfix-patcher-tests, which this script creates and deletes;
# no game folder is touched. It prints one line per check and ends with "--- N checks, M failed" (exit code 1 if M > 0).
#
#   A  patch.cmd, re-run, -Restore, uninstall.cmd   F  game folder with [ ] and spaces, relative -GameDir
#   B  update from the previous release, old -Restore  G  game running (a dummy warhogs_ process)
#   C  hand-installed dgVoodoo kept as *.orig        H  Steam "verify files", missing or damaged .orig
#   D  refuses when a *.orig is in the way           X  missing package file, unknown exe, old switches, patch.cmd
#   E  hogs.ini variants and the [Render] append     Y  foreign ddraw.dll later, leftovers, logs, locked file
[CmdletBinding()]
param([ValidateSet('All', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'X', 'Y')][string]$Which = 'All')
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'test_patcher: run it with PowerShell 7: pwsh tools\test_patcher.ps1' }
. (Join-Path $PSScriptRoot 'test_patcher_lib.ps1')

function P8([string]$h) { if ($h.Length -ge 8) { $h.Substring(0, 8) } else { $h } }

# bytes of the text the patcher must append to an ini that has no [Render]: the template's section, CRLF
function RenderBlock { $tpl = [IO.File]::ReadAllText((Join-Path $dist 'files\hogs.ini')); ([regex]::Match($tpl, '(?ms)^\[Render\].*?(?=^\[|\z)').Value -replace '\r?\n', "`r`n") }

function InstalledChecks([string]$sc, [string]$g, [string]$tag) {
    Check $sc "$tag exe is the patched one" ((FileSha $g 'warhogs_.exe') -eq $HX.patched) (P8 (FileSha $g 'warhogs_.exe'))
    Check $sc "$tag warhogs_.exe.orig is the original" ((FileSha $g 'warhogs_.exe.orig') -eq $HX.orig) (P8 (FileSha $g 'warhogs_.exe.orig'))
    Check $sc "$tag hogs.dll = package" ((FileSha $g 'hogs.dll') -eq $HX.hogs)
    Check $sc "$tag ddraw.dll = package" ((FileSha $g 'ddraw.dll') -eq $HX.dd)
    Check $sc "$tag winmm.dll = ayuanx, winmm.dll.orig = the 2014 one" (((FileSha $g 'winmm.dll') -eq $HX.winmm) -and ((FileSha $g 'winmm.dll.orig') -eq $HX.bad)) ''
    Check $sc "$tag winmm.ini present" (Has (Join-Path $g 'winmm.ini'))
}

function ScenA {
    $g = NewGame 'A'; $pkg = AddPkg $g; $s0 = Snap $g
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
    $old = AddPkg $g (BuildPrev)
    # the released script, except that its dgVoodoo "download" copies the zip we already have
    $ps = Join-Path $old 'patch.ps1'; $txt = [IO.File]::ReadAllText($ps)
    $line = 'Invoke-WebRequest $dgvUrl -OutFile $zip -UseBasicParsing'
    if (-not $txt.Contains($line)) { throw 'old script changed?' }
    $zipSrc = (Join-Path $repo 'downloads\dgVoodoo2_87_5.zip') -replace "'", "''"
    [IO.File]::WriteAllText($ps, $txt.Replace($line, "Copy-Item -LiteralPath '$zipSrc' -Destination `$zip"))
    $tmpd = Join-Path $T 'B\tmp'; [void][IO.Directory]::CreateDirectory($tmpd)
    $oldTemp = $env:TEMP; $oldTmp = $env:TMP; $env:TEMP = $tmpd; $env:TMP = $tmpd
    try { $r = RunPatch $old @('-DgVoodoo') } finally { $env:TEMP = $oldTemp; $env:TMP = $oldTmp }
    CleanDir $tmpd
    Check 'B' 'previous release (old patch.ps1, old files) installed incl. dgVoodoo + marker' (($r.Code -eq 0) -and (Has (Join-Path $g 'dgVoodoo.installed-by-hogsfix')) -and (Has (Join-Path $g 'dgVoodooCpl.exe'))) "exit $($r.Code)"
    # the player's file: made before [Render] existed (so without it), and two settings changed
    $ini = Join-Path $g 'hogs.ini'
    $iniText = [regex]::Replace([IO.File]::ReadAllText($ini), '(?ms)^\[Render\].*?(?=^\[|\z)', '')
    [IO.File]::WriteAllText($ini, $iniText.Replace('Windowed=1', 'Windowed=0').Replace('Resizable=1', 'Resizable=0'))
    Check 'B' 'old state: DDraw.dll is dgVoodoo 2.87.5, old hogs.dll, ini has no [Render]' (((FileSha $g 'DDraw.dll') -eq $HX.dgvDD) -and ((FileSha $g 'hogs.dll') -ne $HX.hogs) -and -not ([IO.File]::ReadAllText($ini) -match '\[Render\]')) ''
    $iniBefore = [IO.File]::ReadAllBytes($ini)
    $winmmBefore = FileSha $g 'winmm.dll'; $origBefore = FileSha $g 'warhogs_.exe.orig'; $exeBefore = FileSha $g 'warhogs_.exe'
    # copy of this state for B2 (-Restore straight from the old install)
    $g2 = Join-Path $T 'B2\game'; CleanDir (Join-Path $T 'B2'); [void][IO.Directory]::CreateDirectory((Split-Path $g2 -Parent))
    Copy-Item -LiteralPath $g -Destination $g2 -Recurse
    # --- update with the new package
    $new = AddPkg $g
    $r = RunPatch $new
    Check 'B' 'update: exit 0, exe "already patched", old dgVoodoo reported removed' (($r.Code -eq 0) -and ($r.Out -match 'already patched') -and ($r.Out -match 'dgVoodoo2 installed by an older version removed')) "exit $($r.Code)"
    Check 'B' 'update: dgVoodoo files and marker gone (D3DImm.dll, dgVoodoo.conf, dgVoodooCpl.exe, marker)' (-not ((Has (Join-Path $g 'D3DImm.dll')) -or (Has (Join-Path $g 'dgVoodoo.conf')) -or (Has (Join-Path $g 'dgVoodooCpl.exe')) -or (Has (Join-Path $g 'dgVoodoo.installed-by-hogsfix')))) ''
    Check 'B' 'update: ddraw.dll is ours (= package), not dgVoodoo' (((FileSha $g 'ddraw.dll') -eq $HX.dd) -and ((FileSha $g 'ddraw.dll') -ne $HX.dgvDD)) (P8 (FileSha $g 'ddraw.dll'))
    Check 'B' 'update: hogs.dll replaced by the new one' ((FileSha $g 'hogs.dll') -eq $HX.hogs)
    Check 'B' 'update: exe, exe.orig, winmm.dll untouched' (((FileSha $g 'warhogs_.exe') -eq $exeBefore) -and ((FileSha $g 'warhogs_.exe.orig') -eq $origBefore) -and ((FileSha $g 'winmm.dll') -eq $winmmBefore)) ''
    $iniAfter = [IO.File]::ReadAllBytes($ini)
    $prefix = ($iniAfter.Length -gt $iniBefore.Length) -and ([Linq.Enumerable]::SequenceEqual([byte[]]$iniAfter[0..($iniBefore.Length - 1)], [byte[]]$iniBefore))
    $tail = [Text.Encoding]::ASCII.GetString($iniAfter, $iniBefore.Length, $iniAfter.Length - $iniBefore.Length)
    Check 'B' 'update: hogs.ini = old bytes untouched + appended [Render] block (CRLF)' ($prefix -and ($tail -ceq ("`r`n" + (RenderBlock)))) "old $($iniBefore.Length) B -> $($iniAfter.Length) B"
    Check 'B' "update: player's values kept, [Render] defaults present" ((IniVals $ini) -eq 'Windowed=0 Resizable=0 Scale=0 VSync=1') (IniVals $ini)
    $sUpd = Snap $g
    $r = RunPatch $new
    Check 'B' 'update again: idempotent (identical tree)' (($r.Code -eq 0) -and (SameSnap $sUpd (Snap $g))) "exit $($r.Code)"
    $r = RunPatch $new @('-Restore')
    Check 'B' '-Restore after update: tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
    # --- B2: new -Restore directly on the previous release's install
    $new2 = AddPkg $g2 $dist (Join-Path $g2 'HogsFix')
    $r = RunPatch $new2 @('-Restore')
    Check 'B2' '-Restore on an old-version install: exit 0, dgVoodoo reported removed' (($r.Code -eq 0) -and ($r.Out -match 'dgVoodoo2 installed by an older version removed')) "exit $($r.Code)"
    Check 'B2' '-Restore on an old-version install: tree identical to the original' (SameSnap $s0 (Snap $g2)) (DiffSnap $s0 (Snap $g2))
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
    # C2: only a foreign DDraw.dll (e.g. another wrapper), nothing else
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
    Check 'D' 'nothing changed (tree identical, exe still original, no hogs.dll)' ((SameSnap $s0 (Snap $g)) -and ((FileSha $g 'warhogs_.exe') -eq $HX.orig) -and -not (Has (Join-Path $g 'hogs.dll'))) (DiffSnap $s0 (Snap $g))
    # D2: the collision is on a sibling, not on DDraw.dll
    [IO.File]::Delete((Join-Path $g 'DDraw.dll.orig')); [IO.File]::WriteAllText((Join-Path $g 'dgVoodoo.conf.orig'), 'x')
    $s0 = Snap $g
    $r = RunPatch $pkg
    Check 'D2' 'collision on dgVoodoo.conf.orig also refuses before any change' (($r.Code -ne 0) -and ($r.Out -match 'dgVoodoo\.conf\.orig is in the way') -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"
}

function ScenE {
    $block = RenderBlock
    $enc = [Text.Encoding]::ASCII
    $variants = [ordered]@{
        'crlf, trailing newline'      = $enc.GetBytes("; mine`r`n[Display]`r`nWindowed=0`r`nResizable=0`r`n")
        'lf, no trailing newline'     = $enc.GetBytes("[Display]`nWindowed=0`nResizable=0")
        'utf8 BOM, comment first'     = ([byte[]](0xEF, 0xBB, 0xBF) + $enc.GetBytes("; mine`r`n[Display]`r`nWindowed=0`r`nResizable=0`r`n"))
        'utf8 BOM, [Display] first'   = ([byte[]](0xEF, 0xBB, 0xBF) + $enc.GetBytes("[Display]`r`nWindowed=0`r`nResizable=0`r`n"))
        'utf16le'                     = ([byte[]](0xFF, 0xFE) + [Text.Encoding]::Unicode.GetBytes("[Display]`r`nWindowed=0`r`nResizable=0`r`n"))
        'has [Render] (custom)'       = $enc.GetBytes("[Display]`r`nWindowed=0`r`n[Render]`r`nScale=3`r`n")
        'has indented [render]'       = $enc.GetBytes("[Display]`r`nWindowed=0`r`n  [render]`r`nVSync=0`r`n")
        'empty file'                  = [byte[]]@()
        'mac-style trailing CR only'  = $enc.GetBytes("[Display]`r`nWindowed=0`r`nResizable=0`r")
    }
    $expect = @{
        'crlf, trailing newline' = 'Windowed=0 Resizable=0 Scale=0 VSync=1'; 'lf, no trailing newline' = 'Windowed=0 Resizable=0 Scale=0 VSync=1'
        'utf8 BOM, comment first' = 'Windowed=0 Resizable=0 Scale=0 VSync=1'
        'utf8 BOM, [Display] first' = 'Windowed=99 Resizable=99 Scale=0 VSync=1'   # the API ignores the first section of such a file, with or without us
        'utf16le' = 'Windowed=0 Resizable=0 Scale=99 VSync=99'; 'has [Render] (custom)' = 'Windowed=0 Resizable=99 Scale=3 VSync=99'
        'has indented [render]' = 'Windowed=0 Resizable=99 Scale=99 VSync=0'; 'empty file' = 'Windowed=99 Resizable=99 Scale=0 VSync=1'
        'mac-style trailing CR only' = 'Windowed=0 Resizable=0 Scale=0 VSync=1'
    }
    $i = 0
    foreach ($k in $variants.Keys) {
        $i++
        $g = NewGame "E$i"; $pkg = AddPkg $g
        $ini = Join-Path $g 'hogs.ini'
        [IO.File]::WriteAllBytes($ini, [byte[]]$variants[$k])
        $before = [IO.File]::ReadAllBytes($ini); $valsBefore = IniVals $ini
        $r = RunPatch $pkg
        $after = [IO.File]::ReadAllBytes($ini); $vals = IniVals $ini
        $kept = ($after.Length -ge $before.Length) -and ($before.Length -eq 0 -or [Linq.Enumerable]::SequenceEqual([byte[]]$after[0..($before.Length - 1)], [byte[]]$before))
        $grew = $after.Length -gt $before.Length
        $null = RunPatch $pkg; $after2 = [IO.File]::ReadAllBytes($ini)
        $idem = [Linq.Enumerable]::SequenceEqual([byte[]]$after2, [byte[]]$after)
        $wantGrow = ($k -notin 'utf16le', 'has [Render] (custom)', 'has indented [render]')
        $note = ''
        if ($grew) { $tail = [Text.Encoding]::ASCII.GetString($after, $before.Length, $after.Length - $before.Length); $note = if ($tail.EndsWith($block) -and $tail.StartsWith("`r`n")) { 'appended = CRLF + template [Render]' } else { 'TAIL MISMATCH' } }
        if ($k -eq 'utf16le') { $note = if ($r.Out -match 'UTF-16') { 'warned: UTF-16' } else { 'no warning' } }
        $ok = ($r.Code -eq 0) -and $kept -and ($grew -eq $wantGrow) -and $idem -and ($vals -eq $expect[$k]) -and ($note -ne 'TAIL MISMATCH') -and ($note -ne 'no warning')
        Check 'E' "ini '$k'" $ok "before: $valsBefore | after: $vals | $($before.Length)->$($after.Length) B, old bytes kept=$kept, 2nd run unchanged=$idem, $note"
    }
    # no ini at all: template copied as it is
    $g = NewGame 'E0'; $pkg = AddPkg $g
    $r = RunPatch $pkg
    Check 'E' 'no hogs.ini: template copied byte for byte' (($r.Code -eq 0) -and ((FileSha $g 'hogs.ini') -eq (Sha (Join-Path $dist 'files\hogs.ini')))) (IniVals (Join-Path $g 'hogs.ini'))
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
    # F2: package outside the game folder, relative -GameDir with brackets, cwd = the sandbox
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
    # H1: Steam "verify integrity" puts back the original exe and the 2014 winmm.dll
    Copy-Item -LiteralPath (Join-Path $repo 'backup\warhogs_.exe.orig') -Destination (Join-Path $g 'warhogs_.exe') -Force
    Copy-Item -LiteralPath (Join-Path $repo 'backup\winmm.dll.ogg-winmm-2014') -Destination (Join-Path $g 'winmm.dll') -Force
    $r = RunPatch $pkg
    Check 'H1' 'after "Steam verify" (original exe + 2014 winmm back): re-run repatches exe and swaps winmm' (($r.Code -eq 0) -and ((FileSha $g 'warhogs_.exe') -eq $HX.patched) -and ((FileSha $g 'winmm.dll') -eq $HX.winmm)) "exit $($r.Code)"
    Check 'H1' 'tree converges to the same state as the first install' (SameSnap $s1 (Snap $g)) (DiffSnap $s1 (Snap $g))
    # H2: patched exe and the .orig is gone: -Restore must refuse and keep hogs.dll
    [IO.File]::Delete((Join-Path $g 'warhogs_.exe.orig'))
    $sNo = Snap $g
    $r = RunPatch $pkg @('-Restore')
    Check 'H2' 'patched exe + missing .orig: -Restore refuses with the Steam hint' (($r.Code -ne 0) -and ($r.Out -match 'Verify integrity')) "exit $($r.Code)"
    Check 'H2' '... and removed nothing (hogs.dll, ddraw.dll, ini, winmm still there)' ((SameSnap $sNo (Snap $g)) -and (Has (Join-Path $g 'hogs.dll'))) (DiffSnap $sNo (Snap $g))
    # H3: patched exe + truncated .orig: same refusal
    [IO.File]::WriteAllBytes((Join-Path $g 'warhogs_.exe.orig'), [byte[]](1..100))
    $sBad = Snap $g
    $r = RunPatch $pkg @('-Restore')
    Check 'H3' 'patched exe + damaged .orig: -Restore refuses, nothing removed' (($r.Code -ne 0) -and ($r.Out -match 'not the original') -and (SameSnap $sBad (Snap $g))) "exit $($r.Code)"
    # H2 cont.: user runs Steam verify (original exe back), then -Restore completes (stale damaged .orig is dropped)
    Copy-Item -LiteralPath (Join-Path $repo 'backup\warhogs_.exe.orig') -Destination (Join-Path $g 'warhogs_.exe') -Force
    $r = RunPatch $pkg @('-Restore')
    Check 'H2' 'after Steam verify, -Restore completes and the tree equals the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
    # H4: Steam verify but the (valid) .orig is still there: -Restore just consumes it
    $r = RunPatch $pkg; $null = $r
    Copy-Item -LiteralPath (Join-Path $repo 'backup\warhogs_.exe.orig') -Destination (Join-Path $g 'warhogs_.exe') -Force
    $r = RunPatch $pkg @('-Restore')
    Check 'H4' 'original exe back + valid .orig still there: -Restore ok, tree equals the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code) $(DiffSnap $s0 (Snap $g))"
    # H5: interrupted earlier run left a damaged .orig next to the ORIGINAL exe: install repairs it
    [IO.File]::WriteAllBytes((Join-Path $g 'warhogs_.exe.orig'), [byte[]](1..100))
    $r = RunPatch $pkg
    Check 'H5' 'damaged .orig + original exe: install replaces .orig by a good copy' (($r.Code -eq 0) -and ((FileSha $g 'warhogs_.exe.orig') -eq $HX.orig) -and ((FileSha $g 'warhogs_.exe') -eq $HX.patched)) "exit $($r.Code)"
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
    # X5: interrupted old-patcher cleanup: marker + siblings next to OUR ddraw.dll
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
    # X6: Restore on a folder that never had the fix
    $g = NewGame 'X6'; $pkg = AddPkg $g; $s0 = Snap $g
    $r = RunPatch $pkg @('-Restore')
    Check 'X6' '-Restore on a never-patched folder: exit 0, nothing changed' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) "exit $($r.Code)"
    # X7: an older build of OUR ddraw.dll (signature inside, other hash) is simply replaced
    $g = NewGame 'X7'; $pkg = AddPkg $g
    $dd = [IO.File]::ReadAllBytes((Join-Path $dist 'files\ddraw.dll')) + [byte[]](0, 0, 0, 0)
    [IO.File]::WriteAllBytes((Join-Path $g 'ddraw.dll'), $dd)
    $r = RunPatch $pkg
    Check 'X7' 'older build of our ddraw.dll: replaced, no .orig made' (($r.Code -eq 0) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd) -and -not (Has (Join-Path $g 'ddraw.dll.orig')) -and -not (Has (Join-Path $g 'DDraw.dll.orig'))) "exit $($r.Code)"
    # X8: patch.cmd guard when patch.ps1 is not next to it (opened from inside the zip)
    $lone = Join-Path $T 'X8'; CleanDir $lone; [void][IO.Directory]::CreateDirectory($lone)
    Copy-Item -LiteralPath (Join-Path $dist 'patch.cmd') -Destination $lone
    [IO.File]::WriteAllText((Join-Path $lone 'run.cmd'), "@echo off`r`ncall `"$lone\patch.cmd`" < nul`r`necho EXIT=%errorlevel%`r`n")
    $o = RunCmd (Join-Path $lone 'run.cmd')
    Check 'X8' 'patch.cmd alone (no patch.ps1): friendly message, exit 1' (($o -match 'patch\.ps1 is missing') -and ($o -match 'EXIT=1')) (Short $o).Substring(0, [Math]::Min(140, (Short $o).Length))
    # X9: patch.cmd passes arguments (-GameDir with spaces and brackets, -Restore) through
    $g = NewGame 'X9' -Leaf 'Hogs [GOG] x'; $pkg = AddPkg $g $dist (Join-Path $T 'X9\pkg\HogsFix'); $s0 = Snap $g
    [IO.File]::WriteAllText((Join-Path $T 'X9\run.cmd'), "@echo off`r`ncall `"$pkg\patch.cmd`" -GameDir `"$g`" < nul`r`n")
    $o = RunCmd (Join-Path $T 'X9\run.cmd')
    Check 'X9' 'patch.cmd -GameDir "<path with spaces and brackets>" installs' (((FileSha $g 'warhogs_.exe') -eq $HX.patched) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd)) (Short $o).Substring(0, [Math]::Min(80, (Short $o).Length))
    [IO.File]::WriteAllText((Join-Path $T 'X9\run2.cmd'), "@echo off`r`ncall `"$pkg\patch.cmd`" -Restore -GameDir `"$g`" < nul`r`n")
    $o = RunCmd (Join-Path $T 'X9\run2.cmd')
    Check 'X9' 'patch.cmd -Restore -GameDir ... restores' (SameSnap $s0 (Snap $g)) (DiffSnap $s0 (Snap $g))
}

function ScenY {
    # C3: hand-installed dgVoodoo backed up, then the player puts ANOTHER wrapper in as ddraw.dll: -Restore must not delete it
    $g = NewGame 'Y1' -Dgv; $pkg = AddPkg $g
    $r = RunPatch $pkg
    [IO.File]::WriteAllText((Join-Path $g 'ddraw.dll'), 'some other wrapper')
    $other = FileSha $g 'ddraw.dll'
    $r = RunPatch $pkg @('-Restore')
    Check 'C3' 'foreign ddraw.dll put in later: -Restore leaves it, keeps DDraw.dll.orig, warns' (($r.Code -eq 0) -and ((FileSha $g 'ddraw.dll') -eq $other) -and ((FileSha $g 'DDraw.dll.orig') -eq $HX.dgvDD) -and ($r.Out -match 'DDraw\.dll\.orig left in place')) "exit $($r.Code)"
    Check 'C3' '... and the rest is restored (exe original, D3DImm.dll / dgVoodoo.conf / dgVoodooCpl.exe back, hogs.dll gone)' (((FileSha $g 'warhogs_.exe') -eq $HX.orig) -and (Has (Join-Path $g 'D3DImm.dll')) -and (Has (Join-Path $g 'dgVoodoo.conf')) -and (Has (Join-Path $g 'dgVoodooCpl.exe')) -and -not (Has (Join-Path $g 'hogs.dll'))) ''
    # X10: old-patcher leftovers (marker + siblings) but its DDraw.dll was deleted by the player
    $g = NewGame 'Y2'; $pkg = AddPkg $g; $s0 = Snap $g
    foreach ($n in 'D3DImm.dll', 'dgVoodoo.conf', 'dgVoodooCpl.exe') { [IO.File]::WriteAllText((Join-Path $g $n), 'old') }
    [IO.File]::WriteAllText((Join-Path $g 'dgVoodoo.installed-by-hogsfix'), 'marker')
    $r = RunPatch $pkg
    Check 'X10' 'marker + siblings, no DDraw.dll: install puts ours in and cleans up' (($r.Code -eq 0) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd) -and -not ((Has (Join-Path $g 'D3DImm.dll')) -or (Has (Join-Path $g 'dgVoodoo.installed-by-hogsfix')))) "exit $($r.Code)"
    # X11: logs written by the game are removed by -Restore
    [IO.File]::WriteAllText((Join-Path $g 'hogs.log'), 'log'); [IO.File]::WriteAllText((Join-Path $g 'hogsdraw.log'), 'log')
    $r = RunPatch $pkg @('-Restore')
    Check 'X11' '-Restore also removes hogs.log and hogsdraw.log: tree identical to the original' (($r.Code -eq 0) -and (SameSnap $s0 (Snap $g))) (DiffSnap $s0 (Snap $g))
    # X12: an update fails half way (ddraw.dll locked by another program), the re-run finishes it
    $g = NewGame 'Y3'; $pkg = AddPkg $g
    $r = RunPatch $pkg
    $dd2 = [IO.File]::ReadAllBytes((Join-Path $pkg 'files\ddraw.dll')) + [byte[]](7, 7, 7, 7)
    [IO.File]::WriteAllBytes((Join-Path $pkg 'files\ddraw.dll'), $dd2)           # "the new release": a different ddraw.dll
    $newDd = Sha (Join-Path $pkg 'files\ddraw.dll')
    $lock = [IO.File]::Open((Join-Path $g 'ddraw.dll'), 'Open', 'Read', 'None')
    try { $r = RunPatch $pkg } finally { $lock.Dispose() }
    Check 'X12' 'update while ddraw.dll is locked: fails loudly (exit != 0), old ddraw.dll intact' (($r.Code -ne 0) -and ((FileSha $g 'ddraw.dll') -eq $HX.dd)) ("exit $($r.Code): " + (Short $r.Out).Substring([Math]::Max(0, (Short $r.Out).Length - 110)))
    $r = RunPatch $pkg
    Check 'X12' 're-run after the lock is gone: finishes the update (ddraw.dll = new one)' (($r.Code -eq 0) -and ((FileSha $g 'ddraw.dll') -eq $newDd)) "exit $($r.Code)"
}

# --- run
if (Get-Process -Name warhogs_ -ErrorAction SilentlyContinue) { throw 'test_patcher: close Hogs of War first (the patcher refuses to run while it is running)' }
$list = if ($Which -eq 'All') { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'X', 'Y' } else { $Which }
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
