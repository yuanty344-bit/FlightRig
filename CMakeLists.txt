cmake_minimum_required(VERSION 3.25)

project(FlightBench VERSION 0.12.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find MAVSDK from a developer-provided environment variable or an optional
# repository-local SDK.  Machine-specific drive letters belong in a local
# CMakeUserPresets.json or the environment, never in the shared project.
if(NOT MAVSDK_DIR)
    set(_flightbench_mavsdk_candidates
        "$ENV{MAVSDK_DIR}"
        "$ENV{MAVSDK_ROOT}/lib/cmake/MAVSDK"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/MAVSDK/lib/cmake/MAVSDK"
    )
    foreach(_flightbench_mavsdk_dir IN LISTS _flightbench_mavsdk_candidates)
        if(EXISTS "${_flightbench_mavsdk_dir}/MAVSDKConfig.cmake")
            set(MAVSDK_DIR "${_flightbench_mavsdk_dir}" CACHE PATH
                "Directory containing MAVSDKConfig.cmake")
            break()
        endif()
    endforeach()
    unset(_flightbench_mavsdk_candidates)
    unset(_flightbench_mavsdk_dir)
endif()

find_package(Qt6 6.11 REQUIRED COMPONENTS Quick Quick3D Svg Network)
find_package(MAVSDK 3.17 CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(FlightBench
    src/main.cpp
    src/app/appcontroller.cpp
    src/app/simulationruncoordinator.cpp
    src/app/simulationruncoordinator.h
    src/sessions/sensorsessionfinalizer.cpp
    src/sessions/sensorsessionfinalizer.h
    src/infrastructure/simulationcatalog.cpp
    src/infrastructure/applicationpreferences.cpp
    src/infrastructure/applicationpreferences.h
    src/infrastructure/runtimeinformation.cpp
    src/infrastructure/runtimeinformation.h
    src/infrastructure/simulationcatalog.h
    src/app/appcontroller.h
    src/infrastructure/storagepaths.h
    src/testing/acceptanceevaluator.cpp
    src/testing/acceptanceevaluator.h
    src/infrastructure/dockercontroller.cpp
    src/infrastructure/dockercontroller.h
    src/sessions/flightsessionrecorder.cpp
    src/sessions/sessionarchive.cpp
    src/sessions/sessionarchive.h
    src/sessions/flightsessionrecorder.h
    src/mapping/lidarmappingcontroller.cpp
    src/mapping/lidarmapoutput.cpp
    src/mapping/lidarmapoutput.h
    src/mapping/lidarmaptypes.h
    src/mapping/lidarmappingcontroller.h
    src/mapping/depthmappingcontroller.cpp
    src/mapping/depthsurfacegeometry.cpp
    src/mapping/depthmapexport.cpp
    src/mapping/depthmapreader.cpp
    src/mapping/depthmapreader.h
    src/mapping/depthvoxel.h
    src/mapping/depthmapexport.h
    src/mapping/depthsurfacegeometry.h
    src/mapping/depthvoxelcoordinates.h
    src/mapping/depthmappingcontroller.h
    src/inspection/visualsurveycontroller.cpp
    src/inspection/visualsurveycontroller.h
    src/inspection/gimbalinspectioncontroller.cpp
    src/inspection/gimbalinspectioncontroller.h
    src/mapping/flightpathgeometry.cpp
    src/mapping/flightpathgeometry.h
    src/mission/routelibrary.cpp
    src/mission/routelibrary.h
    src/testing/testplanrepository.cpp
    src/testing/testplanrepository.h
    src/sessions/replaycontroller.cpp
    src/sessions/replaydata.cpp
    src/sessions/replaydata.h
    src/sessions/replaycontroller.h
    src/sensors/sensorservicecontroller.cpp
    src/sensors/sensorsessionrecorder.cpp
    src/sensors/sensorsessionrecorder.h
    src/sensors/sensorsessionstorage.cpp
    src/sensors/sensorsessionstorage.h
    src/sensors/mjpegaviwriter.cpp
    src/sensors/mjpegaviwriter.h
    src/sensors/sensorservicecontroller.h
    src/sensors/sensorclocksync.h
    src/testing/preflightevaluator.cpp
    src/testing/preflightevaluator.h
    src/flight/px4eventtranslator.cpp
    src/flight/px4eventtranslator.h
    src/mission/routedeviationanalyzer.cpp
    src/mission/routedeviationanalyzer.h
    src/sessions/testreportgenerator.cpp
    src/sessions/testreportgenerator.h
    src/mapping/worldmapcatalog.cpp
    src/mapping/worldmapcatalog.h
    src/flight/vehicleconnection.cpp
    src/flight/vehicleconnection.h
)

# Opt-in console subsystem for sanitizer/debugger runs. Normal Windows builds
target_include_directories(FlightBench PRIVATE src)

# remain GUI applications and do not open a terminal window.
option(FLIGHTBENCH_CONSOLE_DIAGNOSTICS "Build FlightBench with a diagnostic console" OFF)
if(FLIGHTBENCH_CONSOLE_DIAGNOSTICS)
    set_target_properties(FlightBench PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

if(WIN32)
    set(FLIGHTBENCH_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/flightbench-app.ico")
    configure_file(resources/windows/FlightBench.rc.in
                   "${CMAKE_CURRENT_BINARY_DIR}/FlightBench.rc" @ONLY)
    target_sources(FlightBench PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/FlightBench.rc")
endif()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# This list is used only by the lint test.  Do not use CONFIGURE_DEPENDS here:
# on Windows it adds a recursive VerifyGlobs.cmake scan before every Ninja
# build, which needlessly delays normal editing and rebuilding.
file(GLOB_RECURSE FLIGHTBENCH_QML_LINT_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")

# City maps are generated assets. They are discovered on CMake configure, so a
# new world only needs one normal reconfigure and never edits this file.
file(GLOB FLIGHTBENCH_WORLD_MAP_ASSETS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "resources/worldmaps/*.svg"
    "resources/worldmaps/*.json"
)
set(FLIGHTBENCH_WORLD_MAP_JSON_ASSETS ${FLIGHTBENCH_WORLD_MAP_ASSETS})
list(FILTER FLIGHTBENCH_WORLD_MAP_JSON_ASSETS INCLUDE REGEX "\\.json$")

# Private relative-import helper, not a public JavaScript namespace of FlightBench.
# Exporting it through qmldir makes repeated module imports ambiguous in Qt.
set_source_files_properties(qml/features/mission/RouteMapPainter.js PROPERTIES
    QT_QML_SKIP_QMLDIR_ENTRY TRUE)

qt_add_qml_module(FlightBench
    URI FlightBench
    VERSION 1.0
    QML_FILES
        qml/Theme.qml
        qml/Main.qml
        qml/controls/ActionButton.qml
        qml/features/plans/AcceptanceResultRow.qml
        qml/controls/AppToolTip.qml
        qml/controls/CompactIconButton.qml
        qml/controls/DisclosureRow.qml
        qml/controls/DialogSurface.qml
        qml/controls/InsetScrollBar.qml
        qml/features/plans/HealthCheckRow.qml
        qml/controls/IconLink.qml
        qml/controls/MotionDialog.qml
        qml/controls/PanelCloseButton.qml
        qml/features/mission/MissionPlanner.qml
        qml/features/mission/WaypointMapMarker.qml
        qml/features/mission/MissionMapInteraction.qml
        qml/features/mission/RouteMapPainter.js
        qml/features/mission/RouteLibraryDialog.qml
        qml/features/mission/WaypointEditorPanel.qml
        qml/features/records/SessionSensorSummary.qml
        qml/controls/NumericField.qml
        qml/controls/SegmentedControl.qml
        qml/controls/StatusRow.qml
        qml/controls/SurfaceCard.qml
        qml/controls/ToastHost.qml
        qml/features/flight/TelemetryPanel.qml
        qml/features/scene3d/FlightScene3D.qml
        qml/features/records/ReplayAnalysisPanel.qml
        qml/features/records/ReplayControls.qml
        qml/features/records/ReplayEventPanel.qml
        qml/features/records/ReplayTelemetryChart.qml
        qml/features/mission/RouteDeviationSummary.qml
        qml/features/sensors/LidarPanel.qml
        qml/features/sensors/CameraPanel.qml
        qml/features/inspection/GimbalInspectionPanel.qml
        qml/features/sensors/DepthMapView3D.qml
        qml/features/inspection/VisualSurveyMap.qml
        qml/features/inspection/VisualSurveyDialog.qml
        qml/features/environment/RuntimeEnvironmentCenter.qml
        qml/features/environment/SimulationModelPicker.qml
        qml/features/environment/SimulationWorldPicker.qml
        qml/controls/SelectField.qml
        qml/features/settings/SettingsCenter.qml
        qml/features/settings/SettingsAppearancePage.qml
        qml/features/settings/SettingsInteractionPage.qml
        qml/features/settings/SettingsScenePage.qml
        qml/features/settings/SettingsPathPage.qml
        qml/features/settings/SettingsStoragePage.qml
        qml/features/settings/SettingsSoftwarePage.qml
        qml/features/records/SessionAcceptanceSection.qml
        qml/features/records/SessionDepthMapDialog.qml
        qml/features/settings/SettingsPageHeader.qml
        qml/features/settings/SettingsSectionLabel.qml
        qml/features/settings/SettingsCategoryRow.qml
        qml/features/settings/SettingsToggle.qml
        qml/features/settings/SettingsSwitchRow.qml
        qml/features/settings/SettingsChoiceRow.qml
        qml/features/settings/SettingsNumberRow.qml
        qml/features/settings/SettingsValueLine.qml
        qml/features/records/SessionEditDialog.qml
        qml/features/records/SessionFilterDialog.qml
        qml/features/records/SessionTrashDialog.qml
        qml/features/plans/PreflightSummary.qml
        qml/features/plans/TestPlanEditDialog.qml
        qml/features/plans/TestPlanPicker.qml
        qml/features/plans/TestResultSummary.qml
        qml/features/scene3d/WorldScene3D.qml
        qml/features/scene3d/GroundPositionMarker3D.qml
        qml/features/scene3d/DroneModel3D.qml
        qml/controls/ThemedIcon.qml
        qml/shell/AppShell.qml
        qml/shell/BottomNavigation.qml
        qml/shell/TopBar.qml
        qml/shell/UtilityDrawer.qml
        qml/pages/FlightPage.qml
        qml/pages/WorkbenchPage.qml
        qml/pages/RecordsPage.qml
)

qt_add_resources(FlightBench "flightbench_assets"
    PREFIX "/"
    BASE "resources"
    FILES
        resources/icons/circle-check.svg
        resources/icons/camera.svg
        resources/icons/circle-alert.svg
        resources/icons/chevron-down.svg
        resources/icons/copy.svg
        resources/icons/clipboard-check.svg
        resources/icons/flightbench-app.svg
        resources/icons/file-down.svg
        resources/icons/file-chart-column.svg
        resources/icons/ellipsis.svg
        resources/icons/folder-open.svg
        resources/icons/gauge.svg
        resources/icons/heart-pulse.svg
        resources/icons/keyboard.svg
        resources/icons/map-pin.svg
        resources/icons/map-pinned.svg
        resources/icons/maximize-2.svg
        resources/icons/plane-landing.svg
        resources/icons/plane-takeoff.svg
        resources/icons/plane.svg
        resources/icons/pencil.svg
        resources/icons/panel-right-open.svg
        resources/icons/play.svg
        resources/icons/refresh-cw.svg
        resources/icons/route.svg
        resources/icons/save.svg
        resources/icons/scroll-text.svg
        resources/icons/sliders-horizontal.svg
        resources/icons/square.svg
        resources/icons/trash-2.svg
        resources/icons/upload.svg
        resources/icons/video.svg
        resources/icons/zoom-in.svg
        resources/icons/zoom-out.svg
        resources/icons/locate-fixed.svg
        resources/icons/pause.svg
        resources/icons/octagon-pause.svg
        resources/icons/package.svg
        resources/icons/rotate-ccw.svg
        resources/icons/rocket.svg
        resources/icons/shield-check.svg
        resources/icons/triangle-alert.svg
        resources/icons/archive-restore.svg
        resources/icons/archive-x.svg
        resources/icons/history.svg
        resources/icons/box.svg
        resources/icons/database.svg
        resources/icons/info.svg
        resources/icons/mouse-pointer-2.svg
        resources/icons/palette.svg
        resources/icons/radar.svg
        resources/icons/x.svg
        resources/markers/position-marker.svg
        ${FLIGHTBENCH_WORLD_MAP_ASSETS}
        resources/licenses/LUCIDE_LICENSE
)

target_link_libraries(FlightBench PRIVATE
    Qt6::Quick
    Qt6::Quick3D
    Qt6::Svg
    Qt6::Network
    MAVSDK::mavsdk
)

target_compile_definitions(FlightBench PRIVATE
    FLIGHTBENCH_VERSION="${PROJECT_VERSION}"
    FLIGHTBENCH_MAVSDK_VERSION="3.17.1"
)

if(MSVC)
    # Qt 6.11 template/QML-cache internals trigger C4702 repeatedly under /W4.
    # It is not actionable application code and otherwise floods Qt Creator's
    # Problems pane with duplicate third-party warnings.
    target_compile_options(FlightBench PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_custom_command(TARGET FlightBench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:MAVSDK::mavsdk>
            $<TARGET_FILE_DIR:FlightBench>
    COMMENT "Copying MAVSDK runtime beside FlightBench"
)

add_custom_command(TARGET FlightBench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/simulation"
            "$<TARGET_FILE_DIR:FlightBench>/simulation"
    COMMENT "Copying simulation worlds beside FlightBench"
)

install(TARGETS FlightBench
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

add_executable(FlightBenchFlightTest EXCLUDE_FROM_ALL
    tests/flighttest.cpp
    src/app/appcontroller.cpp
    src/app/simulationruncoordinator.cpp
    src/app/simulationruncoordinator.h
    src/sessions/sensorsessionfinalizer.cpp
    src/sessions/sensorsessionfinalizer.h
    src/infrastructure/simulationcatalog.cpp
    src/infrastructure/applicationpreferences.cpp
    src/infrastructure/applicationpreferences.h
    src/infrastructure/runtimeinformation.cpp
    src/infrastructure/runtimeinformation.h
    src/infrastructure/simulationcatalog.h
    src/app/appcontroller.h
    src/infrastructure/storagepaths.h
    src/testing/acceptanceevaluator.cpp
    src/testing/acceptanceevaluator.h
    src/infrastructure/dockercontroller.cpp
    src/infrastructure/dockercontroller.h
    src/sessions/flightsessionrecorder.cpp
    src/sessions/sessionarchive.cpp
    src/sessions/sessionarchive.h
    src/sessions/flightsessionrecorder.h
    src/mapping/lidarmappingcontroller.cpp
    src/mapping/lidarmapoutput.cpp
    src/mapping/lidarmapoutput.h
    src/mapping/lidarmaptypes.h
    src/mapping/lidarmappingcontroller.h
    src/mapping/depthmappingcontroller.cpp
    src/mapping/depthsurfacegeometry.cpp
    src/mapping/depthmapexport.cpp
    src/mapping/depthmapreader.cpp
    src/mapping/depthmapreader.h
    src/mapping/depthvoxel.h
    src/mapping/depthmapexport.h
    src/mapping/depthsurfacegeometry.h
    src/mapping/depthvoxelcoordinates.h
    src/mapping/depthmappingcontroller.h
    src/inspection/visualsurveycontroller.cpp
    src/inspection/visualsurveycontroller.h
    src/inspection/gimbalinspectioncontroller.cpp
    src/inspection/gimbalinspectioncontroller.h
    src/mapping/flightpathgeometry.cpp
    src/mapping/flightpathgeometry.h
    src/mission/routelibrary.cpp
    src/mission/routelibrary.h
    src/testing/testplanrepository.cpp
    src/testing/testplanrepository.h
    src/sessions/replaycontroller.cpp
    src/sessions/replaydata.cpp
    src/sessions/replaydata.h
    src/sessions/replaycontroller.h
    src/sensors/sensorservicecontroller.cpp
    src/sensors/sensorsessionrecorder.cpp
    src/sensors/sensorsessionrecorder.h
    src/sensors/sensorsessionstorage.cpp
    src/sensors/sensorsessionstorage.h
    src/sensors/mjpegaviwriter.cpp
    src/sensors/mjpegaviwriter.h
    src/sensors/sensorservicecontroller.h
    src/testing/preflightevaluator.cpp
    src/testing/preflightevaluator.h
    src/flight/px4eventtranslator.cpp
    src/flight/px4eventtranslator.h
    src/mission/routedeviationanalyzer.cpp
    src/mission/routedeviationanalyzer.h
    src/sessions/testreportgenerator.cpp
    src/sessions/testreportgenerator.h
    src/mapping/worldmapcatalog.cpp
    src/mapping/worldmapcatalog.h
    src/flight/vehicleconnection.cpp
    src/flight/vehicleconnection.h
)

target_include_directories(FlightBenchFlightTest PRIVATE src)
target_link_libraries(FlightBenchFlightTest PRIVATE Qt6::Core Qt6::Gui Qt6::Quick3D Qt6::Network MAVSDK::mavsdk)

if(MSVC)
    target_compile_options(FlightBenchFlightTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_custom_command(TARGET FlightBenchFlightTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:MAVSDK::mavsdk>
            $<TARGET_FILE_DIR:FlightBenchFlightTest>
    COMMENT "Copying MAVSDK runtime beside FlightBenchFlightTest"
)

add_executable(FlightBenchMissionTest EXCLUDE_FROM_ALL
    tests/missiontest.cpp
    src/app/appcontroller.cpp
    src/app/simulationruncoordinator.cpp
    src/app/simulationruncoordinator.h
    src/sessions/sensorsessionfinalizer.cpp
    src/sessions/sensorsessionfinalizer.h
    src/infrastructure/simulationcatalog.cpp
    src/infrastructure/applicationpreferences.cpp
    src/infrastructure/applicationpreferences.h
    src/infrastructure/runtimeinformation.cpp
    src/infrastructure/runtimeinformation.h
    src/infrastructure/simulationcatalog.h
    src/app/appcontroller.h
    src/infrastructure/storagepaths.h
    src/testing/acceptanceevaluator.cpp
    src/testing/acceptanceevaluator.h
    src/infrastructure/dockercontroller.cpp
    src/infrastructure/dockercontroller.h
    src/sessions/flightsessionrecorder.cpp
    src/sessions/sessionarchive.cpp
    src/sessions/sessionarchive.h
    src/sessions/flightsessionrecorder.h
    src/mapping/lidarmappingcontroller.cpp
    src/mapping/lidarmapoutput.cpp
    src/mapping/lidarmapoutput.h
    src/mapping/lidarmaptypes.h
    src/mapping/lidarmappingcontroller.h
    src/mapping/depthmappingcontroller.cpp
    src/mapping/depthsurfacegeometry.cpp
    src/mapping/depthmapexport.cpp
    src/mapping/depthmapreader.cpp
    src/mapping/depthmapreader.h
    src/mapping/depthvoxel.h
    src/mapping/depthmapexport.h
    src/mapping/depthsurfacegeometry.h
    src/mapping/depthvoxelcoordinates.h
    src/mapping/depthmappingcontroller.h
    src/inspection/visualsurveycontroller.cpp
    src/inspection/visualsurveycontroller.h
    src/inspection/gimbalinspectioncontroller.cpp
    src/inspection/gimbalinspectioncontroller.h
    src/mapping/flightpathgeometry.cpp
    src/mapping/flightpathgeometry.h
    src/mission/routelibrary.cpp
    src/mission/routelibrary.h
    src/testing/testplanrepository.cpp
    src/testing/testplanrepository.h
    src/sessions/replaycontroller.cpp
    src/sessions/replaydata.cpp
    src/sessions/replaydata.h
    src/sessions/replaycontroller.h
    src/sensors/sensorservicecontroller.cpp
    src/sensors/sensorsessionrecorder.cpp
    src/sensors/sensorsessionrecorder.h
    src/sensors/sensorsessionstorage.cpp
    src/sensors/sensorsessionstorage.h
    src/sensors/mjpegaviwriter.cpp
    src/sensors/mjpegaviwriter.h
    src/sensors/sensorservicecontroller.h
    src/testing/preflightevaluator.cpp
    src/testing/preflightevaluator.h
    src/flight/px4eventtranslator.cpp
    src/flight/px4eventtranslator.h
    src/mission/routedeviationanalyzer.cpp
    src/mission/routedeviationanalyzer.h
    src/sessions/testreportgenerator.cpp
    src/sessions/testreportgenerator.h
    src/mapping/worldmapcatalog.cpp
    src/mapping/worldmapcatalog.h
    src/flight/vehicleconnection.cpp
    src/flight/vehicleconnection.h
)

target_include_directories(FlightBenchMissionTest PRIVATE src)
target_link_libraries(FlightBenchMissionTest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick3D
    Qt6::Network
    MAVSDK::mavsdk
)

if(MSVC)
    target_compile_options(FlightBenchMissionTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_custom_command(TARGET FlightBenchMissionTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:MAVSDK::mavsdk>
            $<TARGET_FILE_DIR:FlightBenchMissionTest>
    COMMENT "Copying MAVSDK runtime beside FlightBenchMissionTest"
)

add_executable(FlightBenchRouteLibraryTest EXCLUDE_FROM_ALL
    tests/routelibrarytest.cpp
    src/mission/routelibrary.cpp
    src/mission/routelibrary.h
    src/infrastructure/storagepaths.h
)

target_include_directories(FlightBenchRouteLibraryTest PRIVATE src)
target_link_libraries(FlightBenchRouteLibraryTest PRIVATE Qt6::Core)

if(MSVC)
    target_compile_options(FlightBenchRouteLibraryTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchTestPlanRepositoryTest EXCLUDE_FROM_ALL
    tests/testplanrepositorytest.cpp
    src/testing/testplanrepository.cpp
    src/testing/testplanrepository.h
    src/infrastructure/storagepaths.h
)

target_include_directories(FlightBenchTestPlanRepositoryTest PRIVATE src)
target_link_libraries(FlightBenchTestPlanRepositoryTest PRIVATE Qt6::Core)

if(MSVC)
    target_compile_options(FlightBenchTestPlanRepositoryTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchReplayTest EXCLUDE_FROM_ALL
    tests/replaycontrollertest.cpp
    src/sessions/replaycontroller.cpp
    src/sessions/replaydata.cpp
    src/sessions/replaydata.h
    src/sessions/replaycontroller.h
    src/mapping/flightpathgeometry.cpp
    src/mapping/flightpathgeometry.h
)

target_include_directories(FlightBenchReplayTest PRIVATE src)
target_link_libraries(FlightBenchReplayTest PRIVATE Qt6::Core Qt6::Gui Qt6::Quick3D)
if(MSVC)
    target_compile_options(FlightBenchReplayTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchSessionRecorderTest EXCLUDE_FROM_ALL
    tests/flightsessionrecordertest.cpp
    src/sessions/flightsessionrecorder.cpp
    src/sessions/sessionarchive.cpp
    src/sessions/sessionarchive.h
    src/sessions/flightsessionrecorder.h
    src/testing/acceptanceevaluator.cpp
    src/testing/acceptanceevaluator.h
    src/mission/routedeviationanalyzer.cpp
    src/mission/routedeviationanalyzer.h
)

target_include_directories(FlightBenchSessionRecorderTest PRIVATE src)
target_link_libraries(FlightBenchSessionRecorderTest PRIVATE Qt6::Core Qt6::Gui)
if(MSVC)
    target_compile_options(FlightBenchSessionRecorderTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchRouteAnalysisTest EXCLUDE_FROM_ALL
    tests/routedeviationanalyzertest.cpp
    src/mission/routedeviationanalyzer.cpp
    src/mission/routedeviationanalyzer.h
)

target_include_directories(FlightBenchRouteAnalysisTest PRIVATE src)
target_link_libraries(FlightBenchRouteAnalysisTest PRIVATE Qt6::Core Qt6::Gui)
set_target_properties(FlightBenchRouteAnalysisTest PROPERTIES AUTOMOC OFF AUTOUIC OFF)
if(MSVC)
    target_compile_options(FlightBenchRouteAnalysisTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchWorldMapTest EXCLUDE_FROM_ALL
    tests/worldmapcatalogtest.cpp
    src/mapping/worldmapcatalog.cpp
    src/mapping/worldmapcatalog.h
)

target_include_directories(FlightBenchWorldMapTest PRIVATE src)
target_link_libraries(FlightBenchWorldMapTest PRIVATE Qt6::Core Qt6::Gui)
qt_add_resources(FlightBenchWorldMapTest "world_map_test_assets"
    PREFIX "/"
    BASE "resources"
    FILES ${FLIGHTBENCH_WORLD_MAP_JSON_ASSETS}
)
if(MSVC)
    target_compile_options(FlightBenchWorldMapTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchTestWorkflowTest EXCLUDE_FROM_ALL
    tests/testworkflowtest.cpp
    src/testing/acceptanceevaluator.cpp
    src/testing/acceptanceevaluator.h
    src/sessions/testreportgenerator.cpp
    src/sessions/testreportgenerator.h
)
target_include_directories(FlightBenchTestWorkflowTest PRIVATE src)
target_link_libraries(FlightBenchTestWorkflowTest PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(FlightBenchTestWorkflowTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchSensorServiceTest EXCLUDE_FROM_ALL
    tests/sensorservicecontrollertest.cpp
    src/sensors/sensorservicecontroller.cpp
    src/sensors/sensorsessionrecorder.cpp
    src/sensors/sensorsessionrecorder.h
    src/sensors/sensorsessionstorage.cpp
    src/sensors/sensorsessionstorage.h
    src/sensors/mjpegaviwriter.cpp
    src/sensors/mjpegaviwriter.h
    src/sensors/sensorservicecontroller.h
)
target_compile_definitions(FlightBenchSensorServiceTest PRIVATE FLIGHTBENCH_TESTING=1)
target_include_directories(FlightBenchSensorServiceTest PRIVATE src)
target_link_libraries(FlightBenchSensorServiceTest PRIVATE Qt6::Core Qt6::Network)
if(MSVC)
    target_compile_options(FlightBenchSensorServiceTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchLidarMappingTest EXCLUDE_FROM_ALL
    tests/lidarmappingcontrollertest.cpp
    src/mapping/lidarmappingcontroller.cpp
    src/mapping/lidarmapoutput.cpp
    src/mapping/lidarmapoutput.h
    src/mapping/lidarmaptypes.h
    src/mapping/lidarmappingcontroller.h
)
target_include_directories(FlightBenchLidarMappingTest PRIVATE src)
target_link_libraries(FlightBenchLidarMappingTest PRIVATE Qt6::Core Qt6::Gui)
if(MSVC)
    target_compile_options(FlightBenchLidarMappingTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchDepthMappingTest EXCLUDE_FROM_ALL
    tests/depthmappingcontrollertest.cpp
    src/mapping/depthmappingcontroller.cpp
    src/mapping/depthsurfacegeometry.cpp
    src/mapping/depthmapexport.cpp
    src/mapping/depthmapreader.cpp
    src/mapping/depthmapreader.h
    src/mapping/depthvoxel.h
    src/mapping/depthmapexport.h
    src/mapping/depthsurfacegeometry.h
    src/mapping/depthvoxelcoordinates.h
    src/mapping/depthmappingcontroller.h
)
target_include_directories(FlightBenchDepthMappingTest PRIVATE src)
target_link_libraries(FlightBenchDepthMappingTest PRIVATE Qt6::Core Qt6::Gui Qt6::Quick3D)
if(MSVC)
    target_compile_options(FlightBenchDepthMappingTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchVisualSurveyTest EXCLUDE_FROM_ALL
    tests/visualsurveycontrollertest.cpp
    src/inspection/visualsurveycontroller.cpp
    src/inspection/visualsurveycontroller.h
)
target_include_directories(FlightBenchVisualSurveyTest PRIVATE src)
target_link_libraries(FlightBenchVisualSurveyTest PRIVATE Qt6::Core Qt6::Gui)
if(MSVC)
    target_compile_options(FlightBenchVisualSurveyTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchGimbalInspectionTest EXCLUDE_FROM_ALL
    tests/gimbalinspectioncontrollertest.cpp
    tests/gimbalinspection.qrc
    src/inspection/gimbalinspectioncontroller.cpp
    src/inspection/gimbalinspectioncontroller.h
    src/inspection/visualsurveycontroller.cpp
    src/inspection/visualsurveycontroller.h
    src/mapping/worldmapcatalog.cpp
    src/mapping/worldmapcatalog.h
    src/sessions/testreportgenerator.cpp
    src/sessions/testreportgenerator.h
)
set_target_properties(FlightBenchGimbalInspectionTest PROPERTIES AUTORCC ON)
target_include_directories(FlightBenchGimbalInspectionTest PRIVATE src)
target_link_libraries(FlightBenchGimbalInspectionTest PRIVATE Qt6::Core Qt6::Gui)
if(MSVC)
    target_compile_options(FlightBenchGimbalInspectionTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

if(BUILD_TESTING)
    add_executable(FlightBenchRunCoordinatorTest
        tests/simulationruncoordinatortest.cpp src/app/simulationruncoordinator.cpp)
    target_include_directories(FlightBenchRunCoordinatorTest PRIVATE src)
    target_link_libraries(FlightBenchRunCoordinatorTest PRIVATE Qt6::Core)
    if(MSVC)
        target_compile_options(FlightBenchRunCoordinatorTest PRIVATE /utf-8 /Zc:__cplusplus)
    endif()
    add_test(NAME FlightBench.RunCoordinator COMMAND FlightBenchRunCoordinatorTest)
    set_tests_properties(FlightBench.RunCoordinator PROPERTIES LABELS "unit" TIMEOUT 30
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_executable(FlightBenchSessionIoTest
        tests/sessioniotest.cpp
        src/sensors/sensorsessionstorage.cpp
        src/sensors/mjpegaviwriter.cpp
        src/sessions/replaydata.cpp)
    target_include_directories(FlightBenchSessionIoTest PRIVATE src)
    target_link_libraries(FlightBenchSessionIoTest PRIVATE Qt6::Core Qt6::Gui)
    if(MSVC)
        target_compile_options(FlightBenchSessionIoTest PRIVATE /utf-8 /Zc:__cplusplus)
    endif()
    add_test(NAME FlightBench.SessionIo COMMAND FlightBenchSessionIoTest)
    set_tests_properties(FlightBench.SessionIo PROPERTIES LABELS "unit" TIMEOUT 30
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_executable(FlightBenchPreferencesTest
        tests/applicationpreferencestest.cpp
        src/infrastructure/applicationpreferences.cpp
        src/infrastructure/applicationpreferences.h)
    target_include_directories(FlightBenchPreferencesTest PRIVATE src)
    target_link_libraries(FlightBenchPreferencesTest PRIVATE Qt6::Core)
    if(MSVC)
        target_compile_options(FlightBenchPreferencesTest PRIVATE /utf-8 /Zc:__cplusplus)
    endif()
    add_test(NAME FlightBench.Preferences COMMAND FlightBenchPreferencesTest)
    set_tests_properties(FlightBench.Preferences PROPERTIES LABELS "unit" TIMEOUT 30
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    # Isolated real-layout regression: load the source component and Theme,
    # without starting AppController, Docker, or touching actual flight records.
    find_program(FLIGHTBENCH_QML_TEST_RUNNER NAMES qmltestrunner qmltestrunner6
                 HINTS "${Qt6_DIR}/../../../bin")
    if(FLIGHTBENCH_QML_TEST_RUNNER)
        foreach(_scale IN ITEMS 1 1.25 1.5)
            add_test(NAME FlightBench.FeaturePanels.Scale${_scale}
                     COMMAND "${FLIGHTBENCH_QML_TEST_RUNNER}"
                             -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/tst_FeaturePanels.qml"
                             -import "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/imports")
            set_tests_properties(FlightBench.FeaturePanels.Scale${_scale} PROPERTIES
                LABELS "ui;unit" TIMEOUT 30
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${_scale};QT_FORCE_STDERR_LOGGING=1"
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
            add_test(NAME FlightBench.UiSections.Scale${_scale}
                     COMMAND "${FLIGHTBENCH_QML_TEST_RUNNER}"
                             -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/tst_UiSections.qml"
                             -import "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/imports")
            set_tests_properties(FlightBench.UiSections.Scale${_scale} PROPERTIES
                LABELS "ui;unit" TIMEOUT 30
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${_scale};QT_FORCE_STDERR_LOGGING=1"
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
            add_test(NAME FlightBench.SettingsRows.Scale${_scale}
                     COMMAND "${FLIGHTBENCH_QML_TEST_RUNNER}"
                             -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/tst_SettingsRows.qml"
                             -import "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/imports")
            set_tests_properties(FlightBench.SettingsRows.Scale${_scale} PROPERTIES
                LABELS "ui;unit" TIMEOUT 30
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${_scale};QT_FORCE_STDERR_LOGGING=1"
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
            add_test(NAME FlightBench.AcceptanceLayout.Scale${_scale}
                     COMMAND "${FLIGHTBENCH_QML_TEST_RUNNER}"
                             -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/tst_AcceptanceResultRow.qml"
                             -import "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/imports")
            set_tests_properties(FlightBench.AcceptanceLayout.Scale${_scale} PROPERTIES
                LABELS "ui;unit" TIMEOUT 30
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${_scale};QT_FORCE_STDERR_LOGGING=1"
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
            add_test(NAME FlightBench.GimbalLayout.Scale${_scale}
                     COMMAND "${FLIGHTBENCH_QML_TEST_RUNNER}"
                             -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/tst_GimbalInspectionPanel.qml"
                             -import "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/imports")
            set_tests_properties(FlightBench.GimbalLayout.Scale${_scale} PROPERTIES
                LABELS "ui;unit" TIMEOUT 30
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${_scale};QT_FORCE_STDERR_LOGGING=1"
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
            add_test(NAME FlightBench.ShellChrome.Scale${_scale}
                     COMMAND "${FLIGHTBENCH_QML_TEST_RUNNER}"
                             -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/tst_ShellChrome.qml"
                             -import "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml/imports")
            set_tests_properties(FlightBench.ShellChrome.Scale${_scale} PROPERTIES
                LABELS "ui;unit" TIMEOUT 30
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${_scale};QT_FORCE_STDERR_LOGGING=1"
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        endforeach()
    endif()
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME FlightBench.SensorGatewayClock
                 COMMAND "${Python3_EXECUTABLE}"
                         "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sensor_gateway_clock.py")
        set_tests_properties(FlightBench.SensorGatewayClock PROPERTIES LABELS "unit" TIMEOUT 15)
        add_test(NAME FlightBench.GimbalChannel
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gimbal_channel.py")
        set_tests_properties(FlightBench.GimbalChannel PROPERTIES LABELS "unit" TIMEOUT 15)
    endif()
    add_test(NAME FlightBench.QmlLint
             COMMAND $<TARGET_FILE:Qt6::qmllint>
                     -I "${CMAKE_CURRENT_BINARY_DIR}"
                     ${FLIGHTBENCH_QML_LINT_FILES})
    set_tests_properties(FlightBench.QmlLint PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME FlightBench.RouteLibrary COMMAND $<TARGET_FILE:FlightBenchRouteLibraryTest>)
    add_test(NAME FlightBench.TestPlans COMMAND $<TARGET_FILE:FlightBenchTestPlanRepositoryTest>)
    add_test(NAME FlightBench.Replay COMMAND $<TARGET_FILE:FlightBenchReplayTest>)
    add_test(NAME FlightBench.SessionRecorder COMMAND $<TARGET_FILE:FlightBenchSessionRecorderTest>)
    add_test(NAME FlightBench.RouteAnalysis COMMAND $<TARGET_FILE:FlightBenchRouteAnalysisTest>)
    add_test(NAME FlightBench.WorldMap COMMAND $<TARGET_FILE:FlightBenchWorldMapTest>)
    add_test(NAME FlightBench.TestWorkflow COMMAND $<TARGET_FILE:FlightBenchTestWorkflowTest>)
    add_test(NAME FlightBench.SensorService COMMAND $<TARGET_FILE:FlightBenchSensorServiceTest>)
    add_test(NAME FlightBench.LidarMapping COMMAND $<TARGET_FILE:FlightBenchLidarMappingTest>)
    add_test(NAME FlightBench.DepthMapping COMMAND $<TARGET_FILE:FlightBenchDepthMappingTest>)
    add_test(NAME FlightBench.VisualSurvey COMMAND $<TARGET_FILE:FlightBenchVisualSurveyTest>)
    add_test(NAME FlightBench.GimbalInspection COMMAND $<TARGET_FILE:FlightBenchGimbalInspectionTest>)
    set_tests_properties(FlightBench.QmlLint FlightBench.RouteLibrary FlightBench.TestPlans FlightBench.Replay FlightBench.SessionRecorder
        FlightBench.RouteAnalysis FlightBench.WorldMap FlightBench.TestWorkflow FlightBench.SensorService
        FlightBench.LidarMapping FlightBench.DepthMapping FlightBench.VisualSurvey FlightBench.GimbalInspection
        PROPERTIES LABELS "unit")

    add_test(NAME FlightBench.UiExit.Light
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --theme light)
    add_test(NAME FlightBench.UiExit.Dark
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --theme dark)
    add_test(NAME FlightBench.UiMinimum.Workbench
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --page 0)
    add_test(NAME FlightBench.UiMinimum.Flight
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --page 1 --flight-view 0)
    add_test(NAME FlightBench.UiMinimum.Scene3D
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --page 1 --flight-view 1)
    add_test(NAME FlightBench.UiMinimum.Mission
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --page 1 --flight-view 2)
    add_test(NAME FlightBench.UiMinimum.Records
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --page 2)
    add_test(NAME FlightBench.UiMinimum.Environment
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --drawer environment)
    add_test(NAME FlightBench.UiMinimum.Settings.Appearance
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --drawer settings:1 --text-scale 110)
    add_test(NAME FlightBench.UiMinimum.Settings.Interaction
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --drawer settings:2 --text-scale 110)
    add_test(NAME FlightBench.UiMinimum.Settings.Scene3D
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --drawer settings:3 --text-scale 110)
    add_test(NAME FlightBench.UiMinimum.Settings.Path
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --drawer settings:4 --text-scale 110)
    add_test(NAME FlightBench.UiMinimum.Settings.Records
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --drawer settings:5 --text-scale 110)
    add_test(NAME FlightBench.UiMinimum.Settings.Software
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 760 --window-height 590 --drawer settings:6 --text-scale 110)
    add_test(NAME FlightBench.UiScale125.Workbench
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 900 --window-height 650 --page 0)
    add_test(NAME FlightBench.UiScale150.Settings
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --window-width 900 --window-height 650 --drawer settings:6 --text-scale 110)
    set_tests_properties(FlightBench.UiScale125.Workbench PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=1.25")
    set_tests_properties(FlightBench.UiScale150.Settings PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=1.5")
    set_tests_properties(
        FlightBench.UiExit.Light
        FlightBench.UiExit.Dark
        FlightBench.UiMinimum.Workbench
        FlightBench.UiMinimum.Flight
        FlightBench.UiMinimum.Scene3D
        FlightBench.UiMinimum.Mission
        FlightBench.UiMinimum.Records
        FlightBench.UiMinimum.Environment
        FlightBench.UiMinimum.Settings.Appearance
        FlightBench.UiMinimum.Settings.Interaction
        FlightBench.UiMinimum.Settings.Scene3D
        FlightBench.UiMinimum.Settings.Path
        FlightBench.UiMinimum.Settings.Records
        FlightBench.UiMinimum.Settings.Software
        FlightBench.UiScale125.Workbench
        FlightBench.UiScale150.Settings
        PROPERTIES
        LABELS "ui;smoke"
        TIMEOUT 20)

    add_test(NAME FlightBench.SitlFlight COMMAND $<TARGET_FILE:FlightBenchFlightTest>)
    add_test(NAME FlightBench.SitlMission COMMAND $<TARGET_FILE:FlightBenchMissionTest>)
    add_test(NAME FlightBench.SitlModel.Depth
             COMMAND $<TARGET_FILE:FlightBenchFlightTest> --connection-only --model gz_x500_depth)
    add_test(NAME FlightBench.SitlModel.MonoCamera
             COMMAND $<TARGET_FILE:FlightBenchFlightTest> --connection-only --model gz_x500_mono_cam)
    add_test(NAME FlightBench.SitlModel.Lidar
             COMMAND $<TARGET_FILE:FlightBenchFlightTest> --connection-only --model gz_x500_lidar_2d)
    add_test(NAME FlightBench.SitlModel.Gimbal
             COMMAND $<TARGET_FILE:FlightBenchFlightTest> --connection-only --model gz_x500_gimbal)
    add_test(NAME FlightBench.SitlWorld.Xatu
             COMMAND $<TARGET_FILE:FlightBenchFlightTest> --connection-only --model gz_x500 --world xatu_weiyang)
    set_tests_properties(
        FlightBench.SitlFlight
        FlightBench.SitlMission
        FlightBench.SitlModel.Depth
        FlightBench.SitlModel.MonoCamera
        FlightBench.SitlModel.Lidar
        FlightBench.SitlModel.Gimbal
        FlightBench.SitlWorld.Xatu
        PROPERTIES
        LABELS "sitl;integration"
        RESOURCE_LOCK "flightbench-docker"
        RUN_SERIAL TRUE)
    set_tests_properties(FlightBench.SitlFlight PROPERTIES TIMEOUT 360)
    set_tests_properties(FlightBench.SitlMission PROPERTIES TIMEOUT 480)
    set_tests_properties(
        FlightBench.SitlModel.Depth
        FlightBench.SitlModel.MonoCamera
        FlightBench.SitlModel.Lidar
        FlightBench.SitlModel.Gimbal
        FlightBench.SitlWorld.Xatu
        PROPERTIES TIMEOUT 240)

    # Windows does not search Qt's installation directory when CTest launches a
    # target directly.  Keep the build tree lightweight while making every test
    # runnable from Qt Creator and the command line without copying all Qt DLLs.
    set_tests_properties(
        FlightBench.RouteLibrary
        FlightBench.TestPlans
        FlightBench.Replay
        FlightBench.SessionRecorder
        FlightBench.RouteAnalysis
        FlightBench.WorldMap
        FlightBench.TestWorkflow
        FlightBench.SensorService
        FlightBench.LidarMapping
        FlightBench.DepthMapping
        FlightBench.VisualSurvey
        FlightBench.GimbalInspection
        FlightBench.QmlLint
        FlightBench.UiExit.Light
        FlightBench.UiExit.Dark
        FlightBench.UiMinimum.Workbench
        FlightBench.UiMinimum.Flight
        FlightBench.UiMinimum.Scene3D
        FlightBench.UiMinimum.Mission
        FlightBench.UiMinimum.Records
        FlightBench.UiMinimum.Environment
        FlightBench.UiMinimum.Settings.Appearance
        FlightBench.UiMinimum.Settings.Interaction
        FlightBench.UiMinimum.Settings.Scene3D
        FlightBench.UiMinimum.Settings.Path
        FlightBench.UiMinimum.Settings.Records
        FlightBench.UiMinimum.Settings.Software
        FlightBench.UiScale125.Workbench
        FlightBench.UiScale150.Settings
        FlightBench.SitlFlight
        FlightBench.SitlMission
        FlightBench.SitlModel.Depth
        FlightBench.SitlModel.MonoCamera
        FlightBench.SitlModel.Lidar
        FlightBench.SitlModel.Gimbal
        FlightBench.SitlWorld.Xatu
        PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
