@echo off
rem Starts the game's launcher, once Setup.bat has built it.
if exist "%~dp0build\DOAX3Launcher.exe" (
    start "" "%~dp0build\DOAX3Launcher.exe"
) else (
    echo The game isn't built yet. Run Setup.bat first.
    pause
)
