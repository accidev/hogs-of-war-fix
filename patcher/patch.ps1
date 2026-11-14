# Hogs of War fix - patcher for the player's own copy of the game; ships no game code.
#   patch.cmd                                   install or update the fix
#   patch.cmd -Restore                          put the original files back
#   patch.cmd -GameDir "D:\Games\Hogs of War"   game folder (default: the folder above this one)
#   russian.cmd                                 the Russian translation (russian.ps1, a separate download; see step 7)
# Windows PowerShell 5.1; keep this file ASCII. CmdletBinding: an unknown switch (the old -NoDgVoodoo) is an error.
# The patched exe imports CallDLL from ddraw.dll and starts only with our ddraw.dll (Windows' own has none); the exe of
# v1.0-v1.1 imports it from hogs.dll and needs that file. So that an interrupted run leaves a game that starts: our
# ddraw.dll goes in before such an exe, hogs.dll goes only once no exe imports it, -Restore puts the original exe back
# before it removes ddraw.dll.
[CmdletBinding()] param([string]$GameDir, [switch]$Restore)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$files = Join-Path $here 'files'
$patch = Get-Content -LiteralPath (Join-Path $here 'warhogs_v12.json') -Raw | ConvertFrom-Json

# the 2014 ogg-winmm build, which deadlocks the Windows 10/11 loader
$badWinmm = '9ACA7D1B2649256970D38ECF346CB90CAE3545DD2DC73F63BC1030E1CCE4CEA4'
# dgVoodoo2 files: installed by an older patcher (then the marker exists) or by the player.
# DDraw.dll is the same path as our ddraw.dll (names ignore case).
$dgvSiblings = 'D3DImm.dll', 'dgVoodoo.conf', 'dgVoodooCpl.exe'
$dgvFiles = @('DDraw.dll') + $dgvSiblings
$dgvMarker = 'dgVoodoo.installed-by-hogsfix'
# text inside every build of our ddraw.dll (its log file name)
$ddrawSig = 'hogsdraw.log'

function Say($msg, $color = 'Gray') { Write-Host $msg -ForegroundColor $color }
function Sha($path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
function HexBytes($s) { [byte[]]($s -split '(..)' -ne '' | ForEach-Object { [Convert]::ToByte($_, 16) }) }
function Has($path) { Test-Path -LiteralPath $path }
function Drop($path) { if (Has $path) { Remove-Item -LiteralPath $path -Force } }
function OwnDdraw($path) { (Has $path) -and [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($path)).Contains($ddrawSig) }
function Crlf($s) { ($s.TrimEnd() -replace '\r\n|\r|\n', "`r`n") + "`r`n" }
# the text before $pos may end without a line end
function NewLine($text, $pos) { if ($pos -eq 0 -or $text[$pos - 1] -match '[\r\n]') { '' } else { "`r`n" } }

# Writes warhogs_.exe.new: the exe with one change list of the JSON applied, checked against output_sha256.
# The exe itself is replaced later (step 3).
function PatchedExe($changes) {
    $bytes = [IO.File]::ReadAllBytes($exe)
    foreach ($c in $changes) {
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
    $tmp
}

# Mirrors GetPrivateProfile*, which the game reads hogs.ini with: only the first [name] counts, names ignore case and
# blanks (PowerShell dictionaries do too), a ;-line is a comment, a lone CR ends a line. End is after the last non-blank
# line: new settings land above the blank line before the next section. Text: the setting's line and the comments above it.
function IniSections($text) {
    $secs = [ordered]@{}
    $cur = $null; $from = 0
    foreach ($m in [regex]::Matches($text, '[^\r\n]*(?:\r\n|\r|\n)|[^\r\n]+$')) {
        $end = $m.Index + $m.Length
        if ($m.Value -match '^[ \t]*\[([^\]\r\n]*)') {
            $name = $Matches[1].Trim(" `t")
            $cur = $null
            if (-not $secs.Contains($name)) {
                $cur = @{ Name = $name; Start = $m.Index; Settings = [ordered]@{} }
                $secs[$name] = $cur
            }
            $from = $end
        } elseif ($cur -and $m.Value -match '^[ \t]*([^;\s=][^=\r\n]*)=') {
            $key = $Matches[1].Trim(" `t")
            if (-not $cur.Settings.Contains($key)) {
                $cur.Settings[$key] = @{ Name = $key; Text = $text.Substring($from, $end - $from) }
            }
            $from = $end
        }
        if ($cur -and $m.Value -match '\S') { $cur.End = $end }
    }
    $secs
}

if (-not $GameDir) {
    $GameDir = Split-Path $here -Parent
    if (-not (Has (Join-Path $GameDir $patch.file))) { $GameDir = $here }
}
# absolute: cmdlets and [IO.File] resolve a relative path against different folders
$GameDir = (Resolve-Path -LiteralPath $GameDir).ProviderPath
$exe = Join-Path $GameDir $patch.file
if (-not (Has $exe)) { throw "$($patch.file) not found in '$GameDir'. Put this folder inside the game folder or pass -GameDir." }
Say "Game folder: $GameDir"
# a running game holds warhogs_.exe and our DLLs open
if (Get-Process -Name warhogs_ -ErrorAction SilentlyContinue) { throw 'Hogs of War is running. Close it and run this again.' }

$exeOrig = "$exe.orig"
$winmm = Join-Path $GameDir 'winmm.dll'
$dd = Join-Path $GameDir 'ddraw.dll'
$hogsDll = Join-Path $GameDir 'hogs.dll'
$marker = Join-Path $GameDir $dgvMarker
$russian = Join-Path $here 'russian.ps1'
$hash = Sha $exe
$older = $patch.older_outputs | Where-Object { $_.sha256 -eq $hash } | Select-Object -First 1

if ($Restore) {
    $origOk = (Has $exeOrig) -and (Sha $exeOrig) -eq $patch.input_sha256
    if ($origOk) {
        Move-Item -LiteralPath $exeOrig -Destination $exe -Force
        Say 'warhogs_.exe restored' 'Green'
    } elseif ($hash -eq $patch.output_sha256 -or $older) {
        # the patched exe needs ddraw.dll (or hogs.dll): remove nothing while there is no original to go back to
        throw 'warhogs_.exe.orig is missing or is not the original. In Steam: game > Properties > Installed Files > Verify integrity of game files, then run -Restore again.'
    } elseif ($hash -eq $patch.input_sha256) {
        Drop $exeOrig
    } else {
        Say 'warhogs_.exe is not a version this fix knows: left as it is' 'Yellow'
    }
    # english.orig: the English files, put away by russian.ps1
    if ((Has (Join-Path $GameDir 'english.orig')) -and (Has $russian)) {
        try { & $russian -Remove -GameDir $GameDir } catch { Say "The Russian translation was not removed: $($_.Exception.Message)" 'Yellow' }
    }
    if (Has "$winmm.orig") {
        Move-Item -LiteralPath "$winmm.orig" -Destination $winmm -Force
        Drop (Join-Path $GameDir 'winmm.ini')
        Say 'winmm.dll restored' 'Green'
    }
    # hogs.dll is from v1.0-v1.1; *.new is left by a run that failed before moving it into place
    foreach ($f in 'hogs.dll', 'hogs.ini', 'hogs.log', 'hogsdraw.log', 'warhogs_.exe.new', 'ddraw.dll.new', 'winmm.dll.new') { Drop (Join-Path $GameDir $f) }
    if (Has $marker) {
        # dgVoodoo2 of an older patcher (its DDraw.dll may already be ours)
        foreach ($f in $dgvFiles + $dgvMarker) { Drop (Join-Path $GameDir $f) }
        Say 'dgVoodoo2 installed by an older version removed' 'Green'
    } elseif (OwnDdraw $dd) {
        Drop $dd
        Say 'ddraw.dll removed' 'Green'
    }
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
foreach ($f in 'ddraw.dll', 'hogs.ini', 'winmm.dll', 'winmm.ini') {
    if (-not (Has (Join-Path $files $f))) { throw "files\$f is missing. Extract the whole zip, then run patch.cmd again." }
}
# A hand-installed dgVoodoo2 (no marker) is not ours to delete: it is kept as *.orig.
$keep = @()
if ((Has $dd) -and -not (OwnDdraw $dd) -and -not (Has $marker)) {
    $keep = @($dgvFiles | Where-Object { Has (Join-Path $GameDir $_) })
    foreach ($f in $keep) {
        if (Has (Join-Path $GameDir "$f.orig")) { throw "$f.orig is in the way. Move it away and run patch.cmd again; nothing was changed." }
    }
}

# 1. warhogs_.exe: remove LaserLock, load the fixes from ddraw.dll. The new exe is only made here, as warhogs_.exe.new;
#    it takes its place in step 3, once our ddraw.dll is there.
$tmp = $null
if ($hash -eq $patch.output_sha256) {
    Say 'warhogs_.exe is already patched'
} elseif ($older) {
    # made by an older version: its own changes turn it into the current exe, without warhogs_.exe.orig
    $tmp = PatchedExe $older.changes
} elseif ($hash -ne $patch.input_sha256) {
    throw "Unknown warhogs_.exe (SHA-256 $hash). Supported: $($patch.game), SHA-256 $($patch.input_sha256)."
} else {
    # keep the original; replace a missing or damaged copy (an interrupted earlier run)
    if (-not ((Has $exeOrig) -and (Sha $exeOrig) -eq $patch.input_sha256)) {
        Copy-Item -LiteralPath $exe -Destination $exeOrig -Force
        if ((Sha $exeOrig) -ne $patch.input_sha256) { throw 'Could not copy the original warhogs_.exe; nothing was changed.' }
    }
    $tmp = PatchedExe $patch.changes
}

# 2. ddraw.dll: the fixes and the Direct3D 11 renderer. Every exe this step can meet starts with it: the original, the
#    current one, and the v1.0-v1.1 one together with its hogs.dll.
foreach ($f in $keep) { Move-Item -LiteralPath (Join-Path $GameDir $f) -Destination (Join-Path $GameDir "$f.orig") }
if ($keep) {
    Say "Found dgVoodoo2/ddraw files, renamed to *.orig (patch.cmd -Restore puts them back): $($keep -join ', ')" 'Yellow'
    Say 'The fixed game needs the ddraw.dll of this fix: with another one (dgVoodoo2) it does not start.' 'Yellow'
}
# complete next to it before it takes its place: an interrupted copy must not leave a broken ddraw.dll
Copy-Item -LiteralPath (Join-Path $files 'ddraw.dll') -Destination "$dd.new" -Force
Move-Item -LiteralPath "$dd.new" -Destination $dd -Force
if (Has $marker) {
    # of an older patcher: DDraw.dll was just replaced, the rest is not needed
    foreach ($f in $dgvSiblings + $dgvMarker) { Drop (Join-Path $GameDir $f) }
    Say 'dgVoodoo2 installed by an older version removed' 'Green'
}
Say 'ddraw.dll installed (the fixes and the Direct3D 11 renderer, settings: hogs.ini)' 'Green'

# 3. the new exe: it imports ddraw.dll, which is ours now
if ($tmp) {
    Move-Item -LiteralPath $tmp -Destination $exe -Force
    if ($older) { Say 'warhogs_.exe updated: it loads the fixes from ddraw.dll now' 'Green' }
    else { Say "warhogs_.exe patched: $($patch.changes.Count) changes (original kept as warhogs_.exe.orig)" 'Green' }
}

# 4. hogs.dll (v1.0-v1.1): no exe imports it any more
if (Has $hogsDll) {
    Drop $hogsDll
    Say 'hogs.dll removed (its fixes are in ddraw.dll now)' 'Green'
}

# 5. hogs.ini is the player's: keep it, and add what a newer version needs.
$ini = Join-Path $GameDir 'hogs.ini'
if (-not (Has $ini)) {
    Copy-Item -LiteralPath (Join-Path $files 'hogs.ini') -Destination $ini
} else { try {
    $b = [IO.File]::ReadAllBytes($ini)
    if ($b.Length -ge 2 -and (($b[0] -eq 0xFF -and $b[1] -eq 0xFE) -or ($b[0] -eq 0xFE -and $b[1] -eq 0xFF))) {
        Say 'hogs.ini is saved as UTF-16 and was not updated: copy new sections and settings from files\hogs.ini into it by hand' 'Yellow'
    } else {
        # Insertions only, so the player's text, values and encoding stay as they are: a missing section goes to the end of
        # the file, a missing setting to the end of its section.
        $latin1 = [Text.Encoding]::GetEncoding(28591)   # one byte per char, so nothing is re-encoded
        $text = $latin1.GetString($b)
        $tplText = $latin1.GetString([IO.File]::ReadAllBytes((Join-Path $files 'hogs.ini')))
        $mine = IniSections $text
        $sections = @(); $settings = @(); $inserts = @(); $tail = ''
        foreach ($e in (IniSections $tplText).GetEnumerator()) {
            $t = $e.Value
            if (-not $mine.Contains($e.Key)) {
                $sections += "[$($t.Name)]"
                $tail += "`r`n" + (Crlf $tplText.Substring($t.Start, $t.End - $t.Start))
                continue
            }
            $add = ''; $names = @()
            foreach ($s in $t.Settings.GetEnumerator()) {
                if (-not $mine[$e.Key].Settings.Contains($s.Key)) { $add += Crlf $s.Value.Text; $names += $s.Value.Name }
            }
            if ($add) {
                $inserts += @{ Pos = $mine[$e.Key].End; Text = $add }
                $settings += "$($names -join ', ') in [$($t.Name)]"
            }
        }
        # last position first, so the earlier ones stay valid
        foreach ($i in ($inserts | Sort-Object { $_.Pos } -Descending)) {
            $text = $text.Insert($i.Pos, (NewLine $text $i.Pos) + $i.Text)
        }
        if ($tail) { $text += (NewLine $text $text.Length) + $tail }
        if ($sections -or $settings) {
            [IO.File]::WriteAllBytes($ini, $latin1.GetBytes($text))
            $what = @()
            if ($sections) { $what += 'the sections ' + ($sections -join ', ') }
            if ($settings) { $what += 'the settings ' + ($settings -join '; ') }
            Say "hogs.ini: added $($what -join ' and ') (your settings are unchanged)" 'Green'
        }
    }
} catch {
    # the file is optional (a missing setting has its default): the steps below must still run
    Say "hogs.ini was not updated ($($_.Exception.Message)): new settings keep their defaults" 'Yellow'
} }

# 6. CD music: replace the ogg-winmm build that hangs the game on start
if ((Has $winmm) -and (Sha $winmm) -eq $badWinmm) {
    # the new file is complete before the old one moves away: a failed copy must not leave the game without winmm.dll
    Copy-Item -LiteralPath (Join-Path $files 'winmm.dll') -Destination "$winmm.new" -Force
    Move-Item -LiteralPath $winmm -Destination "$winmm.orig" -Force
    Move-Item -LiteralPath "$winmm.new" -Destination $winmm
    Copy-Item -LiteralPath (Join-Path $files 'winmm.ini') -Destination $GameDir -Force
    Say 'winmm.dll replaced with a working ogg-winmm build (old one kept as winmm.dll.orig)' 'Green'
}

# 7. The Russian translation (russian.ps1, a separate download) is offered on a PC with Russian as display language,
#    to a person who can answer (not with input from a file or nul: a script runs this), and again when russian.ps1
#    holds a newer release than the game folder has.
$ruMark = Join-Path $GameDir 'english.orig\version.txt'
$ruHave = ''
if (Has $ruMark) { $ruHave = "$(Get-Content -LiteralPath $ruMark -TotalCount 1)".Trim() }
$ruWant = ''
if ((Has $russian) -and (Get-Content -LiteralPath $russian -Raw) -match '(?m)^[$]version = ''([^'']+)''') { $ruWant = $Matches[1] }
if ($ruHave -and $ruWant -and $ruHave -ne $ruWant) {
    Say "A newer Russian translation can be installed ($ruWant; the game has $ruHave): run russian.cmd" 'Yellow'
}
if ($ruWant -and $ruHave -ne $ruWant -and (Get-UICulture).TwoLetterISOLanguageName -eq 'ru' -and -not [Console]::IsInputRedirected) {
    # The texts are Russian, as escapes because this file is ASCII: "The language of Windows is Russian. Download and
    # install the Russian translation of the game (text and voices, 330 MB)?", "1 = yes, 2 = no", "The translation was
    # not installed. To install it later: russian.cmd"
    Say ''
    Say ([regex]::Unescape('\u042f\u0437\u044b\u043a Windows \u0440\u0443\u0441\u0441\u043a\u0438\u0439. \u0421\u043a\u0430\u0447\u0430\u0442\u044c \u0438 \u043f\u043e\u0441\u0442\u0430\u0432\u0438\u0442\u044c \u0440\u0443\u0441\u0441\u043a\u0438\u0439 \u043f\u0435\u0440\u0435\u0432\u043e\u0434 \u0438\u0433\u0440\u044b (\u0442\u0435\u043a\u0441\u0442 \u0438 \u043e\u0437\u0432\u0443\u0447\u043a\u0430, 330 \u041c\u0411)?')) 'Cyan'
    $answer = Read-Host ([regex]::Unescape('1 = \u0434\u0430, 2 = \u043d\u0435\u0442'))
    if ("$answer".Trim() -eq '1') {
        try { & $russian -GameDir $GameDir } catch { Say "The Russian translation was not installed: $($_.Exception.Message)" 'Yellow' }
    } else {
        Say ([regex]::Unescape('\u041f\u0435\u0440\u0435\u0432\u043e\u0434 \u043d\u0435 \u043f\u043e\u0441\u0442\u0430\u0432\u043b\u0435\u043d. \u041f\u043e\u0441\u0442\u0430\u0432\u0438\u0442\u044c \u0435\u0433\u043e \u043f\u043e\u0437\u0436\u0435: russian.cmd'))
    }
}

Say 'Done. Start the game as usual.' 'Green'
