@echo off
rem Hogs of War fix: double-click to install, or run "patch.cmd -Restore" to undo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch.ps1" %*
pause
