@echo off
rem Opens the setup window (setup\Setup.ps1). Nothing to install first: Windows has what it needs.
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0setup\Setup.ps1" %*
if errorlevel 1 pause
