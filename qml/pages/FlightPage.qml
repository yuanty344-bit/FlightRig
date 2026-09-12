import QtQuick
import "../controls"
import "../features/flight"
import "../features/mission"
import "../features/records"
import "../features/scene3d"
import "../features/sensors"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Item {
    id: root
    focus: true

    readonly property bool keyboardControlAvailable: App.vehicleConnected
                                                     && App.vehicleReady
                                                     && App.telemetryAvailable
                                                     && App.armed
                                                     && App.inAir
                                                     && !App.busy
    readonly property bool missionActive: App.missionRunning || App.missionPaused || App.missionUploaded
    readonly property bool compactLayout: width < 820 || height < 470
    property bool forwardPressed: false
    property bool backwardPressed: false
    property bool leftPressed: false
    property bool rightPressed: false
    property bool upPressed: false
    property bool downPressed: false
    property bool yawLeftPressed: false
    property bool yawRightPressed: false
    property int viewIndex: 1
    property int previousViewIndex: 1
    property real preciseTurnDegrees: App.preciseTurnDefaultDegrees
    property bool replayEventPanelOpen: false
    property bool replayAnalysisOpen: false

    signal openSettings()
    signal openHealth()
    signal openTelemetry()
    signal replayClosed()

    function openReplay() {
        clearKeyboardState(true)
        viewIndex = 1
        replayEventPanelOpen = false
        replayAnalysisOpen = false
        forceActiveFocus(Qt.OtherFocusReason)
    }

    function inspectReplayTime(timeMs) {
        pauseReplayForInspection()
        App.seekReplay(Math.max(0, Math.min(App.replayDurationMs, timeMs)))
    }

    function pauseReplayForInspection() {
        if (App.replayPlaying) App.toggleReplay()
    }

    function displayFlightMode() {
        if (!App.vehicleConnected) return "等待连接"
        if (!App.inAir && App.flightMode === "悬停") return "保持模式"
        return App.flightMode
    }

    function loadSavedRoute(routeId) {
        if (viewIndex !== 2)
            previousViewIndex = viewIndex
        viewIndex = 2
        var plan = App.selectedTestPlan || ({})
        var planEndAction = String(plan.routeId || "") === String(routeId || "")
                            ? String(plan.endAction || "") : ""
        missionPlanner.loadLibraryRoute(routeId, planEndAction)
    }

    function openMissionPlanner(showLibrary) {
        if (viewIndex !== 2)
            previousViewIndex = viewIndex
        viewIndex = 2
        if (showLibrary)
            Qt.callLater(function() { missionPlanner.openRouteLibrary() })
    }

    function pushKeyboardAxes() {
        if (!App.keyboardControlActive || App.keyboardControlTransitioning) return
        App.setKeyboardControlAxes((forwardPressed ? 1 : 0) - (backwardPressed ? 1 : 0),
                                   (rightPressed ? 1 : 0) - (leftPressed ? 1 : 0),
                                   (upPressed ? 1 : 0) - (downPressed ? 1 : 0),
                                   (yawRightPressed ? 1 : 0) - (yawLeftPressed ? 1 : 0))
    }

    function clearKeyboardState(sendRelease) {
        forwardPressed = false
        backwardPressed = false
        leftPressed = false
        rightPressed = false
        upPressed = false
        downPressed = false
        yawLeftPressed = false
        yawRightPressed = false
        if (sendRelease) App.releaseKeyboardControl()
    }

    function requestPreciseTurn(degrees) {
        clearKeyboardState(false)
        App.turnKeyboardControl(degrees)
        anglePopup.close()
        forceActiveFocus(Qt.OtherFocusReason)
    }

    function handleFlightKey(key, pressed, autoRepeat) {
        if (!App.keyboardControlActive || App.keyboardControlTransitioning) return false
        if (key === Qt.Key_Escape && pressed && !autoRepeat) {
            clearKeyboardState(true)
            App.stopKeyboardControl()
            return true
        }
        if (key === Qt.Key_L && pressed && !autoRepeat) {
            clearKeyboardState(true)
            App.land()
            return true
        }
        if (autoRepeat)
            return key === Qt.Key_W || key === Qt.Key_S || key === Qt.Key_A
                    || key === Qt.Key_D || key === Qt.Key_Space || key === Qt.Key_C
                    || key === Qt.Key_Q || key === Qt.Key_E
        if (key === Qt.Key_W) forwardPressed = pressed
        else if (key === Qt.Key_S) backwardPressed = pressed
        else if (key === Qt.Key_A) leftPressed = pressed
        else if (key === Qt.Key_D) rightPressed = pressed
        else if (key === Qt.Key_Space) upPressed = pressed
        else if (key === Qt.Key_C) downPressed = pressed
        else if (key === Qt.Key_Q) yawLeftPressed = pressed
        else if (key === Qt.Key_E) yawRightPressed = pressed
        else return false
        pushKeyboardAxes()
        return true
    }

    function controlTitle() {
        if (viewIndex === 2) return "航线规划"
        if (App.missionPaused) return "任务已暂停"
        if (App.missionRunning) return "任务飞行中"
        if (App.keyboardControlActive) return "键盘控制 · " + App.controlCommandText
        if (App.inAir) return "飞行器悬停中"
        return "飞行控制"
    }

    function controlSubtitle() {
        if (viewIndex === 2) return "编辑、保存并上传一条航点路线"
        if (App.missionRunning || App.missionPaused)
            return "第 " + Math.min(App.missionCurrentItem + 1, App.missionTotalItems)
                    + " / " + App.missionTotalItems + " 航点"
        if (App.keyboardControlActive) return "WASD 移动 · Space/C 高度 · Q/E 转向 · Esc 退出"
        if (!App.vehicleConnected) return "连接飞行器后可用"
        if (!App.inAir) return "起飞后可使用任务或键盘控制"
        return "飞行器已就绪，可以规划任务或启用键盘控制"
    }

    function takeoffHint() {
        if (App.busy) return "请等待当前操作完成"
        if (!App.vehicleConnected) return "等待飞行器连接"
        if (!App.vehicleReady) return "健康检查尚未通过"
        if (!App.telemetryAvailable) return "等待完整遥测数据"
        return "起飞至 " + App.takeoffAltitude.toFixed(1) + " 米"
    }

    function keyboardHint() {
        if (App.keyboardControlTransitioning) return "正在切换控制模式"
        if (App.keyboardControlActive) return "退出键盘飞行控制"
        if (!App.vehicleConnected) return "等待飞行器连接"
        if (!App.vehicleReady) return "健康检查尚未通过"
        if (!App.telemetryAvailable) return "等待完整遥测数据"
        if (!App.armed || !App.inAir) return "起飞后可以启用键盘控制"
        return "启用 WASD 键盘飞行控制"
    }

    Keys.onPressed: function(event) { event.accepted = root.handleFlightKey(event.key, true, event.isAutoRepeat) }
    Keys.onReleased: function(event) { event.accepted = root.handleFlightKey(event.key, false, event.isAutoRepeat) }
    onActiveFocusChanged: if (!activeFocus && App.keyboardControlActive) clearKeyboardState(true)
    onVisibleChanged: if (!visible && App.keyboardControlActive) clearKeyboardState(true)

    Timer {
        interval: 100
        repeat: true
        running: App.keyboardControlActive && !App.keyboardControlTransitioning
                 && root.visible && root.viewIndex !== 2
        onTriggered: App.heartbeatKeyboardControl()
    }

    Connections {
        target: App
        function onSimulationRunningChanged() {
            if (!App.simulationRunning && root.viewIndex === 3)
                root.viewIndex = 1
        }
        function onKeyboardControlActiveChanged() {
            if (App.keyboardControlActive) {
                root.clearKeyboardState(false)
                root.forceActiveFocus(Qt.OtherFocusReason)
                root.pushKeyboardAxes()
            } else {
                anglePopup.close()
                root.clearKeyboardState(false)
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.canvasSoft }
            GradientStop { position: 1; color: Theme.canvas }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: -180
        anchors.top: parent.top
        anchors.topMargin: -250
        width: 540
        height: 540
        radius: 270
        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                       Theme.darkMode ? 0.07 : 0.04)
    }

    ColumnLayout {
        z: 1
        anchors.fill: parent
        anchors.leftMargin: root.compactLayout ? 20 : 28
        anchors.rightMargin: root.compactLayout ? 20 : 28
        anchors.topMargin: root.compactLayout ? 12 : 20
        anchors.bottomMargin: root.compactLayout ? 10 : 18
        spacing: root.compactLayout ? 8 : 12

        SurfaceCard {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compactLayout ? 50 : 58
            elevated: true
            clip: true
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.heroEnd }
                GradientStop { position: 1; color: Theme.heroStart }
            }
            border.color: Theme.heroLine

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.topMargin: Theme.accentRailInset
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.accentRailInset
                anchors.leftMargin: Theme.accentRailInset
                width: 3
                radius: 2
                color: Theme.accent
            }

            ColumnLayout {
                anchors.left: parent.left
                anchors.leftMargin: Theme.panelInset
                anchors.right: headerActions.left
                anchors.rightMargin: Theme.space12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: App.replayActive ? "飞行回放"
                          : root.viewIndex === 2 ? "任务"
                          : root.viewIndex === 3 ? "传感器" : "飞行"
                    color: Theme.heroText
                    font.family: Theme.fontFamily
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: App.replayActive
                          ? App.replaySessionName + " · " + App.replayModelName + " · "
                            + App.replayWorldName
                          : App.hasIssue ? App.issueMessage
                                       : String(App.selectedTestPlan.name || App.selectedModelName)
                                         + " · " + App.testRunStageLabel
                    color: App.hasIssue ? "#F6BB69" : Theme.heroMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                    elide: Text.ElideRight
                }
            }

            Row {
                id: headerActions
                anchors.right: parent.right
                anchors.rightMargin: Theme.edgeInset
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                CompactIconButton {
                    visible: App.simulationRunning && !App.replayActive
                    iconSource: "qrc:/icons/octagon-pause.svg"
                    accessibleLabel: "中止当前测试"
                    toolTipText: App.inAir || App.armed
                                 ? "中止测试并安全降落后停止仿真"
                                 : "中止测试并停止仿真"
                    danger: true
                    onDarkSurface: true
                    onClicked: abortTestDialog.open()
                }

                CompactIconButton {
                    visible: root.viewIndex < 2 && !App.replayActive
                    iconSource: "qrc:/icons/route.svg"
                    accessibleLabel: "打开航线规划"
                    toolTipText: "航线规划"
                    onDarkSurface: true
                    onClicked: root.openMissionPlanner(false)
                }

                CompactIconButton {
                    visible: root.viewIndex < 2 && !App.replayActive
                    iconSource: "qrc:/icons/gauge.svg"
                    accessibleLabel: "打开更多遥测"
                    toolTipText: "更多遥测"
                    onDarkSurface: true
                    onClicked: root.openTelemetry()
                }

                CompactIconButton {
                    visible: root.viewIndex < 2 && !App.replayActive
                    iconSource: "qrc:/icons/heart-pulse.svg"
                    accessibleLabel: "打开健康检查"
                    toolTipText: App.vehicleReady ? "健康检查通过" : "健康检查"
                    onDarkSurface: true
                    normalIconColor: App.vehicleReady ? Theme.success : Theme.heroIcon
                    onClicked: root.openHealth()
                }

                CompactIconButton {
                    visible: ((App.replayActive && App.replaySensorAvailable)
                              || (App.sensorServiceEnabled && App.sensorServiceSupported))
                             && root.viewIndex !== 3
                    iconSource: App.displayedSensorKind === "lidar-2d"
                                ? "qrc:/icons/radar.svg" : "qrc:/icons/camera.svg"
                    accessibleLabel: "打开传感器视图"
                    toolTipText: App.displayedSensorKind === "lidar-2d" ? "查看二维雷达" : "查看相机"
                    onDarkSurface: true
                    normalIconColor: App.sensorServiceConnected ? Theme.success
                                     : App.sensorServiceRunning ? "#79ADFF" : Theme.heroIcon
                    onClicked: {
                        if (root.viewIndex !== 3) root.previousViewIndex = root.viewIndex
                        root.viewIndex = 3
                        root.forceActiveFocus(Qt.OtherFocusReason)
                    }
                }

                SegmentedControl {
                    visible: root.viewIndex < 2 && !App.replayActive
                    width: 124
                    options: ["仪表", "三维"]
                    currentIndex: root.viewIndex
                    onSelected: index => {
                        root.viewIndex = index
                        root.forceActiveFocus(Qt.OtherFocusReason)
                    }
                }

                CompactIconButton {
                    visible: root.viewIndex === 2
                    iconSource: "qrc:/icons/folder-open.svg"
                    accessibleLabel: "打开航线库"
                    toolTipText: "航线库"
                    onDarkSurface: true
                    onClicked: missionPlanner.openRouteLibrary()
                }

                CompactIconButton {
                    visible: App.replayActive
                    iconSource: "qrc:/icons/scroll-text.svg"
                    accessibleLabel: "查看飞行事件"
                    toolTipText: "飞行事件"
                    outlined: root.replayEventPanelOpen
                    onDarkSurface: true
                    normalIconColor: root.replayEventPanelOpen ? "#79ADFF" : Theme.heroIcon
                    onClicked: {
                        root.replayAnalysisOpen = false
                        root.replayEventPanelOpen = !root.replayEventPanelOpen
                    }
                }

                CompactIconButton {
                    visible: App.replayActive
                    iconSource: "qrc:/icons/map-pin.svg"
                    accessibleLabel: "查看或选择回放场景"
                    toolTipText: "查看回放场景"
                    onDarkSurface: true
                    onClicked: replayWorldDialog.open()
                }

                PanelCloseButton {
                    visible: App.replayActive
                    accessibleLabel: "退出历史回放"
                    toolTipText: "退出回放"
                    onDarkSurface: true
                    onClicked: root.replayClosed()
                }

                PanelCloseButton {
                    visible: root.viewIndex === 2
                    accessibleLabel: "关闭任务规划"
                    toolTipText: "返回飞行"
                    onDarkSurface: true
                    onClicked: root.viewIndex = root.previousViewIndex
                }

                PanelCloseButton {
                    visible: root.viewIndex === 3 && !App.replayActive
                    accessibleLabel: "关闭传感器视图"
                    toolTipText: "返回飞行"
                    onDarkSurface: true
                    onClicked: root.viewIndex = root.previousViewIndex < 2
                                               ? root.previousViewIndex : 1
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 220

            TelemetryPanel {
                anchors.fill: parent
                visible: root.viewIndex === 0 && !App.replayActive
                onMoreTelemetryRequested: root.openTelemetry()
            }

            FlightScene3D {
                id: flightScene
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: replayEventPanel.visible ? replayEventPanel.left : parent.right
                anchors.rightMargin: replayEventPanel.visible ? 10 : 0
                // Historical sensor pages fully replace the 3D scene. Keeping
                // the scene visible underneath leaked city pixels through the
                // scrollbar gutter on the right.
                visible: root.viewIndex === 1
                onInspectionTargetSelected: root.viewIndex = 3
                onReplayAnalysisRequested: {
                    root.replayEventPanelOpen = false
                    root.replayAnalysisOpen = true
                }
            }

            ReplayEventPanel {
                id: replayEventPanel
                visible: App.replayActive && root.viewIndex === 1 && root.replayEventPanelOpen
                         && !root.replayAnalysisOpen
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: Math.min(320, Math.max(268, parent.width * 0.28))
                onCloseRequested: root.replayEventPanelOpen = false
                onEventRequested: eventIndex => {
                    root.pauseReplayForInspection()
                    App.seekReplayEvent(eventIndex)
                }
            }

            ReplayAnalysisPanel {
                visible: App.replayActive && root.viewIndex === 1 && root.replayAnalysisOpen
                anchors.fill: parent
                z: 5
                onCloseRequested: root.replayAnalysisOpen = false
                onSeekRequested: timeMs => root.inspectReplayTime(timeMs)
            }

            MissionPlanner {
                id: missionPlanner
                anchors.fill: parent
                visible: root.viewIndex === 2 && !App.replayActive
            }

            Flickable {
                id: sensorScroller
                anchors.fill: parent
                visible: root.viewIndex === 3
                clip: true
                contentWidth: width
                contentHeight: sensorContent.height
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: InsetScrollBar {
                    id: sensorVerticalScrollBar
                    policy: ScrollBar.AsNeeded
                }

                Item {
                    id: sensorContent
                    width: Math.max(0, sensorScroller.width
                                    - (sensorVerticalScrollBar.visible ? Theme.scrollBarExtent : 0))
                    // 460 px still fits every radar summary card at 110% text
                    // scale, while avoiding a few pixels of needless overflow
                    // in a maximized 900p-class window.
                    height: Math.max(sensorScroller.height,
                                     App.displayedSensorKind === "gimbal-camera"
                                     ? sensorCamera.minimumPanelHeight : 460)

                    LidarPanel {
                        anchors.fill: parent
                        visible: App.displayedSensorKind === "lidar-2d"
                    }

                    CameraPanel {
                        id: sensorCamera
                        anchors.fill: parent
                        onInspectionTargetRequested: root.viewIndex = 1
                        viewActive: root.visible && root.viewIndex === 3
                        visible: App.displayedSensorKind === "mono-camera"
                                 || App.displayedSensorKind === "depth-camera"
                                 || App.displayedSensorKind === "gimbal-camera"
                    }
                }
            }
        }

        SurfaceCard {
            // MissionPlanner already owns its status and actions. Keeping this
            // flight-control strip in mission view only duplicates information
            // and steals the vertical space needed by its settings pane.
            visible: root.viewIndex !== 2 && !App.replayActive
            Layout.fillWidth: true
            Layout.preferredHeight: App.keyboardControlActive
                                    ? (root.compactLayout ? 68 : 76)
                                    : (root.compactLayout ? 62 : 70)
            elevated: true
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.heroEnd }
                GradientStop { position: 1; color: Theme.heroStart }
            }
            border.color: Theme.heroLine

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 14
                spacing: 12

                ThemedIcon {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    source: root.viewIndex === 3
                            ? (App.displayedSensorKind === "lidar-2d"
                               ? "qrc:/icons/radar.svg" : "qrc:/icons/camera.svg")
                            : root.viewIndex === 2 ? "qrc:/icons/route.svg"
                                                 : App.keyboardControlActive ? "qrc:/icons/keyboard.svg"
                                                                             : "qrc:/icons/plane.svg"
                    iconColor: root.missionActive || App.keyboardControlActive ? "#79ADFF" : Theme.heroIcon
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: root.controlTitle()
                        color: Theme.heroText
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.controlSubtitle()
                        color: Theme.heroMuted
                        font.family: App.keyboardControlActive ? Theme.monoFontFamily : Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        elide: Text.ElideRight
                    }
                }

                ColumnLayout {
                    visible: root.viewIndex !== 2
                             && !App.keyboardControlActive
                             && !App.keyboardControlTransitioning
                    Layout.preferredWidth: 90
                    spacing: 2
                    Text { text: "目标高度"; color: Theme.heroMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                    Text { text: App.takeoffAltitude.toFixed(1) + " m"; color: Theme.heroText; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.DemiBold }
                }

                Rectangle {
                    visible: App.keyboardControlActive && root.viewIndex !== 2
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 28
                    color: Theme.heroLine
                }

                Flickable {
                    id: keyboardSpeedScroller
                    visible: App.keyboardControlActive && root.viewIndex !== 2
                    Layout.minimumWidth: 218
                    Layout.preferredWidth: Math.min(438, Math.max(218, root.width * 0.31))
                    Layout.maximumWidth: 438
                    Layout.preferredHeight: 44
                    Layout.alignment: Qt.AlignVCenter
                    clip: true
                    contentWidth: keyboardSpeedRow.implicitWidth
                    contentHeight: height
                    interactive: contentWidth > width
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick

                    RowLayout {
                        id: keyboardSpeedRow
                        height: 34
                        spacing: 10

                        Text {
                            text: "水平速度"
                            color: Theme.heroMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            verticalAlignment: Text.AlignVCenter
                        }
                        NumericField {
                            Layout.preferredWidth: 118
                            Layout.preferredHeight: 32
                            Layout.alignment: Qt.AlignVCenter
                            value: App.controlSpeedMetersPerSecond
                            from: 0.5; to: 5.0; stepSize: 0.1; decimals: 1; unit: "m/s"
                            accessibleName: "键盘控制水平速度"
                            onValueEdited: value => {
                                App.controlSpeedMetersPerSecond = value
                                root.forceActiveFocus(Qt.OtherFocusReason)
                            }
                        }

                        Text {
                            text: "垂直速度"
                            color: Theme.heroMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            verticalAlignment: Text.AlignVCenter
                        }
                        NumericField {
                            Layout.preferredWidth: 118
                            Layout.preferredHeight: 32
                            Layout.alignment: Qt.AlignVCenter
                            value: App.controlVerticalSpeedMetersPerSecond
                            from: 0.2; to: 3.0; stepSize: 0.1; decimals: 1; unit: "m/s"
                            accessibleName: "键盘控制垂直速度"
                            onValueEdited: value => {
                                App.controlVerticalSpeedMetersPerSecond = value
                                root.forceActiveFocus(Qt.OtherFocusReason)
                            }
                        }

                        CompactIconButton {
                            outlined: true
                            onDarkSurface: true
                            iconSource: "qrc:/icons/rotate-ccw.svg"
                            accessibleLabel: "精确转向"
                            toolTipText: "精确转向"
                            enabled: !App.preciseTurnActive && !App.keyboardControlTransitioning
                            onClicked: anglePopup.open()
                        }
                    }

                    ScrollBar.horizontal: InsetScrollBar {
                        policy: keyboardSpeedScroller.contentWidth > keyboardSpeedScroller.width
                                ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    }
                }

                ActionButton {
                    visible: root.viewIndex !== 2 && (App.missionRunning || App.missionPaused)
                    Layout.preferredWidth: 86
                    compact: true
                    text: App.missionPaused ? "继续" : "暂停"
                    iconSource: App.missionPaused ? "qrc:/icons/play.svg" : "qrc:/icons/pause.svg"
                    kind: "glass"
                    enabled: !App.busy
                    onClicked: App.missionPaused ? App.resumeMission() : App.pauseMission()
                }

                ActionButton {
                    visible: root.viewIndex !== 2 && !root.missionActive && !App.inAir
                    Layout.preferredWidth: 88
                    compact: true
                    text: "起飞"
                    iconSource: "qrc:/icons/plane-takeoff.svg"
                    toolTipText: root.takeoffHint()
                    enabled: App.vehicleConnected && App.vehicleReady && App.telemetryAvailable && !App.busy
                    onClicked: takeoffDialog.open()
                }

                ActionButton {
                    visible: root.viewIndex !== 2 && !root.missionActive && App.inAir
                    Layout.preferredWidth: App.keyboardControlActive ? 78 : 88
                    compact: true
                    text: App.keyboardControlTransitioning ? "处理中" : App.keyboardControlActive ? "退出" : "键盘"
                    iconSource: "qrc:/icons/keyboard.svg"
                    kind: "glass"
                    toolTipText: root.keyboardHint()
                    enabled: !App.keyboardControlTransitioning
                             && (App.keyboardControlActive || root.keyboardControlAvailable)
                    onClicked: {
                        if (App.keyboardControlActive) App.stopKeyboardControl()
                        else {
                            root.clearKeyboardState(false)
                            root.forceActiveFocus(Qt.OtherFocusReason)
                            App.startKeyboardControl()
                        }
                    }
                }

                ActionButton {
                    visible: root.viewIndex !== 2 && App.inAir
                    Layout.preferredWidth: 86
                    compact: true
                    text: "降落"
                    iconSource: "qrc:/icons/plane-landing.svg"
                    kind: "danger"
                    enabled: App.vehicleConnected && (App.inAir || App.armed) && !App.busy
                    onClicked: App.land()
                }
            }
        }


        SurfaceCard {
            visible: App.replayActive
            Layout.fillWidth: true
            Layout.preferredHeight: root.compactLayout ? 66 : 74

            ReplayControls {
                anchors.fill: parent
                anchors.margins: 12
                onPlayPauseRequested: App.toggleReplay()
                onStopRequested: {
                    if (App.replayPlaying) App.toggleReplay()
                    App.seekReplay(0)
                }
                onCloseRequested: root.replayClosed()
                onSeekRequested: timeMs => App.seekReplay(timeMs)
                onSpeedRequested: multiplier => App.setReplaySpeed(multiplier)
            }
        }
    }

    MotionDialog {
        id: replayWorldDialog
        anchors.centerIn: parent
        width: Math.min(430, root.width - 40)
        height: Math.min(480, root.height - 40,
                         128 + Math.max(1, Math.min(App.simulationWorlds.length, 5)) * 54)
        modal: true
        padding: 20
        background: DialogSurface {
            radius: Theme.radiusLarge
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.lineStrong
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "回放场景"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTitle
                font.weight: Font.Medium
            }
            Text {
                Layout.fillWidth: true
                text: "会话已记录原始场景；也可以在本次回放中临时切换查看。"
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                wrapMode: Text.WordWrap
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: App.simulationWorlds
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: InsetScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: replayWorldOption
                    required property var modelData
                    width: ListView.view.width
                    height: 52
                    radius: Theme.radiusSmall
                    color: String(replayWorldOption.modelData.id) === App.replayWorldId
                           ? Theme.accentSoft : optionMouse.containsMouse
                                                ? Theme.surfaceInteractive : "transparent"
                    border.width: String(replayWorldOption.modelData.id) === App.replayWorldId ? 1 : 0
                    border.color: Theme.accent
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            radius: 7
                            color: "transparent"
                            border.width: 1
                            border.color: String(replayWorldOption.modelData.id) === App.replayWorldId
                                          ? Theme.accent : Theme.lineStrong
                            Rectangle {
                                anchors.centerIn: parent
                                width: 6
                                height: 6
                                radius: 3
                                visible: String(replayWorldOption.modelData.id) === App.replayWorldId
                                color: Theme.accent
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: String(replayWorldOption.modelData.name)
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSmall
                                font.weight: Font.Medium
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(replayWorldOption.modelData.description)
                                color: Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                                elide: Text.ElideRight
                            }
                        }
                    }
                    MouseArea {
                        id: optionMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (App.setReplayWorldId(String(replayWorldOption.modelData.id)))
                                replayWorldDialog.close()
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: anglePopup
        anchors.centerIn: parent
        width: 320
        height: 132
        padding: 14
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: if (App.keyboardControlActive) root.forceActiveFocus(Qt.OtherFocusReason)
        background: DialogSurface { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.lineStrong }
        contentItem: ColumnLayout {
            spacing: 8
            Text { text: "精确转向"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "角度"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                NumericField {
                    Layout.fillWidth: true
                    value: root.preciseTurnDegrees
                    from: 1; to: 180; stepSize: 1; decimals: 0; unit: "°"
                    accessibleName: "精确转向角度"
                    onValueEdited: value => root.preciseTurnDegrees = value
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ActionButton { Layout.fillWidth: true; compact: true; text: "向左转"; kind: "secondary"; onClicked: root.requestPreciseTurn(-root.preciseTurnDegrees) }
                ActionButton { Layout.fillWidth: true; compact: true; text: "向右转"; onClicked: root.requestPreciseTurn(root.preciseTurnDegrees) }
            }
        }
    }

    MotionDialog {
        id: takeoffDialog
        anchors.centerIn: parent
        width: Math.min(410, root.width - 48)
        modal: true
        padding: 20
        background: DialogSurface { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.lineStrong }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "确认起飞"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            Text {
                Layout.fillWidth: true
                text: "飞行器将解锁并上升至 " + App.takeoffAltitude.toFixed(1) + " 米悬停。此操作仅作用于当前 PX4 SITL。"
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textBody
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { Layout.preferredWidth: 88; text: "取消"; kind: "secondary"; onClicked: takeoffDialog.close() }
                ActionButton { Layout.preferredWidth: 112; text: "确认起飞"; onClicked: { takeoffDialog.close(); App.takeoff() } }
            }
        }
    }

    MotionDialog {
        id: abortTestDialog
        anchors.centerIn: parent
        width: Math.min(420, root.width - 48)
        modal: true
        padding: 20
        background: DialogSurface { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.lineStrong }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "中止当前测试？"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            Text {
                Layout.fillWidth: true
                text: App.inAir || App.armed
                      ? "FlightBench 会先发送降落指令，确认落地并锁定后停止仿真。本次记录将标记为“用户中止”。"
                      : "仿真将停止，本次记录会标记为“用户中止”。"
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textBody
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { implicitWidth: 86; text: "继续测试"; kind: "secondary"; onClicked: abortTestDialog.close() }
                ActionButton {
                    implicitWidth: 104
                    text: "确认中止"
                    kind: "danger"
                    iconSource: "qrc:/icons/octagon-pause.svg"
                    onClicked: { abortTestDialog.close(); App.abortTest() }
                }
            }
        }
    }
}
