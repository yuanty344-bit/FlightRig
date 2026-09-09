[CmdletBinding()]
param(
    [switch]$SkipSmokeTests,
    [switch]$CleanBuild
)

$ErrorActionPreference = 'Stop'
$sourceDirectory = Split-Path -Parent $PSScriptRoot
$outputDirectory = Split-Path -Parent $sourceDirectory
$buildDirectory = Join-Path $sourceDirectory 'build-msvc'
$releaseDirectory = Join-Path $outputDirectory 'release'
$stagingDirectory = Join-Path $outputDirectory 'release.new'
$cmakeListsContent = Get-Content -Raw -LiteralPath (Join-Path $sourceDirectory 'CMakeLists.txt')
$versionMatch = [regex]::Match(
    $cmakeListsContent,
    'project\s*\(\s*FlightBench\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)',
    [Text.RegularExpressions.RegexOptions]::IgnoreCase
)
if (-not $versionMatch.Success) {
    throw 'Could not read the FlightBench version from CMakeLists.txt.'
}
$projectVersion = $versionMatch.Groups[1].Value
$archivePath = Join-Path $outputDirectory "FlightBench-$projectVersion-win64.zip"

function Read-CMakeCacheValue {
    param([string]$Name)
    $cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath)) { return $null }
    $entry = Select-String -LiteralPath $cachePath -Pattern ("^" + [regex]::Escape($Name) + ":[^=]+=(.*)$") | Select-Object -First 1
    if ($entry) { return $entry.Matches[0].Groups[1].Value }
    return $null
}

function Import-VisualStudioEnvironment {
    $vcvars = $null
    if ($env:VSINSTALLDIR) {
        $candidate = Join-Path $env:VSINSTALLDIR 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path -LiteralPath $candidate) { $vcvars = $candidate }
    }
    if (-not $vcvars) {
        $cachedCompiler = Read-CMakeCacheValue 'CMAKE_CXX_COMPILER'
        if ($cachedCompiler -and $cachedCompiler -match '^(.*?)[\\/]VC[\\/]Tools[\\/]') {
            $candidate = Join-Path $Matches[1] 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path -LiteralPath $candidate) { $vcvars = $candidate }
        }
    }
    if (-not $vcvars) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($installation) {
                $candidate = Join-Path $installation.Trim() 'VC\Auxiliary\Build\vcvars64.bat'
                if (Test-Path -LiteralPath $candidate) { $vcvars = $candidate }
            }
        }
    }
    if (-not $vcvars) {
        throw 'MSVC x64 was not found. Install the Visual Studio 2022 Desktop development with C++ workload.'
    }
    $environmentLines = & cmd.exe /d /c ('"' + $vcvars + '" >nul && set')
    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), 'Process')
        }
    }
}

function Resolve-QtRoot {
    foreach ($candidate in @($env:FLIGHTBENCH_QT_ROOT, $env:QT_ROOT)) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'bin\windeployqt.exe'))) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $qt6Directory = Read-CMakeCacheValue 'Qt6_DIR'
    if ($qt6Directory) {
        $candidate = [IO.Path]::GetFullPath((Join-Path $qt6Directory '..\..\..'))
        if (Test-Path -LiteralPath (Join-Path $candidate 'bin\windeployqt.exe')) { return $candidate }
    }
    $qmake = Get-Command qmake.exe -ErrorAction SilentlyContinue
    if ($qmake) {
        $candidate = (& $qmake.Source -query QT_INSTALL_PREFIX).Trim()
        if (Test-Path -LiteralPath (Join-Path $candidate 'bin\windeployqt.exe')) { return $candidate }
    }
    throw 'Qt MSVC 64-bit was not found. Configure the project in Qt Creator or set FLIGHTBENCH_QT_ROOT.'
}

function Resolve-MavsdkConfig {
    foreach ($candidate in @($env:MAVSDK_DIR,
                             $(if ($env:MAVSDK_ROOT) { Join-Path $env:MAVSDK_ROOT 'lib\cmake\MAVSDK' }),
                             (Read-CMakeCacheValue 'MAVSDK_DIR'),
                             (Join-Path $sourceDirectory 'third_party\MAVSDK\lib\cmake\MAVSDK'))) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'MAVSDKConfig.cmake'))) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'MAVSDK 3.17.x was not found. Set MAVSDK_DIR or MAVSDK_ROOT.'
}

function Resolve-CMake {
    $cached = Read-CMakeCacheValue 'CMAKE_COMMAND'
    if ($cached -and (Test-Path -LiteralPath $cached)) { return $cached }
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $qtBase = Split-Path -Parent (Split-Path -Parent $script:qtRoot)
    $bundled = Get-ChildItem -LiteralPath (Join-Path $qtBase 'Tools') -Filter cmake.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($bundled) { return $bundled.FullName }
    throw 'CMake was not found. Install CMake with the Qt Maintenance Tool.'
}

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    Write-Host ('> ' + $Program + ' ' + ($Arguments -join ' ')) -ForegroundColor DarkGray
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE." }
}

function Write-Stage {
    param([string]$Message)
    Write-Host ''
    Write-Host ("==> " + $Message) -ForegroundColor Cyan
}

$runningFlightBench = Get-Process -Name 'FlightBench' -ErrorAction SilentlyContinue
if ($runningFlightBench) {
    throw 'FlightBench is still running. Close the application, then run publish-release.cmd again.'
}

Write-Stage 'Checking the MSVC, Qt, MAVSDK, CMake and Ninja toolchain'
Import-VisualStudioEnvironment
$qtRoot = Resolve-QtRoot
$mavsdkDirectory = Resolve-MavsdkConfig
$cmake = Resolve-CMake
$qtBin = Join-Path $qtRoot 'bin'
$env:PATH = $qtBin + ';' + $env:PATH

$ninja = Read-CMakeCacheValue 'CMAKE_MAKE_PROGRAM'
if ($ninja -and (Test-Path -LiteralPath $ninja)) {
    $env:PATH = (Split-Path -Parent $ninja) + ';' + $env:PATH
}

Write-Stage "Configuring FlightBench $projectVersion"
Invoke-Checked $cmake @('-S', $sourceDirectory, '-B', $buildDirectory, '-G', 'Ninja',
                        '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$qtRoot",
                        "-DMAVSDK_DIR=$mavsdkDirectory", '-DBUILD_TESTING=ON')

Write-Stage $(if ($CleanBuild) { 'Performing a clean release build' } else { 'Building changed release files' })
$buildArguments = @('--build', $buildDirectory, '--config', 'Release')
if ($CleanBuild) { $buildArguments += '--clean-first' }
$buildArguments += @('--target',
                    'FlightBench', 'FlightBenchRouteLibraryTest', 'FlightBenchTestPlanRepositoryTest',
                    'FlightBenchReplayTest',
                    'FlightBenchSessionRecorderTest', 'FlightBenchRouteAnalysisTest',
                    'FlightBenchWorldMapTest', 'FlightBenchTestWorkflowTest',
                    'FlightBenchSensorServiceTest', 'FlightBenchLidarMappingTest',
                    'FlightBenchDepthMappingTest', 'FlightBenchVisualSurveyTest',
                    'FlightBenchGimbalInspectionTest')
Invoke-Checked $cmake $buildArguments

if (-not $SkipSmokeTests) {
Write-Stage 'Running unit and interface smoke tests'
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    Invoke-Checked $ctest @('--test-dir', $buildDirectory, '-C', 'Release', '--output-on-failure',
                            '-E', 'FlightBench\.Sitl')
}

Write-Stage 'Creating a clean Windows release directory'

if ((Split-Path -Parent $releaseDirectory) -ne $outputDirectory -or
    (Split-Path -Leaf $releaseDirectory) -ne 'release') {
    throw "Refusing to clean an unexpected release path: $releaseDirectory"
}
if (Test-Path -LiteralPath $stagingDirectory) { Remove-Item -LiteralPath $stagingDirectory -Recurse -Force }
New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $buildDirectory 'FlightBench.exe') -Destination $stagingDirectory
Copy-Item -LiteralPath (Join-Path $buildDirectory 'mavsdk.dll') -Destination $stagingDirectory
Copy-Item -LiteralPath (Join-Path $sourceDirectory 'simulation') -Destination $stagingDirectory -Recurse

# ROS 2 sensor support remains optional and is deliberately not built while
# publishing: a normal x500 release must not require Docker or network access.
# Ship the tiny image recipe instead, so a recipient who wants LiDAR / camera
# data can install the matching 0.12.0 image directly from the release folder.
$releaseScriptsDirectory = Join-Path $stagingDirectory 'scripts'
$releaseDockerDirectory = Join-Path $stagingDirectory 'docker\ros-sensor-bridge'
New-Item -ItemType Directory -Path $releaseScriptsDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $releaseDockerDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceDirectory 'scripts\build-ros-sensor-image.cmd') `
          -Destination $releaseScriptsDirectory
Copy-Item -LiteralPath (Join-Path $sourceDirectory 'docker\ros-sensor-bridge\Dockerfile') `
          -Destination $releaseDockerDirectory
Copy-Item -LiteralPath (Join-Path $sourceDirectory 'docker\ros-sensor-bridge\entrypoint.sh') `
          -Destination $releaseDockerDirectory
Copy-Item -LiteralPath (Join-Path $sourceDirectory 'docker\ros-sensor-bridge\sensor_gateway.py') `
          -Destination $releaseDockerDirectory
Copy-Item -LiteralPath (Join-Path $sourceDirectory '.dockerignore') `
          -Destination $stagingDirectory

@(
    'FlightBench 0.12.0 optional ROS 2 sensor service',
    '',
    'Normal PX4 simulation does not require this service.',
    'For x500 LiDAR, Mono Camera, Depth or Gimbal sensor data:',
    '1. Install and start Docker Desktop.',
    '2. Double-click scripts\build-ros-sensor-image.cmd once.',
    '3. In FlightBench, enable ROS 2 sensor data before starting simulation.',
    '',
    'The installer builds flightbench/ros-sensor-bridge:0.12.0 locally.',
    'It does not start a ROS container until the user enables it in FlightBench.'
) | Set-Content -LiteralPath (Join-Path $stagingDirectory 'ROS2-SENSOR-SETUP.txt') -Encoding UTF8

$windeployqt = Join-Path $qtBin 'windeployqt.exe'
Invoke-Checked $windeployqt @('--release', '--compiler-runtime', '--qmldir', (Join-Path $sourceDirectory 'qml'),
                              '--translations', 'zh_CN', '--skip-plugin-types', 'qmltooling',
                              (Join-Path $stagingDirectory 'FlightBench.exe'))

@(
    "FlightBench $projectVersion",
    'Windows x64 / MSVC 2022',
    'Qt 6.11.x',
    'MAVSDK 3.17.x',
    ('Built: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz'))
) | Set-Content -LiteralPath (Join-Path $stagingDirectory 'build-info.txt') -Encoding UTF8

if (Test-Path -LiteralPath $releaseDirectory) { Remove-Item -LiteralPath $releaseDirectory -Recurse -Force }
Move-Item -LiteralPath $stagingDirectory -Destination $releaseDirectory
if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
Write-Stage 'Compressing the shareable archive'
Compress-Archive -Path (Join-Path $releaseDirectory '*') -DestinationPath $archivePath -CompressionLevel Optimal

Write-Host "Release ready: $releaseDirectory" -ForegroundColor Green
Write-Host "Shareable archive: $archivePath" -ForegroundColor Green
