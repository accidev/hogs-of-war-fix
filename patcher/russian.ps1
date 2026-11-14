# Hogs of War fix - the Russian translation: downloads it and puts it into the game folder.
#   russian.cmd                 download the translation and install it
#   russian.cmd -Remove         put the English files back (russian-remove.cmd does this)
#   russian.cmd -Zip "D:\Downloads\hogs-of-war-ru-1.1.1.zip"    install from a zip you downloaded yourself
#   russian.cmd -GameDir "D:\Games\Hogs of War"                 game folder (default: the folder above this one)
# The translation is not part of this fix: github.com/accidev/hogs-of-war-ru, the release named below, accepted only
# with the SHA-256 written here. It replaces about 1900 game data files; each goes first to <game folder>\english.orig,
# and -Remove moves them back. patch.cmd offers to run this, and patch.cmd -Restore removes the translation too.
# The only script of the fix that uses the network. Windows PowerShell 5.1; keep this file ASCII.
[CmdletBinding()] param([string]$GameDir, [string]$Zip, [switch]$Remove)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$version = '1.1.1'
$name = "hogs-of-war-ru-$version.zip"
$url = "https://github.com/accidev/hogs-of-war-ru/releases/download/v$version/$name"
$sha = 'B319AFC625668656E6CEA4E15A2B9E78BFA1CF8051709DADB09B60E9448A762A'
$folders = 'FEText', 'Language', 'Speech'
$types = '.wav', '.bmp', '.bik', '.tab', '.tim', '.bin', '.mad', '.ofs'

function Say($msg, $color = 'Gray') { Write-Host $msg -ForegroundColor $color }
function Sha($path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
function Has($path) { Test-Path -LiteralPath $path }
function Drop($path) { if (Has $path) { Remove-Item -LiteralPath $path -Force } }
function MakeFolderOf($path) { [void][IO.Directory]::CreateDirectory((Split-Path $path -Parent)) }

# The game folder path of a zip entry. Refuses a path outside the three folders or a file type the game does not read,
# so a zip cannot put a program into the game folder.
function Target($rel) {
    $rel = $rel -replace '/', '\'
    $parts = $rel.Split('\')
    if ($parts.Count -lt 2 -or $folders -notcontains $parts[0] -or $parts -contains '..' -or $parts -contains '' -or
        $rel.Contains(':') -or $types -notcontains [IO.Path]::GetExtension($rel).ToLowerInvariant()) {
        throw "The zip holds a file that is not game data: '$rel'. Nothing was changed."
    }
    Join-Path $GameDir $rel
}

function Download($from, $to) {
    Say "Downloading $from (about 330 MB)"
    Drop $to
    $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
    if (Has $curl) {
        # curl draws its progress on stderr: that is not an error
        $ErrorActionPreference = 'Continue'
        & $curl -L --fail --retry 3 -C - -o $to $from
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($code) { throw "curl exit code $code" }
    } else {
        # Windows older than 10 1803 has no curl.exe
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $from -OutFile $to -UseBasicParsing
    }
}

# warhogs_.exe: Steam; warhogs.exe: disc versions
$exeNames = 'warhogs_.exe', 'warhogs.exe'
function HasGame($dir) { [bool]($exeNames | Where-Object { Has (Join-Path $dir $_) }) }
if (-not $GameDir) {
    $GameDir = Split-Path $here -Parent
    if (-not (HasGame $GameDir)) { $GameDir = $here }
}
$GameDir = (Resolve-Path -LiteralPath $GameDir).ProviderPath
if (-not (HasGame $GameDir)) { throw "$($exeNames -join ' or ') not found in '$GameDir'. Put this folder inside the game folder or pass -GameDir." }
if (Get-Process -Name ($exeNames -replace '[.]exe$') -ErrorAction SilentlyContinue) { throw 'Hogs of War is running. Close it and run this again.' }

$bak = Join-Path $GameDir 'english.orig'
$added = Join-Path $bak 'added.txt'       # files the translation added: the game had none to keep
$mark = Join-Path $bak 'version.txt'      # written last: the translation is completely installed

if ($Remove) {
    if (-not (Has $bak)) { Say 'The Russian translation is not installed (there is no english.orig folder).'; return }
    $count = 0
    foreach ($f in Get-ChildItem -LiteralPath $bak -Recurse -File) {
        $rel = $f.FullName.Substring($bak.Length + 1)
        if ($rel -eq 'added.txt' -or $rel -eq 'version.txt') { continue }
        $to = Target $rel
        MakeFolderOf $to
        Move-Item -LiteralPath $f.FullName -Destination $to -Force
        $count++
    }
    if (Has $added) { foreach ($rel in Get-Content -LiteralPath $added) { if ($rel) { Drop (Target $rel) } } }
    Remove-Item -LiteralPath $bak -Recurse -Force
    Say "Russian translation removed: $count English files are back" 'Green'
    return
}

$fetched = $false
if ($Zip) {
    $Zip = (Resolve-Path -LiteralPath $Zip).ProviderPath
} else {
    $Zip = Join-Path $here $name
    if (-not ((Has $Zip) -and (Sha $Zip) -eq $sha)) {
        try { Download $url "$Zip.part" } catch {
            throw "The download failed ($($_.Exception.Message)). Check the connection and run russian.cmd again. Or download $url in a browser, put the file next to russian.cmd and run russian.cmd again."
        }
        if ((Sha "$Zip.part") -ne $sha) { Drop "$Zip.part"; throw 'The downloaded file has a wrong checksum and was deleted; nothing was changed. Run russian.cmd again.' }
        Move-Item -LiteralPath "$Zip.part" -Destination $Zip -Force
        $fetched = $true
    }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($Zip)
try {
    $entries = @($archive.Entries | Where-Object { $_.Name })   # a folder entry has no name
    if (-not $entries) { throw "'$Zip' is empty. Nothing was changed." }
    # everything that can refuse is checked before the first change
    $size = 0
    foreach ($e in $entries) { [void](Target $e.FullName); $size += $e.Length }
    $free = $null
    try { $free = (New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($GameDir))).AvailableFreeSpace } catch { }   # a network path has none
    if ($null -ne $free -and $free -lt $size + 50MB) {
        throw ('Not enough free disk space in the game folder: {0:N0} MB are needed. Nothing was changed.' -f (($size + 50MB) / 1MB))
    }
    Say "Installing the Russian translation: $($entries.Count) files"
    $new = @{}
    if (Has $added) { foreach ($rel in Get-Content -LiteralPath $added) { $new[$rel] = $true } }
    try {
        foreach ($e in $entries) {
            $rel = $e.FullName -replace '/', '\'
            $to = Target $rel
            $keep = Join-Path $bak $rel
            # the English file is kept once: a second run must not replace it with a Russian one
            if (-not (Has $keep) -and -not $new[$rel]) {
                MakeFolderOf $keep
                if (Has $to) { Move-Item -LiteralPath $to -Destination $keep }
                else { Add-Content -LiteralPath $added -Value $rel -Encoding ASCII; $new[$rel] = $true }
            }
            MakeFolderOf $to
            [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $to, $true)
        }
    } catch {
        throw "The installation stopped: $($_.Exception.Message) Run russian.cmd again to finish it, or russian-remove.cmd to go back to English."
    }
} finally { $archive.Dispose() }
# a zip the player brought says its release in its name
$installed = $version
if ((Split-Path $Zip -Leaf) -match '^hogs-of-war-ru-([0-9]+[.][0-9]+[.][0-9]+)[.]zip$') { $installed = $Matches[1] }
Set-Content -LiteralPath $mark -Value $installed -Encoding ASCII
if ($fetched) { Drop $Zip }
Say 'Russian translation installed (the English files are kept in english.orig; russian-remove.cmd puts them back)' 'Green'
