@echo off
REM VM Manager v6 launcher — used only when the user enables "auto start" in Settings.
cd /d "%~dp0"
start "VM Manager" /min cmd /c "npm start"
