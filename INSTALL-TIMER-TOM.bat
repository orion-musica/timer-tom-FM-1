@echo off
rem Timer Tom - one-click local build and install (Windows).
rem Builds the firmware, then opens the web installer on http://localhost:8766
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Install-Local.ps1" %*
if errorlevel 1 pause
