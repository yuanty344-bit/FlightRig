@echo off
setlocal
title FlightBench Release Publisher
cd /d "%~dp0"

echo.
echo ============================================================
echo   FlightBench Release Publisher
echo ============================================================
echo.
echo This window will remain open after publishing finishes.
echo Close FlightBench before publishing. The first run may take a while.
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish-release.ps1" %*
set "publish_exit_code=%ERRORLEVEL%"

echo.
if not "%publish_exit_code%"=="0" (
    echo [FAILED] FlightBench could not be published.
    echo Read the error above, then press any key to close this window.
) else (
    echo [DONE] FlightBench release has been generated successfully.
    echo Press any key to close this window.
)
echo.
pause >nul
exit /b %publish_exit_code%
