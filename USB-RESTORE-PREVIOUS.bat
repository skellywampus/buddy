@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if not errorlevel 1 (
    py -3 installer\launch_usb_update.py --restore-previous-app %*
    goto finished
)
where python >nul 2>nul
if not errorlevel 1 (
    python installer\launch_usb_update.py --restore-previous-app %*
    goto finished
)
echo Install Python 3.10 or newer from https://www.python.org/downloads/windows/
echo Enable the Python launcher or Add Python to PATH, then run this again.
cmd /c exit 1
:finished
set "buddy_result=%errorlevel%"
echo.
pause
exit /b %buddy_result%
