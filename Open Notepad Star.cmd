@echo off
setlocal
set "APPDIR=%~dp0dist\windows-x64-0.1.0-rc.6"
if not exist "%APPDIR%\notepad-star.exe" (
    echo The packaged application was not found in:
    echo "%APPDIR%"
    echo See docs\PACKAGING.md for packaging instructions.
    pause
    exit /b 1
)
start "" /D "%APPDIR%" "%APPDIR%\notepad-star.exe"
