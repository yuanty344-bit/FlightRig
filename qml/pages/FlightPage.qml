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
    property bool forwardPressed: false
    property bool backwardPressed: false
    property bool leftPressed: false
    property bool rightPressed: false
    property bool upPressed: false
    property bool downPressed: false
    property bool yawLeftPressed: false
    property bool yawRightPressed: false
    property int viewIndex: 1
    signal openSettings()
    signal openHealth()
    signal openTelemetry()

    function displayFlightMode() {
        if (!App.vehicleConnected)
            return "等待连接"
        if (!App.inAir && App.flightMode === "悬停")
            return "保持模式"
        return App.flightMode
    }

    function pushKeyboardAxes() {
        if (!App.keyboardControlActive || App.keyboardControlTransitioning)
            return
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
        if (sendRelease)
            App.releaseKeyboardControl()
    }

    function requestPreciseTurn(degrees) {
        clearKeyboardState(false)
        App.turnKeyboardControl(degrees)
        anglePopup.close()
        forceActiveFocus(Qt.OtherFocusReason)
    }

    function handleFlightKey(key, pressed, autoRepeat) {
        if (!App.keyboardControlActive || App.keyboardControlTransitioning)
            return false
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

    Keys.onPressed: function(event) {
        event.accepted = root.handleFlightKey(event.key, true, event.isAutoRepeat)
    }
    Keys.onReleased: function(event) {
        event.accepted = root.handleFlightKey(event.key, false, event.isAutoRepeat)
    }
    onActiveFocusChanged: {
        if (!activeFocus && App.keyboardControlActive)
            clearKeyboardState(true)
    }
    onVisibleChanged: {
        if (!visible && App.keyboardControlActive)
            clearKeyboardState(true)
    }

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
        anchors.leftMargin: 26
        anchors.rightMargin: 26
        anchors.topMargin: 20
        anchors.bottomMargin: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            ColumnLayout {
                spacing: 3
                Text { text: "飞行"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 22; font.weight: Font.Medium }
                Text { text: App.hasIssue ? App.issueMessage : App.vehicleConnected ? App.selectedModelName + " · UDP 14540 · " + root.displayFlightMode() : "等待 PX4 飞行器连接"; color: App.hasIssue ? Theme.warning : Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; elide: Text.ElideRight }
            }
            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: 168
                Layout.preferredHeight: 32
                radius: Theme.radiusMedium
                color: Theme.surface
                border.width: 1
                border.color: Theme.line

                Row {
                    anchors.fill: parent
                    Repeater {
                        model: ["仪表", "三维", "任务"]
                        delegate: Rectangle {
                            required property int index
                            required property string modelData
                            width: 55
                            height: 30
                            anchors.verticalCenter: parent.verticalCenter
                            radius: Theme.radiusSmall
                            color: root.viewIndex === index
                                   ? Theme.surfaceRaised : "transparent"
                            border.width: activeFocus ? 1 : 0
                            border.color: Theme.accent
                            activeFocusOnTab: true
                            Accessible.name: "切换到" + modelData
                            Accessible.role: Accessible.Button
                            Keys.onReturnPressed: activateView()
                            Keys.onEnterPressed: activateView()
                            Keys.onSpacePressed: activateView()

                            function activateView() {
                                if (index === 2 && App.keyboardControlActive) {
                                    root.clearKeyboardState(true)
                                    App.stopKeyboardControl()
                                }
                                root.viewIndex = index
                                root.forceActiveFocus(Qt.OtherFocusReason)
                            }

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: root.viewIndex === index
                                       ? Theme.textPrimary : Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                                font.weight: root.viewIndex === index
                                             ? Font.Medium : Font.Normal
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: parent.activateView()
                            }
                        }
                    }
                }
            }

            IconLink {
                text: "健康检查"
                iconSource: "qrc:/icons/heart-pulse.svg"
                foregroundColor: App.vehicleReady ? Theme.success : Theme.accentHover
                sidePadding: 16
                onClicked: root.openHealth()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 350

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
                anchors.fill: parent
                visible: root.viewIndex === 2
            }
        }

        Rectangle {
            visible: root.viewIndex !== 2
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? (App.keyboardControlActive ? 68 : 42) : 0
            radius: Theme.radiusMedium
            color: App.keyboardControlActive ? Theme.accentSoft : "transparent"
            border.width: 1
            border.color: App.keyboardControlActive ? Theme.accent : Theme.line

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 10

                ThemedIcon {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    source: "qrc:/icons/keyboard.svg"
                    iconColor: App.keyboardControlActive ? Theme.accentHover : Theme.textMuted
                }

                ColumnLayout {
                    Layout.preferredWidth: App.keyboardControlActive ? 134 : 250
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: App.keyboardControlActive ? "键盘控制 · " + App.controlCommandText : "键盘飞行控制"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: App.keyboardControlActive
                              ? "Esc 退出 · L 安全降落"
                              : root.keyboardControlAvailable ? "飞行器已悬停，可以启用" : "完成起飞并悬停后可用"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        elide: Text.ElideRight
                    }
                }

                Text {
                    visible: App.keyboardControlActive
                    Layout.fillWidth: true
                    text: "WASD 移动  ·  Space/C 高度  ·  Q/E 转向  ·  松开悬停"
                    color: Theme.textSecondary
                    font.family: Theme.monoFontFamily
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }

                Row {
                    visible: App.keyboardControlActive
                    spacing: 3
                    Repeater {
                        model: ["慢", "标准", "快"]
                        delegate: Rectangle {
                            required property int index
                            required property string modelData
                            width: index === 1 ? 40 : 30
                            height: 28
                            radius: Theme.radiusSmall
                            color: App.controlSpeedMode === index
                                   ? Theme.accent
                                   : speedMouse.containsMouse ? Theme.surfaceInteractive : "transparent"
                            border.width: activeFocus ? 1 : 0
                            border.color: Theme.accent
                            activeFocusOnTab: true
                            Accessible.name: modelData + "控制速度"
                            Accessible.role: Accessible.Button
                            Keys.onReturnPressed: selectSpeed()
                            Keys.onEnterPressed: selectSpeed()
                            Keys.onSpacePressed: selectSpeed()

                            function selectSpeed() {
                                if (App.keyboardControlTransitioning) return
                                App.controlSpeedMode = index
                                root.forceActiveFocus(Qt.OtherFocusReason)
                            }
                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: App.controlSpeedMode === index ? Theme.accentContent : Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                            }
                            MouseArea {
                                id: speedMouse
                                anchors.fill: parent
                                enabled: !App.keyboardControlTransitioning
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: parent.selectSpeed()
                            }
                        }
                    }
                }


                ActionButton {
                    id: angleButton
                    visible: App.keyboardControlActive
                    Layout.preferredWidth: 58
                    compact: true
                    text: "转向"
                    kind: "secondary"
                    enabled: App.keyboardControlActive
                             && !App.preciseTurnActive
                             && !App.keyboardControlTransitioning
                    onClicked: anglePopup.open()
                }

                ActionButton {
                    Layout.preferredWidth: App.keyboardControlActive ? 72 : 84
                    compact: true
                    text: App.keyboardControlTransitioning
                          ? "处理中"
                          : App.keyboardControlActive ? "退出" : "启用"
                    kind: "secondary"
                    enabled: !App.keyboardControlTransitioning
                             && (App.keyboardControlActive || root.keyboardControlAvailable)
                    onClicked: {
                        if (App.keyboardControlActive)
                            App.stopKeyboardControl()
                        else {
                            root.clearKeyboardState(false)
                            root.forceActiveFocus(Qt.OtherFocusReason)
                            App.startKeyboardControl()
                        }
                    }
                }
            }
        }

        RowLayout {
            visible: root.viewIndex !== 2
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 44 : 0
            spacing: 9

            Rectangle {
                Layout.preferredWidth: 150
                Layout.preferredHeight: 40
                radius: Theme.radiusMedium
                color: targetMouse.containsMouse ? Theme.surfaceRaised : "transparent"
                border.width: 1
                border.color: targetMouse.containsMouse ? Theme.lineStrong : Theme.line
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    spacing: 7
                    Text { Layout.fillWidth: true; text: "目标高度"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                    Text { text: App.takeoffAltitude.toFixed(0) + " m"; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                }
                MouseArea { id: targetMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openSettings() }
            }

            Item { Layout.fillWidth: true }
            ActionButton { Layout.preferredWidth: 96; compact: true; text: "降落"; iconSource: "qrc:/icons/plane-landing.svg"; kind: "secondary"; enabled: App.vehicleConnected && (App.inAir || App.armed) && !App.busy; onClicked: App.land() }
            ActionButton { Layout.preferredWidth: 112; compact: true; text: "起飞"; iconSource: "qrc:/icons/plane-takeoff.svg"; enabled: App.vehicleConnected && App.vehicleReady && App.telemetryAvailable && !App.inAir && !App.busy; onClicked: takeoffDialog.open() }
        }
    }

    Popup {
        id: anglePopup
        x: root.width - width - 94
        y: root.height - height - 98
        width: 258
        height: 126
        padding: 14
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: {
            if (App.keyboardControlActive)
                root.forceActiveFocus(Qt.OtherFocusReason)
        }
        background: Rectangle {
            radius: Theme.radiusLarge
            color: Theme.surface
            border.width: 1
            border.color: Theme.lineStrong
        }
        contentItem: ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Text { text: "精确转向"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                Item { Layout.fillWidth: true }
                Text { text: "到达目标后自动悬停"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { Layout.preferredWidth: 32; text: "左转"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                ActionButton { Layout.fillWidth: true; compact: true; text: "15°"; kind: "secondary"; onClicked: root.requestPreciseTurn(-15) }
                ActionButton { Layout.fillWidth: true; compact: true; text: "45°"; kind: "secondary"; onClicked: root.requestPreciseTurn(-45) }
                ActionButton { Layout.fillWidth: true; compact: true; text: "90°"; kind: "secondary"; onClicked: root.requestPreciseTurn(-90) }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { Layout.preferredWidth: 32; text: "右转"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                ActionButton { Layout.fillWidth: true; compact: true; text: "15°"; kind: "secondary"; onClicked: root.requestPreciseTurn(15) }
                ActionButton { Layout.fillWidth: true; compact: true; text: "45°"; kind: "secondary"; onClicked: root.requestPreciseTurn(45) }
                ActionButton { Layout.fillWidth: true; compact: true; text: "90°"; kind: "secondary"; onClicked: root.requestPreciseTurn(90) }
            }
        }
    }

    Dialog {
        id: takeoffDialog
        anchors.centerIn: parent
        width: Math.min(410, root.width - 48)
        modal: true
        padding: 20
        background: Rectangle { radius: Theme.radiusLarge; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.lineStrong }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "确认起飞"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            Text { Layout.fillWidth: true; text: "飞行器将解锁并上升至 " + App.takeoffAltitude.toFixed(1) + " 米悬停。此操作仅作用于当前 PX4 SITL。"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textBody; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { Layout.preferredWidth: 88; text: "取消"; kind: "secondary"; onClicked: takeoffDialog.close() }
                ActionButton { Layout.preferredWidth: 112; text: "确认起飞"; onClicked: { takeoffDialog.close(); App.takeoff() } }
            }
        }
    }
}
