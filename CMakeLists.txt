cmake_minimum_required(VERSION 3.25)

project(FlightBench VERSION 0.9.0 LANGUAGES CXX)

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
    src/dockercontroller.cpp
    src/dockercontroller.h
    src/flightsessionrecorder.cpp
    src/flightsessionrecorder.h
    src/flightpathgeometry.cpp
    src/flightpathgeometry.h
    src/routelibrary.cpp
    src/routelibrary.h
    src/vehicleconnection.cpp
    src/vehicleconnection.h
)

if(WIN32)
    set(FLIGHTBENCH_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/flightbench-app.ico")
    configure_file(resources/windows/FlightBench.rc.in
                   "${CMAKE_CURRENT_BINARY_DIR}/FlightBench.rc" @ONLY)
    target_sources(FlightBench PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/FlightBench.rc")
endif()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(FlightBench
    URI FlightBench
    VERSION 1.0
    QML_FILES
        qml/Theme.qml
        qml/Main.qml
        qml/components/ActionButton.qml
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
        resources/icons/chevron-down.svg
        resources/icons/copy.svg
        resources/icons/flightbench-app.svg
        resources/icons/file-down.svg
        resources/icons/folder-open.svg
        resources/icons/gauge.svg
        resources/icons/heart-pulse.svg
        resources/icons/keyboard.svg
        resources/icons/map-pin.svg
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
        resources/icons/rotate-ccw.svg
        resources/icons/x.svg
        resources/markers/position-marker.svg
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
    target_compile_options(FlightBench PRIVATE /W4 /permissive- /utf-8)
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
    src/dockercontroller.cpp
    src/dockercontroller.h
    src/flightsessionrecorder.cpp
    src/flightsessionrecorder.h
    src/flightpathgeometry.cpp
    src/flightpathgeometry.h
    src/routelibrary.cpp
    src/routelibrary.h
    src/vehicleconnection.cpp
    src/vehicleconnection.h
)

target_include_directories(FlightBenchFlightTest PRIVATE src)
target_link_libraries(FlightBenchFlightTest PRIVATE Qt6::Core Qt6::Gui Qt6::Quick3D MAVSDK::mavsdk)

if(MSVC)
    target_compile_options(FlightBenchFlightTest PRIVATE /W4 /permissive- /utf-8)
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
    src/dockercontroller.cpp
    src/dockercontroller.h
    src/flightsessionrecorder.cpp
    src/flightsessionrecorder.h
    src/flightpathgeometry.cpp
    src/flightpathgeometry.h
    src/routelibrary.cpp
    src/routelibrary.h
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
    target_compile_options(FlightBenchMissionTest PRIVATE /W4 /permissive- /utf-8)
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
    target_compile_options(FlightBenchRouteLibraryTest PRIVATE /W4 /permissive- /utf-8)
endif()

if(BUILD_TESTING)
    add_test(NAME FlightBench.RouteLibrary COMMAND $<TARGET_FILE:FlightBenchRouteLibraryTest>)
    set_tests_properties(FlightBench.RouteLibrary PROPERTIES LABELS "unit")

    add_test(NAME FlightBench.UiExit.Light
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --theme light)
    add_test(NAME FlightBench.UiExit.Dark
             COMMAND $<TARGET_FILE:FlightBench> --smoke-test --theme dark)
    set_tests_properties(FlightBench.UiExit.Light FlightBench.UiExit.Dark PROPERTIES
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
        FlightBench.UiExit.Light
        FlightBench.UiExit.Dark
        FlightBench.SitlFlight
        FlightBench.SitlMission
        PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
