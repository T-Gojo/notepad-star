@echo off
setlocal
set "APPDIR=%~dp0notepad-star-rust\dist\windows-x64-0.1.0-rc.6"
if not exist "%APPDIR%\notepad-star.exe" (
    echo The packaged application was not found in:
    echo "%APPDIR%"
    echo See notepad-star-rust\README.md for packaging instructions.
    pause
    exit /b 1
)
start "" /D "%APPDIR%" "%APPDIR%\notepad-star.exe"
