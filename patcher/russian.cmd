@echo off
setlocal
rem Hogs of War fix: double-click to download and install the Russian translation of the game (text and voices,
rem about 330 MB), or run "russian.cmd -Remove" to put the English files back. Install the fix first: patch.cmd.
rem If this folder is not inside the game folder: russian.cmd -GameDir "D:\Games\Hogs of War"
rem Started from a PowerShell 7 prompt, the module path it passes on breaks Windows PowerShell 5.1 (Get-FileHash is "not recognized").
set "PSModulePath="
if not exist "%~dp0russian.ps1" (
    echo russian.ps1 is missing next to russian.cmd: extract the whole zip first, then run russian.cmd again.
    pause
    exit /b 1
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0russian.ps1" %*
pause
