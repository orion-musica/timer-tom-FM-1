@echo off
rem Timer Tom - open the website (and installer, if built) on http://localhost:8766
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Serve-Site.ps1" -Page "index.html"
if errorlevel 1 pause
