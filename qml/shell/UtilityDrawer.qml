pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench
import "../components"

Drawer {
    id: root
    property string mode: "environment"
    property bool modelListOpen: false
    property bool worldListOpen: false
    property bool technicalDetailsOpen: false
    property bool healthDetailsOpen: false
    property bool softwareDetailsOpen: false
    property bool telemetryDetailsOpen: false
    property bool telemetryFrozen: false
    property var telemetrySnapshot: ({})
    property double telemetryLastUpdateMs: 0
    property double telemetryClockMs: Date.now()
    signal flightRequested()
    readonly property string panelTitle: mode === "settings" ? "设置" : mode === "health" ? "健康检查" : mode === "telemetry" ? "更多遥测" : "仿真环境"
    readonly property int panelIndex: mode === "settings" ? 1 : mode === "health" ? 2 : mode === "telemetry" ? 3 : 0
    readonly property int healthPassedCount: (App.gyrometerOk ? 1 : 0)
                                             + (App.accelerometerOk ? 1 : 0)
                                             + (App.magnetometerOk ? 1 : 0)
                                             + (App.localPositionOk ? 1 : 0)
                                             + (App.globalPositionOk ? 1 : 0)
                                             + (App.homePositionOk ? 1 : 0)
    readonly property bool sensorsReady: App.gyrometerOk && App.accelerometerOk && App.magnetometerOk
    readonly property bool positionReady: App.localPositionOk
    readonly property bool globalReady: App.globalPositionOk
    readonly property bool homeAndArmReady: App.homePositionOk && App.armable
    readonly property int healthGroupPassedCount: (sensorsReady ? 1 : 0)
                                                  + (positionReady ? 1 : 0)
                                                  + (globalReady ? 1 : 0)
                                                  + (homeAndArmReady ? 1 : 0)
    readonly property bool dockerIssue: App.issueCode === "docker-missing"
                                                || App.issueCode === "docker-offline"
                                                || App.issueCode === "image-missing"
                                                || App.issueCode === "container-conflict"
                                                || App.issueCode === "start-failed"
                                                || App.issueCode === "stop-failed"
                                                || App.issueCode === "model-invalid"
                                                || App.issueCode === "world-invalid"
                                                || App.issueCode === "world-assets-missing"
                                                || App.issueCode === "container-name-invalid"

    function openMode(requestedMode) {
        if (requestedMode === "telemetry") telemetryFrozen = false
        mode = requestedMode
        open()
    }

    function oneDecimal(value) {
        return Number(Math.abs(value) < 0.05 ? 0 : value).toFixed(1)
    }

    function flightPathSwatch(preset) {
        if (preset === "blue") return Theme.darkMode ? "#72A7F5" : "#1769D2"
        if (preset === "teal") return Theme.darkMode ? "#43D5A4" : "#16815F"
        if (preset === "violet") return Theme.darkMode ? "#A78BFA" : "#6D4CC2"
        return Theme.darkMode ? "#FFB454" : "#C85D00"
    }

    function flightPathSettingsAreDefault() {
        return App.flightPathVisible
                && Math.abs(App.flightPathWidthMeters - 0.06) < 0.001
                && App.flightPathColorPreset === "orange"
                && App.flightPathMaxVisibleLengthMeters === 100
    }

    function sensorSummary() {
        let waiting = []
        if (!App.gyrometerOk) waiting.push("陀螺仪")
        if (!App.accelerometerOk) waiting.push("加速度计")
        if (!App.magnetometerOk) waiting.push("磁力计")
        return waiting.length === 0 ? "陀螺仪、加速度计和磁力计正常" : "等待 " + waiting.join("、")
    }

    function healthTitle() {
        if (App.issueCode === "health-timeout") return "有项目需要处理"
        if (App.vehicleReady) return "可以起飞"
        if (!App.vehicleConnected) return App.simulationRunning ? "正在建立连接" : "飞行器未连接"
        if (root.healthGroupPassedCount === 4) return "正在确认稳定状态"
        return "正在准备"
    }

    function healthSummary() {
        if (App.issueCode === "health-timeout") return "健康状态长时间未稳定，请查看仍在等待的项目。"
        if (App.vehicleReady) return "全部条件已通过，并已持续稳定 5 秒。"
        if (!App.vehicleConnected) return App.simulationRunning ? "正在等待 PX4 心跳，通常需要十几秒。" : "请先启动一个本地 PX4 仿真环境。"
        if (root.healthGroupPassedCount === 4) return "条件已经通过，正在进行最后 5 秒稳定确认。"
        return "还需完成 " + (4 - root.healthGroupPassedCount) + " 组检查；启动初期短暂等待属于正常现象。"
    }

    function markTelemetryUpdated() {
        telemetryLastUpdateMs = Date.now()
    }

    function telemetryAgeText() {
        if (telemetryFrozen) return "定格于 " + Qt.formatTime(new Date(telemetrySnapshot.capturedAt), "HH:mm:ss")
        if (telemetryLastUpdateMs === 0) return "等待第一组数据"
        const seconds = Math.max(0, Math.floor((telemetryClockMs - telemetryLastUpdateMs) / 1000))
        return seconds < 2 ? "刚刚更新" : seconds + " 秒前更新"
    }

    function telemetryTitle() {
        if (telemetryFrozen) return "数据已定格"
        if (!App.vehicleConnected) return "飞行器未连接"
        return App.telemetryAvailable && App.imuAvailable && App.gpsAvailable && App.batteryAvailable ? "遥测正常" : "数据不完整"
    }

    function telemetrySummary() {
        if (telemetryFrozen) return telemetryAgeText() + "；MAVSDK 仍在后台继续接收。"
        if (!App.vehicleConnected) return "连接 PX4 后，这里会显示更详细的实时数据。"
        return telemetryAgeText() + " · 数据来自 MAVSDK"
    }

    function toggleTelemetryFreeze() {
        if (!telemetryFrozen) {
            telemetrySnapshot = {
                capturedAt: Date.now(),
                telemetryAvailable: App.telemetryAvailable,
                imuAvailable: App.imuAvailable,
                gpsAvailable: App.gpsAvailable,
                batteryAvailable: App.batteryAvailable,
                roll: App.rollDegrees,
                pitch: App.pitchDegrees,
                yaw: App.yawDegrees,
                gyroRoll: App.gyroRollRateDegreesPerSecond,
                gyroPitch: App.gyroPitchRateDegreesPerSecond,
                gyroYaw: App.gyroYawRateDegreesPerSecond,
                gpsFix: App.gpsFixType,
                satellites: App.gpsSatellites,
                batteryPercent: App.batteryRemainingPercent,
                batteryVoltage: App.batteryVoltageVolts
            }
        }
        telemetryFrozen = !telemetryFrozen
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: root.telemetryClockMs = Date.now()
    }

    Connections {
        target: App
        function onAttitudeChanged() { root.markTelemetryUpdated() }
        function onAngularVelocityChanged() { root.markTelemetryUpdated() }
        function onGpsChanged() { root.markTelemetryUpdated() }
        function onBatteryChanged() { root.markTelemetryUpdated() }
    }

    component SectionTitle: Item {
        id: sectionTitle
        property string text: "分组"
        Layout.fillWidth: true
        Layout.preferredHeight: 42
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: Theme.line }
        Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: sectionTitle.text; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
    }

    component ValueRow: Item {
        id: valueRow
        property string label: "参数"
        property string value: "—"
        property color valueColor: Theme.textPrimary
        Layout.fillWidth: true
        Layout.preferredHeight: 38
        RowLayout {
            anchors.fill: parent
            spacing: 12
            Text { Layout.fillWidth: true; text: valueRow.label; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
            Text { Layout.maximumWidth: 218; text: valueRow.value; color: valueRow.valueColor; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall; elide: Text.ElideMiddle }
        }
    }

    component ThemeChoice: Rectangle {
        id: themeChoice
        property string label: "主题"
        property bool selected: false
        signal chosen()
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: Theme.radiusSmall
        color: selected ? Theme.surface : choiceMouse.containsMouse ? Theme.surfaceInteractive : "transparent"
        border.width: selected || activeFocus ? 1 : 0
        border.color: activeFocus ? Theme.accent : Theme.lineStrong
        activeFocusOnTab: true
        Accessible.name: label
        Accessible.role: Accessible.Button
        Keys.onReturnPressed: themeChoice.chosen()
        Keys.onEnterPressed: themeChoice.chosen()
        Keys.onSpacePressed: themeChoice.chosen()
        Text {
            anchors.centerIn: parent
            text: themeChoice.label
            color: themeChoice.selected ? Theme.textPrimary : Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
            font.weight: themeChoice.selected ? Font.Medium : Font.Normal
        }
        MouseArea { id: choiceMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: themeChoice.chosen() }
    }

    component StepButton: Rectangle {
        id: stepButton
        property string label: "+"
        signal clicked()
        Layout.preferredWidth: 38
        Layout.preferredHeight: 38
        radius: Theme.radiusMedium
        color: !enabled ? Theme.surfaceRaised : stepMouse.containsMouse ? Theme.surfaceInteractive : Theme.surfaceRaised
        border.width: 1
        border.color: activeFocus ? Theme.accent : Theme.line
        opacity: enabled ? 1.0 : 0.45
        activeFocusOnTab: enabled
        Accessible.name: label
        Accessible.role: Accessible.Button
        Keys.onReturnPressed: if (enabled) stepButton.clicked()
        Keys.onEnterPressed: if (enabled) stepButton.clicked()
        Keys.onSpacePressed: if (enabled) stepButton.clicked()
        Text { anchors.centerIn: parent; text: stepButton.label; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSubtitle; font.weight: Font.Medium }
        MouseArea { id: stepMouse; anchors.fill: parent; enabled: stepButton.enabled; hoverEnabled: true; cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: stepButton.clicked() }
    }

    component ToggleSwitch: Rectangle {
        id: toggleSwitch
        property bool checked: false
        signal toggled()
        Layout.preferredWidth: 42
        Layout.preferredHeight: 24
        radius: 12
        color: checked ? Theme.accent : Theme.surfaceInteractive
        border.width: checked ? 0 : 1
        border.color: activeFocus ? Theme.accent : Theme.lineStrong
        activeFocusOnTab: true
        Accessible.name: "显示飞行轨迹"
        Accessible.role: Accessible.CheckBox
        Accessible.checked: checked
        Keys.onReturnPressed: toggleSwitch.toggled()
        Keys.onEnterPressed: toggleSwitch.toggled()
        Keys.onSpacePressed: toggleSwitch.toggled()
        Rectangle {
            width: 18
            height: 18
            radius: 9
            y: 3
            x: toggleSwitch.checked ? 21 : 3
            color: toggleSwitch.checked ? Theme.accentContent : Theme.textMuted
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: toggleSwitch.toggled()
        }
    }

    component PathColorChoice: Rectangle {
        id: pathColorChoice
        required property string preset
        property bool selected: false
        signal chosen()
        Layout.preferredWidth: 30
        Layout.preferredHeight: 30
        radius: 15
        color: "transparent"
        border.width: selected ? 2 : 1
        border.color: activeFocus ? Theme.accent : selected ? Theme.textPrimary : Theme.lineStrong
        activeFocusOnTab: true
        Accessible.name: "轨迹颜色 " + preset
        Accessible.role: Accessible.Button
        Keys.onReturnPressed: pathColorChoice.chosen()
        Keys.onEnterPressed: pathColorChoice.chosen()
        Keys.onSpacePressed: pathColorChoice.chosen()
        Rectangle {
            anchors.centerIn: parent
            width: 20
            height: 20
            radius: 10
            color: root.flightPathSwatch(pathColorChoice.preset)
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pathColorChoice.chosen()
        }
    }

    edge: Qt.RightEdge
    modal: true
    interactive: true
    width: Math.min(340, parent ? parent.width * 0.9 : 340)
    height: parent ? parent.height : 700
    padding: 0

    background: Rectangle {
        color: Theme.surface
        border.width: 1
        border.color: Theme.lineStrong
    }

    contentItem: ColumnLayout {
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 14
                spacing: 10
                Text { Layout.fillWidth: true; text: root.panelTitle; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
                CompactIconButton {
                    iconSource: "qrc:/icons/x.svg"
                    accessibleLabel: "关闭" + root.panelTitle
                    toolTipText: "关闭"
                    onClicked: root.close()
                }
            }
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.panelIndex

            ScrollView {
                clip: true
                leftPadding: 20
                rightPadding: 20
                topPadding: 18
                bottomPadding: 18
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        spacing: 12
                        Rectangle {
                            Layout.preferredWidth: 9
                            Layout.preferredHeight: 9
                            radius: 5
                            color: root.dockerIssue ? Theme.warning : App.dockerAvailable && App.imageAvailable ? Theme.success : Theme.textFaint
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                text: root.dockerIssue ? "环境需要处理" : App.vehicleConnected ? "仿真已连接" : App.simulationRunning ? "正在等待飞行器" : App.dockerAvailable && App.imageAvailable ? "环境已就绪" : "正在检查环境"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textBody
                                font.weight: Font.Medium
                            }
                            Text {
                                text: App.dockerAvailable && App.imageAvailable ? "Docker Desktop · PX4 SITL" : "检查 Docker Desktop 与本机镜像"
                                color: Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                            }
                        }
                    }

                    SectionTitle { Layout.topMargin: 8; text: "飞行器" }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 68
                        radius: Theme.radiusMedium
                        color: modelPickerMouse.containsMouse && modelPickerMouse.enabled ? Theme.surfaceInteractive : Theme.surfaceRaised
                        border.width: 1
                        border.color: root.modelListOpen ? Theme.accent : Theme.line
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                RowLayout {
                                    spacing: 8
                                    Text { text: App.selectedModelName; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textBody; font.weight: Font.Medium }
                                    Text { visible: App.selectedModelId === "gz_x500"; text: "推荐"; color: Theme.accent; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; font.weight: Font.Medium }
                                }
                                Text { Layout.fillWidth: true; text: App.selectedModelDescription; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideRight }
                            }
                            Text {
                                visible: App.simulationRunning
                                text: "运行中"
                                color: Theme.textFaint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                            }
                            ThemedIcon {
                                visible: !App.simulationRunning
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                source: "qrc:/icons/chevron-down.svg"
                                iconColor: Theme.textSecondary
                                rotation: root.modelListOpen ? 180 : 0
                                Behavior on rotation { NumberAnimation { duration: 120 } }
                            }
                        }
                        MouseArea {
                            id: modelPickerMouse
                            anchors.fill: parent
                            enabled: !App.simulationRunning && !App.busy
                            hoverEnabled: true
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: root.modelListOpen = !root.modelListOpen
                        }
                    }

                    ColumnLayout {
                        visible: root.modelListOpen
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        spacing: 2
                        Repeater {
                            model: App.simulationModels
                            delegate: Rectangle {
                                id: modelOption
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 54
                                radius: Theme.radiusSmall
                                color: optionMouse.containsMouse ? Theme.surfaceInteractive : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 10
                                    spacing: 10
                                    Rectangle {
                                        Layout.preferredWidth: 16
                                        Layout.preferredHeight: 16
                                        radius: 8
                                        color: "transparent"
                                        border.width: 1
                                        border.color: modelOption.modelData.id === App.selectedModelId ? Theme.accent : Theme.lineStrong
                                        Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Theme.accent; visible: modelOption.modelData.id === App.selectedModelId }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { text: modelOption.modelData.name; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: modelOption.modelData.id === App.selectedModelId ? Font.Medium : Font.Normal }
                                        Text { Layout.fillWidth: true; text: modelOption.modelData.description; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideRight }
                                    }
                                }
                                MouseArea {
                                    id: optionMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        App.selectedModelId = modelOption.modelData.id
                                        root.modelListOpen = false
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        visible: App.simulationRunning
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        text: "停止仿真后可更换机型。"
                        color: Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }

                    SectionTitle { Layout.topMargin: 8; text: "仿真世界" }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 68
                        radius: Theme.radiusMedium
                        color: worldPickerMouse.containsMouse && worldPickerMouse.enabled
                               ? Theme.surfaceInteractive : Theme.surfaceRaised
                        border.width: 1
                        border.color: root.worldListOpen ? Theme.accent : Theme.line
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text { text: App.selectedWorldName; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textBody; font.weight: Font.Medium }
                                Text { Layout.fillWidth: true; text: App.selectedWorldDescription; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideRight }
                            }
                            Text { visible: App.simulationRunning; text: "运行中"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                            ThemedIcon {
                                visible: !App.simulationRunning
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                source: "qrc:/icons/chevron-down.svg"
                                iconColor: Theme.textSecondary
                                rotation: root.worldListOpen ? 180 : 0
                                Behavior on rotation { NumberAnimation { duration: 120 } }
                            }
                        }
                        MouseArea {
                            id: worldPickerMouse
                            anchors.fill: parent
                            enabled: !App.simulationRunning && !App.busy
                            hoverEnabled: true
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: root.worldListOpen = !root.worldListOpen
                        }
                    }
                    ColumnLayout {
                        visible: root.worldListOpen
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        spacing: 2
                        Repeater {
                            model: App.simulationWorlds
                            delegate: Rectangle {
                                id: worldOption
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 54
                                radius: Theme.radiusSmall
                                color: worldOptionMouse.containsMouse ? Theme.surfaceInteractive : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 10
                                    spacing: 10
                                    Rectangle {
                                        Layout.preferredWidth: 16; Layout.preferredHeight: 16; radius: 8
                                        color: "transparent"; border.width: 1
                                        border.color: worldOption.modelData.id === App.selectedWorldId ? Theme.accent : Theme.lineStrong
                                        Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Theme.accent; visible: worldOption.modelData.id === App.selectedWorldId }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 2
                                        Text { text: worldOption.modelData.name; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: worldOption.modelData.id === App.selectedWorldId ? Font.Medium : Font.Normal }
                                        Text { Layout.fillWidth: true; text: worldOption.modelData.description; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideRight }
                                    }
                                }
                                MouseArea {
                                    id: worldOptionMouse
                                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        App.selectedWorldId = worldOption.modelData.id
                                        root.worldListOpen = false
                                    }
                                }
                            }
                        }
                    }

                    SectionTitle { Layout.topMargin: 8; text: "临时容器" }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: "容器名称"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                            Text { text: "退出仿真后自动删除"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                        }
                        TextField {
                            id: containerNameField
                            Layout.preferredWidth: 176
                            Layout.preferredHeight: 34
                            text: App.containerName
                            enabled: !App.simulationRunning && !App.busy
                            maximumLength: 63
                            selectByMouse: true
                            color: Theme.textPrimary
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.textTiny
                            validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9][A-Za-z0-9_.-]{0,62}$/ }
                            onEditingFinished: {
                                if (acceptableInput) App.containerName = text
                                else text = App.containerName
                            }
                            background: Rectangle {
                                radius: Theme.radiusMedium
                                color: Theme.surfaceRaised
                                border.width: 1
                                border.color: containerNameField.activeFocus ? Theme.accent : Theme.line
                            }
                        }
                    }

                    SectionTitle { Layout.topMargin: 8; text: "运行连接" }
                    ValueRow { label: "PX4 SITL"; value: App.simulationRunning ? "运行中" : "未启动"; valueColor: App.simulationRunning ? Theme.success : Theme.textMuted }
                    ValueRow { label: "MAVSDK"; value: App.vehicleConnected ? "已连接" : App.simulationRunning ? "等待连接" : "未连接"; valueColor: App.vehicleConnected ? Theme.success : Theme.textMuted }

                    DisclosureRow {
                        Layout.fillWidth: true
                        label: "连接详情"
                        expanded: root.technicalDetailsOpen
                        onToggled: root.technicalDetailsOpen = !root.technicalDetailsOpen
                    }
                    ColumnLayout {
                        visible: root.technicalDetailsOpen
                        Layout.fillWidth: true
                        spacing: 0
                        ValueRow { label: "镜像"; value: "px4io/px4-sitl-gazebo:latest" }
                        ValueRow { label: "世界"; value: App.selectedWorldName }
                        ValueRow { label: "容器"; value: App.containerName }
                        ValueRow { label: "MAVLink"; value: "UDP 14540" }
                        ValueRow { label: "图形输出"; value: "无界面（HEADLESS）" }
                    }

                    Text { visible: root.dockerIssue; Layout.fillWidth: true; Layout.topMargin: 10; text: App.issueMessage; color: Theme.warning; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }

                    ActionButton {
                        visible: root.dockerIssue
                        Layout.fillWidth: true
                        Layout.topMargin: 16
                        text: "重新检测"
                        iconSource: "qrc:/icons/refresh-cw.svg"
                        kind: "secondary"
                        enabled: !App.busy
                        onClicked: App.recover()
                    }
                    ActionButton {
                        visible: !root.dockerIssue && App.simulationRunning
                        Layout.fillWidth: true
                        Layout.topMargin: 16
                        text: App.landingAndStopping
                              ? "正在降落…"
                              : (App.inAir || App.armed) ? "降落并停止" : "停止仿真"
                        iconSource: (App.inAir || App.armed)
                                    ? "qrc:/icons/plane-landing.svg"
                                    : "qrc:/icons/square.svg"
                        kind: "secondary"
                        enabled: !App.busy && !App.landingAndStopping
                        onClicked: {
                            if (App.inAir || App.armed)
                                App.landAndStopSimulation()
                            else
                                App.stopSimulation()
                        }
                    }
                    ActionButton {
                        visible: !root.dockerIssue && !App.simulationRunning
                        Layout.fillWidth: true
                        Layout.topMargin: 16
                        text: "启动 " + App.selectedModelName
                        iconSource: "qrc:/icons/play.svg"
                        enabled: App.dockerAvailable && App.imageAvailable && !App.busy
                        onClicked: App.startSimulation()
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        text: App.landingAndStopping
                              ? "正在等待飞行器落地并自动锁定，请保持仿真运行。"
                              : "机型、世界与容器名称将在下次启动时生效。"
                        color: App.landingAndStopping ? Theme.textMuted : Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        wrapMode: Text.WordWrap
                    }
                }
            }

            ScrollView {
                clip: true
                leftPadding: 20
                rightPadding: 20
                topPadding: 18
                bottomPadding: 18
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 58
                        spacing: 12
                        Rectangle { Layout.preferredWidth: 9; Layout.preferredHeight: 9; radius: 5; color: Theme.success }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text { text: "设置会自动保存"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textBody; font.weight: Font.Medium }
                            Text { text: "只保留适合日常调整的安全选项"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                        }
                    }

                    SectionTitle { Layout.topMargin: 6; text: "外观" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 82
                        spacing: 8
                        Text { text: "界面主题"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                        Text { text: "切换后立即应用并保存"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 34
                            radius: Theme.radiusMedium
                            color: Theme.surfaceRaised
                            border.width: 1
                            border.color: Theme.line
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 2
                                spacing: 2
                                ThemeChoice { label: "浅色"; selected: Theme.mode === "light"; onChosen: Theme.mode = "light" }
                                ThemeChoice { label: "深色"; selected: Theme.mode === "dark"; onChosen: Theme.mode = "dark" }
                                ThemeChoice { label: "跟随系统"; selected: Theme.mode === "system"; onChosen: Theme.mode = "system" }
                            }
                        }
                    }

                    SectionTitle { Layout.topMargin: 14; text: "飞行控制" }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 58
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: "默认起飞高度"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                            Text { text: "范围 1.0–100.0 米"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                        }
                        NumericField {
                            Layout.preferredWidth: 138
                            value: App.takeoffAltitude
                            from: 1.0; to: 100.0; stepSize: 0.1; decimals: 1; unit: "m"
                            accessibleName: "默认起飞高度"
                            onValueEdited: value => App.takeoffAltitude = value
                        }
                    }
                    RowLayout {
                        visible: Math.abs(App.takeoffAltitude - 5) > 0.1
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "恢复为 5 m"
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            MouseArea { anchors.fill: parent; anchors.margins: -8; cursorShape: Qt.PointingHandCursor; onClicked: App.takeoffAltitude = 5 }
                        }
                    }
                    Text { Layout.fillWidth: true; Layout.topMargin: 9; text: "修改后会用于下一次起飞，不影响正在执行的飞行操作。"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }

                    SectionTitle { Layout.topMargin: 16; text: "轨迹显示" }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: "显示飞行轨迹"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                            Text { text: "关闭后仍会继续记录"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                        }
                        ToggleSwitch {
                            checked: App.flightPathVisible
                            onToggled: App.flightPathVisible = !App.flightPathVisible
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 5
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: "轨迹粗细"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                            Text { text: Math.round(App.flightPathWidthMeters * 100) + " cm"; color: Theme.textSecondary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall }
                        }
                        Slider {
                            id: pathWidthSlider
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            from: 0.02
                            to: 0.12
                            stepSize: 0.01
                            value: App.flightPathWidthMeters
                            onMoved: App.flightPathWidthMeters = value
                            background: Rectangle {
                                x: pathWidthSlider.leftPadding
                                y: pathWidthSlider.topPadding + pathWidthSlider.availableHeight / 2 - height / 2
                                width: pathWidthSlider.availableWidth
                                height: 3
                                radius: 2
                                color: Theme.lineStrong
                                Rectangle {
                                    width: pathWidthSlider.visualPosition * parent.width
                                    height: parent.height
                                    radius: parent.radius
                                    color: Theme.accent
                                }
                            }
                            handle: Rectangle {
                                x: pathWidthSlider.leftPadding + pathWidthSlider.visualPosition * (pathWidthSlider.availableWidth - width)
                                y: pathWidthSlider.topPadding + pathWidthSlider.availableHeight / 2 - height / 2
                                width: 15
                                height: 15
                                radius: 8
                                color: Theme.surface
                                border.width: 2
                                border.color: Theme.accent
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        spacing: 8
                        Text { Layout.fillWidth: true; text: "轨迹颜色"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                        PathColorChoice { preset: "orange"; selected: App.flightPathColorPreset === preset; onChosen: App.flightPathColorPreset = preset }
                        PathColorChoice { preset: "blue"; selected: App.flightPathColorPreset === preset; onChosen: App.flightPathColorPreset = preset }
                        PathColorChoice { preset: "teal"; selected: App.flightPathColorPreset === preset; onChosen: App.flightPathColorPreset = preset }
                        PathColorChoice { preset: "violet"; selected: App.flightPathColorPreset === preset; onChosen: App.flightPathColorPreset = preset }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        spacing: 10
                        Text { Layout.fillWidth: true; text: "可见长度"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                        Rectangle {
                            Layout.preferredWidth: 210
                            Layout.preferredHeight: 34
                            radius: Theme.radiusMedium
                            color: Theme.surfaceRaised
                            border.width: 1
                            border.color: Theme.line
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 2
                                spacing: 2
                                ThemeChoice { label: "25"; selected: App.flightPathMaxVisibleLengthMeters === 25; onChosen: App.flightPathMaxVisibleLengthMeters = 25 }
                                ThemeChoice { label: "50"; selected: App.flightPathMaxVisibleLengthMeters === 50; onChosen: App.flightPathMaxVisibleLengthMeters = 50 }
                                ThemeChoice { label: "100"; selected: App.flightPathMaxVisibleLengthMeters === 100; onChosen: App.flightPathMaxVisibleLengthMeters = 100 }
                                ThemeChoice { label: "全部"; selected: App.flightPathMaxVisibleLengthMeters === 0; onChosen: App.flightPathMaxVisibleLengthMeters = 0 }
                            }
                        }
                    }
                    Text { Layout.fillWidth: true; text: "显示、粗细、颜色和可见长度同时用于三维画面与任务地图；完整数据仍会保存在会话记录中。"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
                    RowLayout {
                        visible: !root.flightPathSettingsAreDefault()
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "恢复轨迹默认设置"
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            MouseArea { anchors.fill: parent; anchors.margins: -8; cursorShape: Qt.PointingHandCursor; onClicked: App.resetFlightPathDisplaySettings() }
                        }
                    }

                    DisclosureRow {
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        label: "软件信息"
                        expanded: root.softwareDetailsOpen
                        topDivider: true
                        onToggled: root.softwareDetailsOpen = !root.softwareDetailsOpen
                    }
                    ColumnLayout {
                        visible: root.softwareDetailsOpen
                        Layout.fillWidth: true
                        spacing: 0
                        ValueRow { label: "FlightBench"; value: App.applicationVersion }
                        ValueRow { label: "Qt 运行库"; value: App.qtRuntimeVersion }
                        ValueRow { label: "MAVSDK"; value: App.mavsdkVersion }
                        ValueRow { label: "构建"; value: App.buildDescription }
                        ValueRow { label: "系统"; value: App.platformDescription }
                        ValueRow { label: "PX4 镜像"; value: "px4io/px4-sitl-gazebo:latest" }
                    }

                    Text { Layout.fillWidth: true; Layout.topMargin: 14; text: "仅控制本机软件仿真，不连接真实无人机。镜像和通信地址只读；临时容器名称可在仿真启动前修改。"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
                }
            }

            ScrollView {
                clip: true
                leftPadding: 20
                rightPadding: 20
                topPadding: 18
                bottomPadding: 18
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        spacing: 12
                        Rectangle {
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 5
                            color: App.issueCode === "health-timeout" ? Theme.warning : App.vehicleReady ? Theme.success : App.vehicleConnected ? Theme.accent : Theme.textFaint
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: root.healthTitle(); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSubtitle; font.weight: Font.Medium }
                            Text { Layout.fillWidth: true; text: root.healthSummary(); color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
                        }
                    }

                    SectionTitle { Layout.topMargin: 6; text: "起飞条件" }
                    HealthCheckRow { Layout.fillWidth: true; label: "飞控传感器"; description: root.sensorSummary(); complete: root.sensorsReady }
                    HealthCheckRow { Layout.fillWidth: true; label: "本地定位"; description: complete ? "局部位置估计已经建立" : "等待局部坐标估计"; complete: root.positionReady }
                    HealthCheckRow { Layout.fillWidth: true; label: "GPS 与全局位置"; description: complete ? (App.gpsAvailable ? App.gpsFixType + " · " + App.gpsSatellites + " 星" : "全局位置估计已经建立") : "等待 GPS 与全局坐标"; complete: root.globalReady }
                    HealthCheckRow { Layout.fillWidth: true; label: "Home 与解锁许可"; description: complete ? "返航参考点和解锁许可均正常" : !App.homePositionOk ? "等待 Home 返航参考点" : "等待 PX4 允许解锁"; complete: root.homeAndArmReady; dividerVisible: false }

                    DisclosureRow {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        label: "技术详情"
                        expanded: root.healthDetailsOpen
                        topDivider: true
                        onToggled: root.healthDetailsOpen = !root.healthDetailsOpen
                    }

                    ColumnLayout {
                        visible: root.healthDetailsOpen
                        Layout.fillWidth: true
                        spacing: 0
                        ValueRow { label: "陀螺仪"; value: App.gyrometerOk ? "已通过" : "等待"; valueColor: App.gyrometerOk ? Theme.success : Theme.textMuted }
                        ValueRow { label: "加速度计"; value: App.accelerometerOk ? "已通过" : "等待"; valueColor: App.accelerometerOk ? Theme.success : Theme.textMuted }
                        ValueRow { label: "磁力计"; value: App.magnetometerOk ? "已通过" : "等待"; valueColor: App.magnetometerOk ? Theme.success : Theme.textMuted }
                        ValueRow { label: "本地位置"; value: App.localPositionOk ? "已通过" : "等待"; valueColor: App.localPositionOk ? Theme.success : Theme.textMuted }
                        ValueRow { label: "全局位置"; value: App.globalPositionOk ? "已通过" : "等待"; valueColor: App.globalPositionOk ? Theme.success : Theme.textMuted }
                        ValueRow { label: "Home 点"; value: App.homePositionOk ? "已通过" : "等待"; valueColor: App.homePositionOk ? Theme.success : Theme.textMuted }
                        ValueRow { label: "解锁许可"; value: App.armable ? "允许" : "等待"; valueColor: App.armable ? Theme.success : Theme.textMuted }
                    }

                    ActionButton {
                        visible: App.issueCode === "health-timeout"
                        Layout.fillWidth: true
                        Layout.topMargin: 16
                        text: "重新启动仿真"
                        iconSource: "qrc:/icons/refresh-cw.svg"
                        kind: "secondary"
                        enabled: !App.busy
                        onClicked: App.recover()
                    }
                    ActionButton {
                        visible: App.issueCode !== "health-timeout" && !App.vehicleConnected
                        Layout.fillWidth: true
                        Layout.topMargin: 16
                        text: "打开仿真环境"
                        iconSource: "qrc:/icons/panel-right-open.svg"
                        kind: "secondary"
                        onClicked: root.mode = "environment"
                    }
                    ActionButton {
                        visible: App.issueCode !== "health-timeout" && App.vehicleReady
                        Layout.fillWidth: true
                        Layout.topMargin: 16
                        text: "前往飞行"
                        iconSource: "qrc:/icons/gauge.svg"
                        onClicked: {
                            root.close()
                            root.flightRequested()
                        }
                    }

                    Text { Layout.fillWidth: true; Layout.topMargin: 14; text: "检查结果直接来自当前 PX4 健康状态；FlightBench 不会绕过任何起飞条件。"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
                }
            }

            ScrollView {
                clip: true
                leftPadding: 20
                rightPadding: 20
                topPadding: 18
                bottomPadding: 18
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        spacing: 12
                        Rectangle {
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 5
                            color: root.telemetryFrozen ? Theme.accent : !App.vehicleConnected ? Theme.textFaint : App.telemetryAvailable && App.imuAvailable && App.gpsAvailable && App.batteryAvailable ? Theme.success : Theme.warning
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: root.telemetryTitle(); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSubtitle; font.weight: Font.Medium }
                            Text { Layout.fillWidth: true; text: root.telemetrySummary(); color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideRight }
                        }
                        ActionButton {
                            visible: App.vehicleConnected && (App.telemetryAvailable || root.telemetryFrozen)
                            Layout.preferredWidth: 92
                            text: root.telemetryFrozen ? "继续实时" : "定格数据"
                            kind: "secondary"
                            compact: true
                            onClicked: root.toggleTelemetryFreeze()
                        }
                    }

                    SectionTitle { Layout.topMargin: 6; text: "姿态" }
                    ValueRow { label: "横滚 Roll"; value: (root.telemetryFrozen ? root.telemetrySnapshot.telemetryAvailable : App.telemetryAvailable) ? root.oneDecimal(root.telemetryFrozen ? root.telemetrySnapshot.roll : App.rollDegrees) + " °" : "—" }
                    ValueRow { label: "俯仰 Pitch"; value: (root.telemetryFrozen ? root.telemetrySnapshot.telemetryAvailable : App.telemetryAvailable) ? root.oneDecimal(root.telemetryFrozen ? root.telemetrySnapshot.pitch : App.pitchDegrees) + " °" : "—" }
                    ValueRow { label: "偏航 Yaw"; value: (root.telemetryFrozen ? root.telemetrySnapshot.telemetryAvailable : App.telemetryAvailable) ? root.oneDecimal(root.telemetryFrozen ? root.telemetrySnapshot.yaw : App.yawDegrees) + " °" : "—" }

                    SectionTitle { text: "角速度" }
                    ValueRow { label: "X / Roll"; value: (root.telemetryFrozen ? root.telemetrySnapshot.imuAvailable : App.imuAvailable) ? root.oneDecimal(root.telemetryFrozen ? root.telemetrySnapshot.gyroRoll : App.gyroRollRateDegreesPerSecond) + " °/s" : "—" }
                    ValueRow { label: "Y / Pitch"; value: (root.telemetryFrozen ? root.telemetrySnapshot.imuAvailable : App.imuAvailable) ? root.oneDecimal(root.telemetryFrozen ? root.telemetrySnapshot.gyroPitch : App.gyroPitchRateDegreesPerSecond) + " °/s" : "—" }
                    ValueRow { label: "Z / Yaw"; value: (root.telemetryFrozen ? root.telemetrySnapshot.imuAvailable : App.imuAvailable) ? root.oneDecimal(root.telemetryFrozen ? root.telemetrySnapshot.gyroYaw : App.gyroYawRateDegreesPerSecond) + " °/s" : "—" }

                    SectionTitle { text: "定位与能源" }
                    ValueRow { label: "GPS"; value: (root.telemetryFrozen ? root.telemetrySnapshot.gpsAvailable : App.gpsAvailable) ? (root.telemetryFrozen ? root.telemetrySnapshot.gpsFix + " · " + root.telemetrySnapshot.satellites + " 星" : App.gpsFixType + " · " + App.gpsSatellites + " 星") : "—" }
                    ValueRow { label: "电池"; value: (root.telemetryFrozen ? root.telemetrySnapshot.batteryAvailable : App.batteryAvailable) ? (root.telemetryFrozen ? Number(root.telemetrySnapshot.batteryPercent).toFixed(0) + "% · " + Number(root.telemetrySnapshot.batteryVoltage).toFixed(1) + " V" : App.batteryRemainingPercent.toFixed(0) + "% · " + App.batteryVoltageVolts.toFixed(1) + " V") : "—" }

                    DisclosureRow {
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        label: "技术详情"
                        expanded: root.telemetryDetailsOpen
                        topDivider: true
                        onToggled: root.telemetryDetailsOpen = !root.telemetryDetailsOpen
                    }
                    ColumnLayout {
                        visible: root.telemetryDetailsOpen
                        Layout.fillWidth: true
                        spacing: 0
                        ValueRow { label: "数据来源"; value: "MAVSDK 3.17.1" }
                        ValueRow { label: "MAVLink"; value: "UDP 14540" }
                        ValueRow { label: "姿态数据"; value: (root.telemetryFrozen ? root.telemetrySnapshot.telemetryAvailable : App.telemetryAvailable) ? "有效" : "未收到"; valueColor: (root.telemetryFrozen ? root.telemetrySnapshot.telemetryAvailable : App.telemetryAvailable) ? Theme.success : Theme.textMuted }
                        ValueRow { label: "IMU 数据"; value: (root.telemetryFrozen ? root.telemetrySnapshot.imuAvailable : App.imuAvailable) ? "有效" : "未收到"; valueColor: (root.telemetryFrozen ? root.telemetrySnapshot.imuAvailable : App.imuAvailable) ? Theme.success : Theme.textMuted }
                    }

                    Text { Layout.fillWidth: true; Layout.topMargin: 14; text: root.telemetryFrozen ? "定格只暂停界面数值，MAVSDK 接收不会停止。" : App.vehicleConnected ? "数值来自当前实时遥测；未收到的数据始终显示为“—”。" : "启动并连接飞行器后显示实时值。"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
                }
            }
        }
    }
}
