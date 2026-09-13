@echo off
setlocal
title FlightRig Release Publisher
cd /d "%~dp0"

echo.
echo ============================================================
echo   FlightRig Release Publisher
echo ============================================================
echo.
echo This window will remain open after publishing finishes.
echo Close FlightRig before publishing. The first run may take a while.
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish-release.ps1" %*
set "publish_exit_code=%ERRORLEVEL%"

echo.
if not "%publish_exit_code%"=="0" (
    echo [FAILED] FlightRig could not be published.
    echo Read the error above, then press any key to close this window.
) else (
    echo [DONE] FlightRig release has been generated successfully.
    echo Press any key to close this window.
)
echo.
pause >nul
exit /b %publish_exit_code%
