pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import FlightBench

Rectangle {
    id: root
    signal closeRequested()
    signal seekRequested(int timeMs)

    radius: Theme.radiusLarge
    color: Theme.surface
    border.width: 1
    border.color: Theme.line
    clip: true

    property real viewStartMs: 0
    property real viewEndMs: Math.max(1, App.replayDurationMs)
    property real hoverTimeMs: -1
    readonly property real viewDurationMs: Math.max(1, viewEndMs - viewStartMs)

    function formatTime(ms, precise) {
        const totalSeconds = Math.max(0, Number(ms || 0) / 1000)
        const minutes = Math.floor(totalSeconds / 60)
        const seconds = precise ? totalSeconds - minutes * 60 : Math.floor(totalSeconds) % 60
        const rawSeconds = precise ? seconds.toFixed(1) : String(seconds)
        const secondText = (seconds < 10 ? "0" : "") + rawSeconds
        return (minutes < 10 ? "0" : "") + minutes + ":" + secondText
    }

    function resetView() {
        viewStartMs = 0
        viewEndMs = Math.max(1, App.replayDurationMs)
        hoverTimeMs = -1
    }

    function setViewRange(startMs, endMs) {
        const duration = Math.max(1, App.replayDurationMs)
        const range = Math.min(duration, Math.max(Math.min(2000, duration), endMs - startMs))
        let start = Math.max(0, Math.min(duration - range, startMs))
        viewStartMs = start
        viewEndMs = start + range
    }

    function timeAtX(x) {
        return viewStartMs + Math.max(0, Math.min(1, x / Math.max(1, graph.width))) * viewDurationMs
    }

    function nearestSample(timeMs) {
        const series = App.replayTelemetrySeries
        if (!series || series.length === 0) return ({})
        let best = series[0]
        let bestDistance = Math.abs(Number(best.timeMs) - timeMs)
        for (let index = 1; index < series.length; ++index) {
            const distance = Math.abs(Number(series[index].timeMs) - timeMs)
            if (distance >= bestDistance) continue
            best = series[index]
            bestDistance = distance
        }
        return best
    }

    function displayValue(key, unit) {
        const time = hoverTimeMs >= 0 ? hoverTimeMs : App.replayTimeMs
        const sample = nearestSample(time)
        if (sample[key] === undefined) return "—"
        return Number(sample[key]).toFixed(1) + unit
    }

    onVisibleChanged: if (visible) resetView()
    onViewStartMsChanged: graph.requestPaint()
    onViewEndMsChanged: graph.requestPaint()
    onHoverTimeMsChanged: graph.requestPaint()

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 18
        anchors.rightMargin: 10
        height: 54
        spacing: 10

        Text {
            text: "遥测趋势"
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSubtitle
            font.weight: Font.Medium
        }
        Text {
            Layout.fillWidth: true
            text: root.formatTime(root.viewStartMs, false) + " – "
                  + root.formatTime(root.viewEndMs, false)
            color: Theme.textMuted
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.textSmall
        }
        Rectangle {
            visible: Boolean(App.replayRouteAnalysis && App.replayRouteAnalysis.available)
            Layout.preferredWidth: visible ? 238 : 0
            Layout.preferredHeight: 32
            radius: Theme.radiusSmall
            color: deviationMouse.containsMouse ? Theme.accentSoft : Theme.surfaceRaised
            border.width: 1
            border.color: deviationMouse.containsMouse ? Theme.accent : Theme.line

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                spacing: 7
                ThemedIcon {
                    source: "qrc:/icons/locate-fixed.svg"
                    iconColor: Theme.accent
                    Layout.preferredWidth: 15
                    Layout.preferredHeight: 15
                }
                Text {
                    Layout.fillWidth: true
                    text: "平均 "
                          + Number(App.replayRouteAnalysis.meanHorizontalDeviationMeters || 0).toFixed(1)
                          + " m  ·  最大 "
                          + Number(App.replayRouteAnalysis.maxHorizontalDeviationMeters || 0).toFixed(1)
                          + " m"
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    elide: Text.ElideRight
                }
            }
            MouseArea {
                id: deviationMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.seekRequested(Number(App.replayRouteAnalysis.worstTimeMs || 0))
            }
            AppToolTip {
                visible: deviationMouse.containsMouse
                text: "定位到最大航线偏差"
            }
        }
        CompactIconButton {
            iconSource: "qrc:/icons/rotate-ccw.svg"
            accessibleLabel: "恢复完整时间范围"
            toolTipText: "恢复全部"
            onClicked: root.resetView()
        }
        CompactIconButton {
            outlined: true
            iconSource: "qrc:/icons/x.svg"
            accessibleLabel: "关闭遥测趋势"
            toolTipText: "关闭"
            onClicked: root.closeRequested()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 54
        height: 1
        color: Theme.line
    }

    Text {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 18
        anchors.topMargin: 62
        text: "滚轮缩放 · 拖动平移 · 点击定位 · 指向曲线查看数值"
        color: Theme.textFaint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textTiny
    }

    Column {
        id: laneLabels
        anchors.left: parent.left
        anchors.top: graph.top
        anchors.bottom: graph.bottom
        anchors.leftMargin: 16
        width: 72

        Repeater {
            model: [
                { name: "高度", key: "altitude", unit: " m", color: Theme.accent },
                { name: "水平速度", key: "speed", unit: " m/s", color: Theme.success },
                { name: "垂直速度", key: "verticalSpeed", unit: " m/s", color: Theme.warning },
                { name: "航向角", key: "yaw", unit: "°", color: Theme.flightPathColor("violet") }
            ]
            delegate: Item {
                id: laneLabel
                required property var modelData
                width: laneLabels.width
                height: laneLabels.height / 4
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Text {
                        text: laneLabel.modelData.name
                        color: laneLabel.modelData.color
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                    Text {
                        text: root.displayValue(laneLabel.modelData.key,
                                                laneLabel.modelData.unit)
                        color: Theme.textSecondary
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.textTiny
                    }
                }
            }
        }
    }

    Canvas {
        id: graph
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: 96
        anchors.rightMargin: 18
        anchors.topMargin: 84
        anchors.bottomMargin: 42

        function paintLane(context, series, key, laneIndex, color) {
            const laneHeight = height / 4
            const top = laneIndex * laneHeight
            let minimum = Number.POSITIVE_INFINITY
            let maximum = Number.NEGATIVE_INFINITY
            let visibleCount = 0
            for (let index = 0; index < series.length; ++index) {
                const time = Number(series[index].timeMs)
                if (time < root.viewStartMs || time > root.viewEndMs) continue
                const value = Number(series[index][key])
                minimum = Math.min(minimum, value)
                maximum = Math.max(maximum, value)
                ++visibleCount
            }
            if (visibleCount === 0) return
            if (Math.abs(maximum - minimum) < 0.001) {
                minimum -= 0.5
                maximum += 0.5
            }
            const padding = laneHeight * 0.18
            const usableHeight = Math.max(1, laneHeight - padding * 2)
            context.beginPath()
            context.strokeStyle = color
            context.lineWidth = 1.55
            let started = false
            for (let index = 0; index < series.length; ++index) {
                const time = Number(series[index].timeMs)
                if (time < root.viewStartMs || time > root.viewEndMs) continue
                const x = (time - root.viewStartMs) / root.viewDurationMs * width
                const normalized = (Number(series[index][key]) - minimum) / (maximum - minimum)
                const y = top + laneHeight - padding - normalized * usableHeight
                if (!started) {
                    context.moveTo(x, y)
                    started = true
                } else {
                    context.lineTo(x, y)
                }
            }
            context.stroke()
        }

        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)

            context.strokeStyle = String(Theme.line)
            context.lineWidth = 1
            for (let line = 0; line <= 4; ++line) {
                const y = line * height / 4
                context.beginPath()
                context.moveTo(0, y)
                context.lineTo(width, y)
                context.stroke()
            }
            for (let line = 1; line < 5; ++line) {
                const x = line * width / 5
                context.beginPath()
                context.moveTo(x, 0)
                context.lineTo(x, height)
                context.stroke()
            }

            const series = App.replayTelemetrySeries
            if (series && series.length > 0) {
                paintLane(context, series, "altitude", 0, String(Theme.accent))
                paintLane(context, series, "speed", 1, String(Theme.success))
                paintLane(context, series, "verticalSpeed", 2, String(Theme.warning))
                paintLane(context, series, "yaw", 3,
                          String(Theme.flightPathColor("violet")))
            }

            const events = App.replayEvents
            context.fillStyle = String(Theme.warning)
            for (let index = 0; index < events.length; ++index) {
                const time = Number(events[index].timeMs || 0)
                if (time < root.viewStartMs || time > root.viewEndMs) continue
                const x = (time - root.viewStartMs) / root.viewDurationMs * width
                context.fillRect(x - 1, 0, 2, 8)
            }

            if (App.replayTimeMs >= root.viewStartMs && App.replayTimeMs <= root.viewEndMs) {
                const cursorX = (App.replayTimeMs - root.viewStartMs)
                                / root.viewDurationMs * width
                context.beginPath()
                context.moveTo(cursorX, 0)
                context.lineTo(cursorX, height)
                context.strokeStyle = String(Theme.textSecondary)
                context.lineWidth = 1.2
                context.stroke()
            }
            if (root.hoverTimeMs >= 0) {
                const hoverX = (root.hoverTimeMs - root.viewStartMs)
                               / root.viewDurationMs * width
                context.beginPath()
                context.moveTo(hoverX, 0)
                context.lineTo(hoverX, height)
                context.strokeStyle = String(Theme.accent)
                context.lineWidth = 1
                context.stroke()
            }
        }

        MouseArea {
            id: graphMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.CrossCursor
            property real pressX: 0
            property real initialStart: 0
            property real initialEnd: 0
            property bool dragged: false

            onPressed: mouse => {
                pressX = mouse.x
                initialStart = root.viewStartMs
                initialEnd = root.viewEndMs
                dragged = false
            }
            onPositionChanged: mouse => {
                root.hoverTimeMs = root.timeAtX(mouse.x)
                if (!pressed) return
                const deltaX = mouse.x - pressX
                if (Math.abs(deltaX) > 3) dragged = true
                const deltaMs = -deltaX / Math.max(1, width)
                                * (initialEnd - initialStart)
                root.setViewRange(initialStart + deltaMs, initialEnd + deltaMs)
            }
            onReleased: mouse => {
                if (!dragged) root.seekRequested(Math.round(root.timeAtX(mouse.x)))
            }
            onExited: if (!pressed) root.hoverTimeMs = -1
            onWheel: wheel => {
                const duration = Math.max(1, App.replayDurationMs)
                const oldRange = root.viewDurationMs
                const factor = wheel.angleDelta.y > 0 ? 0.78 : 1.28
                const newRange = Math.max(Math.min(2000, duration),
                                          Math.min(duration, oldRange * factor))
                const ratio = Math.max(0, Math.min(1, wheel.x / Math.max(1, width)))
                const centerTime = root.viewStartMs + ratio * oldRange
                root.setViewRange(centerTime - ratio * newRange,
                                  centerTime + (1 - ratio) * newRange)
                root.hoverTimeMs = root.timeAtX(wheel.x)
                wheel.accepted = true
            }
        }

        Connections {
            target: App
            function onReplayTimeChanged() { graph.requestPaint() }
            function onReplayDataChanged() { graph.requestPaint() }
        }
        Connections {
            target: Theme
            function onDarkModeChanged() { graph.requestPaint() }
        }
    }

    Text {
        anchors.left: graph.left
        anchors.top: graph.bottom
        anchors.topMargin: 8
        text: root.formatTime(root.viewStartMs, false)
        color: Theme.textMuted
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.textTiny
    }
    Text {
        anchors.horizontalCenter: graph.horizontalCenter
        anchors.top: graph.bottom
        anchors.topMargin: 8
        text: root.formatTime((root.viewStartMs + root.viewEndMs) / 2, false)
        color: Theme.textMuted
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.textTiny
    }
    Text {
        anchors.right: graph.right
        anchors.top: graph.bottom
        anchors.topMargin: 8
        text: root.formatTime(root.viewEndMs, false)
        color: Theme.textMuted
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.textTiny
    }

    Rectangle {
        visible: root.hoverTimeMs >= 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 112
        anchors.topMargin: 14
        width: hoverTimeText.implicitWidth + 14
        height: 26
        radius: Theme.radiusSmall
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.line
        Text {
            id: hoverTimeText
            anchors.centerIn: parent
            text: root.formatTime(root.hoverTimeMs, true)
            color: Theme.textSecondary
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.textTiny
        }
    }
}
