# Hogs of War fix - patcher. Works on the player's own copy of the game; ships no game code.
#
#   patch.cmd             install or update the fix
#   patch.cmd -Restore    put the original files back
#   russian.cmd           the Russian translation of the game (russian.ps1: a separate download, offered below)
#   patch.cmd -GameDir "D:\Games\Hogs of War"   game folder (default: the folder above this one)
#
# Windows PowerShell 5.1 compatible (ships with Windows 10/11). Keep this file ASCII.
# CmdletBinding makes an unknown switch (e.g. the old -NoDgVoodoo) an error instead of silently ignoring it.
#
# The patched warhogs_.exe imports CallDLL from ddraw.dll, so it starts only with our ddraw.dll (the one of Windows has
# no CallDLL). The exe of v1.0-v1.1 imports it from hogs.dll and starts only with that file. So that a failed or
# interrupted run always leaves a game that starts: our ddraw.dll is in place before such an exe is, hogs.dll goes
# only once no exe imports it, and -Restore puts the original exe back before it removes ddraw.dll.
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
# what is inserted into hogs.ini is CRLF whatever the line ends of the template are
function Crlf($s) { ($s.TrimEnd() -replace '\r\n|\r|\n', "`r`n") + "`r`n" }
# the line before the insert position is the last one of the file and may have no line end
function NewLine($text, $pos) { if ($pos -eq 0 -or $text[$pos - 1] -match '[\r\n]') { '' } else { "`r`n" } }

# warhogs_.exe with one change list of the JSON applied, written next to it as warhogs_.exe.new and checked against
# output_sha256. The exe itself is replaced later, by a move of that file.
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

# The game reads hogs.ini with GetPrivateProfile*, so "this section / setting is there" has to mean what that API finds:
# only the first [name] of a name counts (a second header would be ignored), names ignore case and the blanks around them,
# a line starting with ; is a comment (a commented-out setting is not there), a lone CR also ends a line.
# $text is decoded as Latin-1 so that every byte survives the round trip.
# Returns name -> @{ Name; Start; End; Settings = key -> @{ Name; Text } } (PowerShell dictionaries ignore case, whatever
# the culture of the PC). End is after the last non-blank line, so new settings land above the blank line that separates
# the next section. Text is the setting's line together with the comment lines above it.
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
# an exe patched by an older version, with the changes that make it the current one
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
    # the Russian translation that russian.ps1 installed: the English files go back
    if ((Has (Join-Path $GameDir 'english.orig')) -and (Has $russian)) {
        try { & $russian -Remove -GameDir $GameDir } catch { Say "The Russian translation was not removed: $($_.Exception.Message)" 'Yellow' }
    }
    if (Has "$winmm.orig") {
        Move-Item -LiteralPath "$winmm.orig" -Destination $winmm -Force
        Drop (Join-Path $GameDir 'winmm.ini')
        Say 'winmm.dll restored' 'Green'
    }
    # hogs.dll held the fixes up to v1.1; a *.new file is left by a run that failed before it could move it into place
    foreach ($f in 'hogs.dll', 'hogs.ini', 'hogs.log', 'hogsdraw.log', 'warhogs_.exe.new', 'ddraw.dll.new', 'winmm.dll.new') { Drop (Join-Path $GameDir $f) }
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
foreach ($f in 'ddraw.dll', 'hogs.ini', 'winmm.dll', 'winmm.ini') {
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

# 1. warhogs_.exe: remove LaserLock, load the fixes from ddraw.dll. The new exe is only made here, as warhogs_.exe.new:
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

# 2. ddraw.dll: the fixes and the built-in Direct3D 11 renderer (it replaces dgVoodoo2). Every exe this step can find
#    starts with it: the original, the current one, and the one of v1.0-v1.1 together with its hogs.dll.
foreach ($f in $keep) { Move-Item -LiteralPath (Join-Path $GameDir $f) -Destination (Join-Path $GameDir "$f.orig") }
if ($keep) {
    Say "Found dgVoodoo2/ddraw files, renamed to *.orig (patch.cmd -Restore puts them back): $($keep -join ', ')" 'Yellow'
    Say 'The fixed game needs the ddraw.dll of this fix: with another one (dgVoodoo2) it does not start.' 'Yellow'
}
# complete next to it before it takes its place: an interrupted copy must not leave a broken ddraw.dll
Copy-Item -LiteralPath (Join-Path $files 'ddraw.dll') -Destination "$dd.new" -Force
Move-Item -LiteralPath "$dd.new" -Destination $dd -Force
if (Has $marker) {
    # installed by an older version of this patcher: DDraw.dll was just replaced, the rest is not needed
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

# 4. hogs.dll held the fixes up to v1.1. The exe imports it no longer.
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
        # Only insertions, so the player's text, values and encoding stay exactly as they are: a section the file lacks goes
        # to its end, a setting a section lacks goes to the end of that section (the game would use the default otherwise).
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
    # the file is optional (a missing setting has its default in the DLLs): the steps below must still run
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

# 7. The Russian translation is a separate download, made by russian.ps1. It is offered on a PC whose display language
#    is Russian, to a person who can answer: with the input taken from a file or from nul, a script runs this.
if ((Has $russian) -and -not (Has (Join-Path $GameDir 'english.orig\version.txt')) -and
    (Get-UICulture).TwoLetterISOLanguageName -eq 'ru' -and -not [Console]::IsInputRedirected) {
    # The texts are Russian, written as escapes because this file is ASCII: "The language of Windows is Russian.
    # Download and install the Russian translation of the game (text and voices, 330 MB)?", "1 = yes, 2 = no",
    # "The translation was not installed. To install it later: russian.cmd"
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
