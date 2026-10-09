@echo off
setlocal
rem Hogs of War fix: double-click to install or update it, or run "patch.cmd -Restore" to undo it.
rem If this folder is not inside the game folder: patch.cmd -GameDir "D:\Games\Hogs of War"
rem Started from a PowerShell 7 prompt, the module path it passes on breaks Windows PowerShell 5.1 (Get-FileHash is "not recognized").
set "PSModulePath="
if not exist "%~dp0patch.ps1" (
    echo patch.ps1 is missing next to patch.cmd: extract the whole zip first, then run patch.cmd again.
    pause
    exit /b 1
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch.ps1" %*
pause
