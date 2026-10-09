@echo off
rem Re-enables all windows that the unpatched Hogs of War left disabled after a crash.
rem Asks for admin once (UAC) so windows of elevated programs are fixed too.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File \"%~dp0enable-windows.ps1\"'"
