@echo off
setlocal
cd /d "%~dp0.."
echo Building flightrig/ros-sensor-bridge:0.13.0 ...
docker build --tag flightrig/ros-sensor-bridge:0.13.0 --file docker\ros-sensor-bridge\Dockerfile .
if errorlevel 1 (
  echo Build failed. Check Docker Desktop and the messages above.
  pause
  exit /b 1
)
echo ROS sensor image is ready.
pause
