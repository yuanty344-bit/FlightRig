import QtQuick
import FlightBench

Rectangle {
    id: root
    signal expandRequested()
    width: Math.min(340, parent ? Math.max(220, parent.width - 24) : 340)
    height: 132
    radius: Theme.radiusSmall
    color: Theme.darkMode ? "#E6101B29" : "#F5FFFFFF"
    border.width: 1
    border.color: Theme.line

    CompactIconButton {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 5
        anchors.topMargin: 4
        implicitWidth: 26
        implicitHeight: 26
        z: 2
        iconSource: "qrc:/icons/zoom-in.svg"
        accessibleLabel: "展开遥测趋势"
        toolTipText: "展开图表"
        onClicked: root.expandRequested()
    }

    Row {
        id: valueRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 10
        anchors.topMargin: 9
        anchors.rightMargin: 31
        spacing: 13

        Text {
            width: Math.max(58, (valueRow.width - valueRow.spacing * 2) / 3)
            text: "高度  " + App.replayAltitudeMeters.toFixed(1) + " m"
            color: Theme.accent
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.textTiny
            elide: Text.ElideRight
        }
        Text {
            width: Math.max(58, (valueRow.width - valueRow.spacing * 2) / 3)
            text: "水平  " + App.replaySpeedMetersPerSecond.toFixed(1) + " m/s"
            color: Theme.success
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.textTiny
            elide: Text.ElideRight
        }
        Text {
            width: Math.max(58, (valueRow.width - valueRow.spacing * 2) / 3)
            text: "垂直  " + App.replayVerticalSpeedMetersPerSecond.toFixed(1) + " m/s"
            color: Theme.warning
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.textTiny
            elide: Text.ElideRight
        }
    }

    Item {
        id: eventArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 31
        height: 31

        Text {
            anchors.fill: parent
            text: App.replayEvents.length > 0
                  ? "事件 " + Math.max(1, App.replayCurrentEventIndex + 1)
                    + "/" + App.replayEvents.length + "  " + App.replayCurrentEventLabel
                  : "本次会话没有事件标记"
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        HoverHandler { id: eventHover }
        AppToolTip {
            visible: eventHover.hovered && App.replayCurrentEventLabel.length > 0
            text: App.replayCurrentEventLabel
        }
    }

    Canvas {
        id: chart
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 68
        anchors.bottomMargin: 9

        function paintSeries(context, series, key, minimum, maximum, color) {
            if (series.length < 2) return
            const span = Math.max(0.001, maximum - minimum)
            context.beginPath()
            context.strokeStyle = color
            context.lineWidth = 1.35
            for (let index = 0; index < series.length; ++index) {
                const point = series[index]
                const x = Number(point.timeMs) / Math.max(1, App.replayDurationMs) * width
                const y = height - (Number(point[key]) - minimum) / span * height
                if (index === 0) context.moveTo(x, y)
                else context.lineTo(x, y)
            }
            context.stroke()
        }

        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.strokeStyle = String(Theme.line)
            context.lineWidth = 1
            for (let line = 1; line <= 2; ++line) {
                const y = line * height / 3
                context.beginPath()
                context.moveTo(0, y)
                context.lineTo(width, y)
                context.stroke()
            }

            const series = App.replayTelemetrySeries
            if (!series || series.length < 2) return
            let minimumAltitude = Number(series[0].altitude)
            let maximumAltitude = minimumAltitude
            let maximumSpeed = 0
            for (let index = 0; index < series.length; ++index) {
                const altitude = Number(series[index].altitude)
                minimumAltitude = Math.min(minimumAltitude, altitude)
                maximumAltitude = Math.max(maximumAltitude, altitude)
                maximumSpeed = Math.max(maximumSpeed, Number(series[index].speed))
            }
            paintSeries(context, series, "altitude", minimumAltitude, maximumAltitude,
                        String(Theme.accent))
            paintSeries(context, series, "speed", 0, maximumSpeed, String(Theme.success))
            const cursorX = App.replayTimeMs / Math.max(1, App.replayDurationMs) * width
            context.beginPath()
            context.moveTo(cursorX, 0)
            context.lineTo(cursorX, height)
            context.strokeStyle = String(Theme.textMuted)
            context.lineWidth = 1
            context.stroke()
        }

        Connections {
            target: App
            function onReplayTimeChanged() { chart.requestPaint() }
            function onReplayDataChanged() { chart.requestPaint() }
        }
        Connections {
            target: Theme
            function onDarkModeChanged() { chart.requestPaint() }
        }
    }
}
