@echo off
setlocal
cd /d "%~dp0.."
echo Building flightbench/ros-sensor-bridge:0.12.0 ...
docker build --tag flightbench/ros-sensor-bridge:0.12.0 --file docker\ros-sensor-bridge\Dockerfile .
if errorlevel 1 (
  echo Build failed. Check Docker Desktop and the messages above.
  pause
  exit /b 1
)
echo ROS sensor image is ready.
pause
