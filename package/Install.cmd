@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*
if errorlevel 1 echo Installation stopped. Read the message above; do not overwrite existing mods blindly.
pause
