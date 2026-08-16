import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench
import "../components"

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
    property real preciseTurnDegrees: 45.0

    signal openSettings()
    signal openHealth()
    signal openTelemetry()

    function displayFlightMode() {
        if (!App.vehicleConnected) return "等待连接"
        if (!App.inAir && App.flightMode === "悬停") return "保持模式"
        return App.flightMode
    }

    function loadSavedRoute(routeId) {
        if (viewIndex !== 2)
            previousViewIndex = viewIndex
        viewIndex = 2
        missionPlanner.loadLibraryRoute(routeId)
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

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compactLayout ? 20 : 28
        anchors.rightMargin: root.compactLayout ? 20 : 28
        anchors.topMargin: root.compactLayout ? 12 : 20
        anchors.bottomMargin: root.compactLayout ? 10 : 18
        spacing: root.compactLayout ? 8 : 12

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compactLayout ? 50 : 58

            ColumnLayout {
                anchors.left: parent.left
                anchors.right: headerActions.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: root.viewIndex === 2 ? "任务" : "飞行"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: 24
                    font.weight: Font.Medium
                }
                Text {
                    Layout.fillWidth: true
                    text: App.hasIssue ? App.issueMessage
                                       : App.vehicleConnected
                                         ? App.selectedModelName + " · UDP 14540 · " + root.displayFlightMode()
                                         : "等待 PX4 飞行器连接"
                    color: App.hasIssue ? Theme.warning : Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                    elide: Text.ElideRight
                }
            }

            Row {
                id: headerActions
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                CompactIconButton {
                    visible: root.viewIndex !== 2
                    iconSource: "qrc:/icons/route.svg"
                    accessibleLabel: "打开航线规划"
                    toolTipText: "航线规划"
                    onClicked: root.openMissionPlanner(false)
                }

                CompactIconButton {
                    visible: root.viewIndex !== 2
                    iconSource: "qrc:/icons/gauge.svg"
                    accessibleLabel: "打开更多遥测"
                    toolTipText: "更多遥测"
                    onClicked: root.openTelemetry()
                }

                CompactIconButton {
                    visible: root.viewIndex !== 2
                    iconSource: "qrc:/icons/heart-pulse.svg"
                    accessibleLabel: "打开健康检查"
                    toolTipText: App.vehicleReady ? "健康检查通过" : "健康检查"
                    normalIconColor: App.vehicleReady ? Theme.success : Theme.textSecondary
                    onClicked: root.openHealth()
                }

                SegmentedControl {
                    visible: root.viewIndex !== 2
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
                    onClicked: missionPlanner.openRouteLibrary()
                }

                CompactIconButton {
                    visible: root.viewIndex === 2
                    outlined: true
                    iconSource: "qrc:/icons/x.svg"
                    accessibleLabel: "关闭任务规划"
                    toolTipText: "返回飞行"
                    onClicked: root.viewIndex = root.previousViewIndex
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 220

            TelemetryPanel {
                anchors.fill: parent
                visible: root.viewIndex === 0
                onMoreTelemetryRequested: root.openTelemetry()
            }

            FlightScene3D {
                anchors.fill: parent
                visible: root.viewIndex === 1
            }

            MissionPlanner {
                id: missionPlanner
                anchors.fill: parent
                visible: root.viewIndex === 2
            }
        }

        SurfaceCard {
            // MissionPlanner already owns its status and actions. Keeping this
            // flight-control strip in mission view only duplicates information
            // and steals the vertical space needed by its settings pane.
            visible: root.viewIndex !== 2
            Layout.fillWidth: true
            Layout.preferredHeight: root.compactLayout ? 62 : 70

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 14
                spacing: 12

                ThemedIcon {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    source: root.viewIndex === 2 ? "qrc:/icons/route.svg"
                                                 : App.keyboardControlActive ? "qrc:/icons/keyboard.svg"
                                                                             : "qrc:/icons/plane.svg"
                    iconColor: root.missionActive || App.keyboardControlActive ? Theme.accent : Theme.textMuted
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: root.controlTitle()
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.controlSubtitle()
                        color: Theme.textMuted
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
                    Text { text: "目标高度"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                    Text { text: App.takeoffAltitude.toFixed(1) + " m"; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.DemiBold }
                }

                Rectangle {
                    visible: App.keyboardControlActive && root.viewIndex !== 2
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 28
                    color: Theme.line
                }

                ColumnLayout {
                    visible: App.keyboardControlActive && root.viewIndex !== 2
                    Layout.preferredWidth: 116
                    spacing: 2
                    Text { text: "水平速度"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                    NumericField {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        value: App.controlSpeedMetersPerSecond
                        from: 0.5; to: 2.0; stepSize: 0.1; decimals: 1; unit: "m/s"
                        accessibleName: "键盘控制水平速度"
                        onValueEdited: value => {
                            App.controlSpeedMetersPerSecond = value
                            root.forceActiveFocus(Qt.OtherFocusReason)
                        }
                    }
                }

                CompactIconButton {
                    visible: App.keyboardControlActive && root.viewIndex !== 2
                    outlined: true
                    iconSource: "qrc:/icons/rotate-ccw.svg"
                    accessibleLabel: "精确转向"
                    toolTipText: "精确转向"
                    enabled: !App.preciseTurnActive && !App.keyboardControlTransitioning
                    onClicked: anglePopup.open()
                }

                ActionButton {
                    visible: root.viewIndex !== 2 && (App.missionRunning || App.missionPaused)
                    Layout.preferredWidth: 86
                    compact: true
                    text: App.missionPaused ? "继续" : "暂停"
                    iconSource: App.missionPaused ? "qrc:/icons/play.svg" : "qrc:/icons/pause.svg"
                    kind: "secondary"
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
                    kind: "secondary"
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
        background: Rectangle { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.lineStrong }
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

    Dialog {
        id: takeoffDialog
        anchors.centerIn: parent
        width: Math.min(410, root.width - 48)
        modal: true
        padding: 20
        background: Rectangle { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.lineStrong }
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
}
