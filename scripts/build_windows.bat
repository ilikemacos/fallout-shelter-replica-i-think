@echo off
setlocal
cd /d %~dp0\..
where py >nul 2>nul
if %errorlevel% neq 0 (
    echo Python 3 was not found. Install Python 3.11+ from python.org and retry.
    pause
    exit /b 1
)
py -3 -m pip install --upgrade pip pygame pyinstaller pillow || exit /b 1
py -3 scripts\build_windows.py
pause
