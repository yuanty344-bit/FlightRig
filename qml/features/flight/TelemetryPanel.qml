pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import QtQuick.Layouts
import FlightBench

Item {
    id: root
    signal moreTelemetryRequested()

    function normalizedFixed(value, decimals) {
        const threshold = 0.5 / Math.pow(10, decimals)
        return Number(Math.abs(value) < threshold ? 0 : value).toFixed(decimals)
    }

    function fixedValue(available, value, decimals) {
        return available ? normalizedFixed(value, decimals) : "—"
    }

    function headingText() {
        if (!App.telemetryAvailable)
            return "—"
        const rounded = ((Math.round(App.headingDegrees) % 360) + 360) % 360
        return ("000" + rounded).slice(-3) + "°"
    }

    function cardinalText() {
        if (!App.telemetryAvailable)
            return ""
        const labels = ["北", "东北", "东", "东南", "南", "西南", "西", "西北"]
        const normalized = ((App.headingDegrees % 360) + 360) % 360
        return labels[Math.round(normalized / 45) % 8]
    }

    function displayFlightMode() {
        if (!App.vehicleConnected)
            return "未知"
        if (!App.inAir && App.flightMode === "悬停")
            return "保持模式"
        return App.flightMode
    }

    component MetricRow: Item {
        id: metric
        property string label: "参数"
        property string value: "—"
        property string unit: ""
        property string detail: ""
        Layout.fillWidth: true
        Layout.preferredHeight: 58

        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
        RowLayout {
            anchors.fill: parent
            spacing: 10
            Text { Layout.fillWidth: true; text: metric.label; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
            Text { text: metric.value; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: 21; font.weight: Font.Medium }
            Text { visible: metric.unit.length > 0; text: metric.unit; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
            Text { visible: metric.detail.length > 0; text: metric.detail; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
        }
    }

    component StatusItem: Item {
        id: statusItem
        property string label: "状态"
        property string value: "—"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Text { text: statusItem.label; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
            Text { text: statusItem.value; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
        }
    }

    implicitHeight: 394

    Connections {
        target: App
        function onAttitudeChanged() { attitude.requestPaint() }
        function onTelemetryAvailableChanged() { attitude.requestPaint() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 278
            spacing: 28

            Item {
                Layout.preferredWidth: Math.min(300, root.width * 0.43)
                Layout.minimumWidth: 248
                Layout.fillHeight: true

                Item {
                    id: instrumentGroup
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: Math.min(284, parent.height - 8)

                    Canvas {
                        id: attitude
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        width: Math.min(238, parent.width - 20)
                        height: width
                        opacity: App.telemetryAvailable ? 1.0 : 0.62
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            const w = width
                            const c = w / 2
                            const outer = w * 0.47
                            const inner = w * 0.405
                            const roll = (App.telemetryAvailable ? App.rollDegrees : 0) * Math.PI / 180
                            const pitch = App.telemetryAvailable ? Math.max(-45, Math.min(45, App.pitchDegrees)) : 0
                            const pitchOffset = pitch * inner / 45

                            ctx.clearRect(0, 0, w, w)
                            ctx.fillStyle = Theme.surfaceRaised
                            ctx.strokeStyle = Theme.lineStrong
                            ctx.lineWidth = 1
                            ctx.beginPath(); ctx.arc(c, c, outer, 0, Math.PI * 2); ctx.fill(); ctx.stroke()

                            ctx.save()
                            ctx.beginPath(); ctx.arc(c, c, inner, 0, Math.PI * 2); ctx.clip()
                            ctx.translate(c, c + pitchOffset)
                            ctx.rotate(-roll)
                            ctx.fillStyle = Theme.instrumentSky
                            ctx.fillRect(-inner * 2.2, -inner * 2.2, inner * 4.4, inner * 2.2)
                            ctx.fillStyle = Theme.instrumentGround
                            ctx.fillRect(-inner * 2.2, 0, inner * 4.4, inner * 2.2)
                            ctx.strokeStyle = "rgba(255,255,255,0.88)"
                            ctx.lineWidth = 2
                            ctx.beginPath(); ctx.moveTo(-inner * 2, 0); ctx.lineTo(inner * 2, 0); ctx.stroke()
                            ctx.strokeStyle = "rgba(255,255,255,0.66)"
                            ctx.fillStyle = "rgba(255,255,255,0.82)"
                            ctx.font = "10px 'Cascadia Mono'"
                            ctx.lineWidth = 1
                            for (let degrees of [-20, -10, 10, 20]) {
                                const offset = -degrees * inner / 45
                                const half = Math.abs(degrees) === 20 ? 25 : 16
                                ctx.beginPath(); ctx.moveTo(-half, offset); ctx.lineTo(half, offset); ctx.stroke()
                                ctx.fillText(Math.abs(degrees).toString(), half + 5, offset + 3)
                            }
                            ctx.restore()

                            ctx.strokeStyle = Theme.instrumentMark
                            ctx.lineWidth = 3
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"
                            ctx.beginPath(); ctx.moveTo(c - 38, c); ctx.lineTo(c - 15, c); ctx.lineTo(c - 6, c + 9); ctx.lineTo(c + 6, c + 9); ctx.lineTo(c + 15, c); ctx.lineTo(c + 38, c); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(c - 6, c + 9); ctx.lineTo(c, c + 16); ctx.lineTo(c + 6, c + 9); ctx.stroke()
                            ctx.beginPath(); ctx.arc(c, c, 3.5, 0, Math.PI * 2); ctx.fill()
                        }
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 11
                        width: headingLabel.implicitWidth + 18
                        height: 25
                        radius: Theme.radiusSmall
                        color: Qt.rgba(Theme.canvas.r, Theme.canvas.g, Theme.canvas.b, 0.76)
                        Text { id: headingLabel; anchors.centerIn: parent; text: root.headingText(); color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                    }

                    IconLink {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: attitude.bottom
                        anchors.topMargin: 4
                        text: "详细遥测"
                        iconSource: "qrc:/icons/gauge.svg"
                        sidePadding: 12
                        onClicked: root.moreTelemetryRequested()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 235
                Layout.maximumHeight: 235
                Layout.alignment: Qt.AlignVCenter
                spacing: 0
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
                MetricRow { label: "相对高度"; value: root.fixedValue(App.telemetryAvailable, App.altitudeMeters, 1); unit: App.telemetryAvailable ? "m" : "" }
                MetricRow { label: "垂直速度"; value: root.fixedValue(App.telemetryAvailable, App.verticalSpeedMetersPerSecond, 1); unit: App.telemetryAvailable ? "m/s" : "" }
                MetricRow { label: "水平速度"; value: root.fixedValue(App.telemetryAvailable, App.speedMetersPerSecond, 1); unit: App.telemetryAvailable ? "m/s" : "" }
                MetricRow { label: "航向"; value: root.headingText(); detail: root.cardinalText() }
                Item { Layout.fillHeight: true }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            spacing: 20
            StatusItem { label: "飞行模式"; value: root.displayFlightMode() }
            StatusItem { label: "解锁状态"; value: App.armed ? "已解锁" : "已锁定" }
            StatusItem { label: "GPS"; value: App.gpsAvailable ? App.gpsSatellites + " 星" : "—" }
            StatusItem { label: "电池"; value: App.batteryAvailable ? App.batteryRemainingPercent.toFixed(0) + "%" : "—" }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
    }
}
