pragma ComponentBehavior: Bound
import QtQuick
import "../controls"
import "../features/environment"
import "../features/plans"
import "../features/settings"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Drawer {
    id: root
    property string mode: "environment"
    property bool healthDetailsOpen: false
    property bool telemetryDetailsOpen: false
    property bool telemetryFrozen: false
    property var telemetrySnapshot: ({})
    property double telemetryLastUpdateMs: 0
    property double telemetryClockMs: Date.now()
    signal flightRequested()
    signal planEditRequested()
    readonly property string panelTitle: mode === "settings" ? "设置" : mode === "health" ? "健康检查" : mode === "telemetry" ? "更多遥测" : "运行环境"
    readonly property int panelIndex: mode === "settings" ? 0 : mode === "health" ? 2 : mode === "telemetry" ? 3 : 1
    readonly property bool sensorsReady: App.gyrometerOk && App.accelerometerOk && App.magnetometerOk
    readonly property bool positionReady: App.localPositionOk
    readonly property bool globalReady: App.globalPositionOk
    readonly property bool homeAndArmReady: App.homePositionOk && App.armable
    readonly property int healthGroupPassedCount: (sensorsReady ? 1 : 0)
                                                  + (positionReady ? 1 : 0)
                                                  + (globalReady ? 1 : 0)
                                                  + (homeAndArmReady ? 1 : 0)
    function openMode(requestedMode) {
        const parts = String(requestedMode).split(":")
        const baseMode = parts[0]
        if (baseMode === "telemetry") telemetryFrozen = false
        if (baseMode === "settings") {
            const requestedPage = parts.length > 1 ? Number(parts[1]) : 0
            settingsCenter.openPage(isNaN(requestedPage)
                                    ? 0 : Math.max(0, Math.min(6, requestedPage)))
        }
        mode = baseMode
        open()
    }

    function oneDecimal(value) {
        return Number(Math.abs(value) < 0.05 ? 0 : value).toFixed(1)
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
        implicitHeight: Math.max(38, valueText.lineCount * valueText.lineHeight)
        RowLayout {
            anchors.fill: parent
            spacing: 12
            Text { text: valueRow.label; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
            Text {
                id: valueText
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: valueRow.value
                color: valueRow.valueColor
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.textSmall
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
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

            SettingsCenter {
                id: settingsCenter
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            RuntimeEnvironmentCenter {
                Layout.fillWidth: true
                Layout.fillHeight: true
                onFlightRequested: root.flightRequested()
                onPlanEditRequested: root.planEditRequested()
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

                    Rectangle {
                        visible: App.hasIssue && Boolean(App.currentIssueExplanation.title)
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? issueExplanation.implicitHeight + 24 : 0
                        Layout.topMargin: visible ? 12 : 0
                        radius: Theme.radiusMedium
                        color: Theme.warningSoft
                        border.width: 1
                        border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.28)
                        ColumnLayout {
                            id: issueExplanation
                            anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12; anchors.rightMargin: 12
                            spacing: 5
                            Text { Layout.fillWidth: true; text: String(App.currentIssueExplanation.title || "当前问题"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
                            Text { Layout.fillWidth: true; text: String(App.currentIssueExplanation.what || ""); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
                            Text { Layout.fillWidth: true; text: "建议：" + String(App.currentIssueExplanation.action || "查看健康检查详情"); color: Theme.warning; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
                        }
                    }

                    RowLayout {
                        visible: App.selectedWorldId !== "default" && Boolean(App.selectedWorldMap.available)
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 42 : 0
                        spacing: 10
                        ThemedIcon { Layout.preferredWidth: 16; Layout.preferredHeight: 16; source: "qrc:/icons/map-pinned.svg"; iconColor: Theme.accent }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text { text: String(App.selectedWorldMap.recommendedTakeoffName || "世界原点起飞区"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                            Text {
                                text: "局部坐标 东 0.0 m / 北 0.0 m · 最近建筑约 "
                                      + Number(App.selectedWorldMap.recommendedTakeoffClearanceMeters || 0).toFixed(1) + " m"
                                color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny
                            }
                        }
                    }

                    DisclosureRow {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        label: "技术详情"
                        expanded: root.healthDetailsOpen
                        topDivider: true
                        onActivated: root.healthDetailsOpen = !root.healthDetailsOpen
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
                            Text { Layout.fillWidth: true; text: root.telemetryTitle(); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSubtitle; font.weight: Font.Medium }
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
                    Text {
                        visible: !(root.telemetryFrozen
                                   ? root.telemetrySnapshot.imuAvailable
                                   : App.imuAvailable)
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        text: root.telemetryFrozen
                              ? "定格时尚未收到飞控主 IMU 数据。"
                              : App.vehicleConnected
                                ? "正在请求飞控主 IMU 数据…"
                                : "连接飞行器后显示飞控主 IMU 角速度。"
                        color: Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        wrapMode: Text.WordWrap
                    }

                    SectionTitle { text: "定位与能源" }
                    ValueRow { label: "GPS"; value: (root.telemetryFrozen ? root.telemetrySnapshot.gpsAvailable : App.gpsAvailable) ? (root.telemetryFrozen ? root.telemetrySnapshot.gpsFix + " · " + root.telemetrySnapshot.satellites + " 星" : App.gpsFixType + " · " + App.gpsSatellites + " 星") : "—" }
                    ValueRow { label: "电池"; value: (root.telemetryFrozen ? root.telemetrySnapshot.batteryAvailable : App.batteryAvailable) ? (root.telemetryFrozen ? Number(root.telemetrySnapshot.batteryPercent).toFixed(0) + "% · " + Number(root.telemetrySnapshot.batteryVoltage).toFixed(1) + " V" : App.batteryRemainingPercent.toFixed(0) + "% · " + App.batteryVoltageVolts.toFixed(1) + " V") : "—" }

                    DisclosureRow {
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        label: "技术详情"
                        expanded: root.telemetryDetailsOpen
                        topDivider: true
                        onActivated: root.telemetryDetailsOpen = !root.telemetryDetailsOpen
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
