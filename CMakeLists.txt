cmake_minimum_required(VERSION 3.25)

project(FlightBench VERSION 0.10.0 LANGUAGES CXX)

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

find_package(Qt6 6.11 REQUIRED COMPONENTS Quick Quick3D Svg)
find_package(MAVSDK 3.17 CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(FlightBench
    src/main.cpp
    src/appcontroller.cpp
    src/appcontroller.h
    src/acceptanceevaluator.cpp
    src/acceptanceevaluator.h
    src/dockercontroller.cpp
    src/dockercontroller.h
    src/flightsessionrecorder.cpp
    src/flightsessionrecorder.h
    src/flightpathgeometry.cpp
    src/flightpathgeometry.h
    src/routelibrary.cpp
    src/routelibrary.h
    src/testplanrepository.cpp
    src/testplanrepository.h
    src/replaycontroller.cpp
    src/replaycontroller.h
    src/preflightevaluator.cpp
    src/preflightevaluator.h
    src/px4eventtranslator.cpp
    src/px4eventtranslator.h
    src/routedeviationanalyzer.cpp
    src/routedeviationanalyzer.h
    src/testreportgenerator.cpp
    src/testreportgenerator.h
    src/worldmapcatalog.cpp
    src/worldmapcatalog.h
    src/vehicleconnection.cpp
    src/vehicleconnection.h
)

# Opt-in console subsystem for sanitizer/debugger runs. Normal Windows builds
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

# City maps are generated assets. They are discovered on CMake configure, so a
# new world only needs one normal reconfigure and never edits this file.
file(GLOB FLIGHTBENCH_WORLD_MAP_ASSETS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "resources/worldmaps/*.svg"
    "resources/worldmaps/*.json"
)
set(FLIGHTBENCH_WORLD_MAP_JSON_ASSETS ${FLIGHTBENCH_WORLD_MAP_ASSETS})
list(FILTER FLIGHTBENCH_WORLD_MAP_JSON_ASSETS INCLUDE REGEX "\\.json$")

qt_add_qml_module(FlightBench
    URI FlightBench
    VERSION 1.0
    QML_FILES
        qml/Theme.qml
        qml/Main.qml
        qml/components/ActionButton.qml
        qml/components/AcceptanceResultRow.qml
        qml/components/AppToolTip.qml
        qml/components/CompactIconButton.qml
        qml/components/DisclosureRow.qml
        qml/components/HealthCheckRow.qml
        qml/components/IconLink.qml
        qml/components/MissionPlanner.qml
        qml/components/NumericField.qml
        qml/components/SegmentedControl.qml
        qml/components/StatusRow.qml
        qml/components/SurfaceCard.qml
        qml/components/ToastHost.qml
        qml/components/TelemetryPanel.qml
        qml/components/FlightScene3D.qml
        qml/components/ReplayAnalysisPanel.qml
        qml/components/ReplayControls.qml
        qml/components/ReplayEventPanel.qml
        qml/components/ReplayTelemetryChart.qml
        qml/components/RouteDeviationSummary.qml
        qml/components/RuntimeEnvironmentCenter.qml
        qml/components/SimulationModelPicker.qml
        qml/components/SimulationWorldPicker.qml
        qml/components/SelectField.qml
        qml/components/SettingsCenter.qml
        qml/components/SessionEditDialog.qml
        qml/components/SessionFilterDialog.qml
        qml/components/SessionTrashDialog.qml
        qml/components/PreflightSummary.qml
        qml/components/TestPlanEditDialog.qml
        qml/components/TestPlanPicker.qml
        qml/components/TestResultSummary.qml
        qml/components/WorldScene3D.qml
        qml/components/GroundPositionMarker3D.qml
        qml/components/DroneModel3D.qml
        qml/components/ThemedIcon.qml
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
        resources/icons/x.svg
        resources/markers/position-marker.svg
        ${FLIGHTBENCH_WORLD_MAP_ASSETS}
        resources/licenses/LUCIDE_LICENSE
)

target_link_libraries(FlightBench PRIVATE
    Qt6::Quick
    Qt6::Quick3D
    Qt6::Svg
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
    src/appcontroller.cpp
    src/appcontroller.h
    src/acceptanceevaluator.cpp
    src/acceptanceevaluator.h
    src/dockercontroller.cpp
    src/dockercontroller.h
    src/flightsessionrecorder.cpp
    src/flightsessionrecorder.h
    src/flightpathgeometry.cpp
    src/flightpathgeometry.h
    src/routelibrary.cpp
    src/routelibrary.h
    src/testplanrepository.cpp
    src/testplanrepository.h
    src/replaycontroller.cpp
    src/replaycontroller.h
    src/preflightevaluator.cpp
    src/preflightevaluator.h
    src/px4eventtranslator.cpp
    src/px4eventtranslator.h
    src/routedeviationanalyzer.cpp
    src/routedeviationanalyzer.h
    src/testreportgenerator.cpp
    src/testreportgenerator.h
    src/worldmapcatalog.cpp
    src/worldmapcatalog.h
    src/vehicleconnection.cpp
    src/vehicleconnection.h
)

target_include_directories(FlightBenchFlightTest PRIVATE src)
target_link_libraries(FlightBenchFlightTest PRIVATE Qt6::Core Qt6::Gui Qt6::Quick3D MAVSDK::mavsdk)

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
    src/appcontroller.cpp
    src/appcontroller.h
    src/acceptanceevaluator.cpp
    src/acceptanceevaluator.h
    src/dockercontroller.cpp
    src/dockercontroller.h
    src/flightsessionrecorder.cpp
    src/flightsessionrecorder.h
    src/flightpathgeometry.cpp
    src/flightpathgeometry.h
    src/routelibrary.cpp
    src/routelibrary.h
    src/testplanrepository.cpp
    src/testplanrepository.h
    src/replaycontroller.cpp
    src/replaycontroller.h
    src/preflightevaluator.cpp
    src/preflightevaluator.h
    src/px4eventtranslator.cpp
    src/px4eventtranslator.h
    src/routedeviationanalyzer.cpp
    src/routedeviationanalyzer.h
    src/testreportgenerator.cpp
    src/testreportgenerator.h
    src/worldmapcatalog.cpp
    src/worldmapcatalog.h
    src/vehicleconnection.cpp
    src/vehicleconnection.h
)

target_include_directories(FlightBenchMissionTest PRIVATE src)
target_link_libraries(FlightBenchMissionTest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick3D
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
    src/routelibrary.cpp
    src/routelibrary.h
)

target_include_directories(FlightBenchRouteLibraryTest PRIVATE src)
target_link_libraries(FlightBenchRouteLibraryTest PRIVATE Qt6::Core)

if(MSVC)
    target_compile_options(FlightBenchRouteLibraryTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchTestPlanRepositoryTest EXCLUDE_FROM_ALL
    tests/testplanrepositorytest.cpp
    src/testplanrepository.cpp
    src/testplanrepository.h
)

target_include_directories(FlightBenchTestPlanRepositoryTest PRIVATE src)
target_link_libraries(FlightBenchTestPlanRepositoryTest PRIVATE Qt6::Core)

if(MSVC)
    target_compile_options(FlightBenchTestPlanRepositoryTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchReplayTest EXCLUDE_FROM_ALL
    tests/replaycontrollertest.cpp
    src/replaycontroller.cpp
    src/replaycontroller.h
    src/flightpathgeometry.cpp
    src/flightpathgeometry.h
)

target_include_directories(FlightBenchReplayTest PRIVATE src)
target_link_libraries(FlightBenchReplayTest PRIVATE Qt6::Core Qt6::Gui Qt6::Quick3D)
if(MSVC)
    target_compile_options(FlightBenchReplayTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchSessionRecorderTest EXCLUDE_FROM_ALL
    tests/flightsessionrecordertest.cpp
    src/flightsessionrecorder.cpp
    src/flightsessionrecorder.h
    src/acceptanceevaluator.cpp
    src/acceptanceevaluator.h
    src/routedeviationanalyzer.cpp
    src/routedeviationanalyzer.h
)

target_include_directories(FlightBenchSessionRecorderTest PRIVATE src)
target_link_libraries(FlightBenchSessionRecorderTest PRIVATE Qt6::Core Qt6::Gui)
if(MSVC)
    target_compile_options(FlightBenchSessionRecorderTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchRouteAnalysisTest EXCLUDE_FROM_ALL
    tests/routedeviationanalyzertest.cpp
    src/routedeviationanalyzer.cpp
    src/routedeviationanalyzer.h
)

target_include_directories(FlightBenchRouteAnalysisTest PRIVATE src)
target_link_libraries(FlightBenchRouteAnalysisTest PRIVATE Qt6::Core Qt6::Gui)
set_target_properties(FlightBenchRouteAnalysisTest PROPERTIES AUTOMOC OFF AUTOUIC OFF)
if(MSVC)
    target_compile_options(FlightBenchRouteAnalysisTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

add_executable(FlightBenchWorldMapTest EXCLUDE_FROM_ALL
    tests/worldmapcatalogtest.cpp
    src/worldmapcatalog.cpp
    src/worldmapcatalog.h
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
    src/acceptanceevaluator.cpp
    src/acceptanceevaluator.h
    src/testreportgenerator.cpp
    src/testreportgenerator.h
)
target_include_directories(FlightBenchTestWorkflowTest PRIVATE src)
target_link_libraries(FlightBenchTestWorkflowTest PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(FlightBenchTestWorkflowTest PRIVATE /W4 /permissive- /utf-8 /wd4702)
endif()

if(BUILD_TESTING)
    add_test(NAME FlightBench.RouteLibrary COMMAND $<TARGET_FILE:FlightBenchRouteLibraryTest>)
    add_test(NAME FlightBench.TestPlans COMMAND $<TARGET_FILE:FlightBenchTestPlanRepositoryTest>)
    add_test(NAME FlightBench.Replay COMMAND $<TARGET_FILE:FlightBenchReplayTest>)
    add_test(NAME FlightBench.SessionRecorder COMMAND $<TARGET_FILE:FlightBenchSessionRecorderTest>)
    add_test(NAME FlightBench.RouteAnalysis COMMAND $<TARGET_FILE:FlightBenchRouteAnalysisTest>)
    add_test(NAME FlightBench.WorldMap COMMAND $<TARGET_FILE:FlightBenchWorldMapTest>)
    add_test(NAME FlightBench.TestWorkflow COMMAND $<TARGET_FILE:FlightBenchTestWorkflowTest>)
    set_tests_properties(FlightBench.RouteLibrary FlightBench.TestPlans FlightBench.Replay FlightBench.SessionRecorder
        FlightBench.RouteAnalysis FlightBench.WorldMap FlightBench.TestWorkflow
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
    set_tests_properties(
        FlightBench.UiExit.Light
        FlightBench.UiExit.Dark
        FlightBench.UiMinimum.Workbench
        FlightBench.UiMinimum.Flight
        FlightBench.UiMinimum.Scene3D
        FlightBench.UiMinimum.Mission
        FlightBench.UiMinimum.Records
        PROPERTIES
        LABELS "ui;smoke"
        TIMEOUT 20)

    add_test(NAME FlightBench.SitlFlight COMMAND $<TARGET_FILE:FlightBenchFlightTest>)
    add_test(NAME FlightBench.SitlMission COMMAND $<TARGET_FILE:FlightBenchMissionTest>)
    set_tests_properties(FlightBench.SitlFlight FlightBench.SitlMission PROPERTIES
        LABELS "sitl;integration"
        RESOURCE_LOCK "flightbench-docker"
        RUN_SERIAL TRUE
        TIMEOUT 360)

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
        FlightBench.UiExit.Light
        FlightBench.UiExit.Dark
        FlightBench.UiMinimum.Workbench
        FlightBench.UiMinimum.Flight
        FlightBench.UiMinimum.Scene3D
        FlightBench.UiMinimum.Mission
        FlightBench.UiMinimum.Records
        FlightBench.SitlFlight
        FlightBench.SitlMission
        PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
