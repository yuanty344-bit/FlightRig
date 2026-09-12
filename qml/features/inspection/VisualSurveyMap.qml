pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Rectangle {
    id: root
    signal replayRequested(int elapsedMs)
    property bool compact: false
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    property int filterMode: 0
    property bool dragging: false
    property point dragOrigin: Qt.point(0, 0)
    property real startPanX: 0
    property real startPanY: 0
    readonly property var frames: App.visualSurvey.keyframes || []
    readonly property var selected: App.visualSurvey.selectedKeyframe || ({})
    property real minEast: 0
    property real maxEast: 1
    property real minNorth: 0
    property real maxNorth: 1

    function updateBounds() {
        if (!frames || frames.length === 0) {
            minEast = -10; maxEast = 10; minNorth = -10; maxNorth = 10
            mapCanvas.requestPaint()
            return
        }
        let a = Number(frames[0].eastMeters || 0)
        let b = Number(frames[0].northMeters || 0)
        minEast = maxEast = a; minNorth = maxNorth = b
        for (let i = 1; i < frames.length; ++i) {
            a = Number(frames[i].eastMeters || 0)
            b = Number(frames[i].northMeters || 0)
            minEast = Math.min(minEast, a); maxEast = Math.max(maxEast, a)
            minNorth = Math.min(minNorth, b); maxNorth = Math.max(maxNorth, b)
        }
        const margin = Math.max(6, Math.max(maxEast - minEast, maxNorth - minNorth) * 0.12)
        minEast -= margin; maxEast += margin; minNorth -= margin; maxNorth += margin
        mapCanvas.requestPaint()
    }
    function screenPoint(east, north) {
        const sx = mapCanvas.width / Math.max(1, maxEast - minEast)
        const sy = mapCanvas.height / Math.max(1, maxNorth - minNorth)
        const scale = Math.min(sx, sy) * zoom
        const centerEast = (minEast + maxEast) / 2
        const centerNorth = (minNorth + maxNorth) / 2
        return Qt.point(mapCanvas.width / 2 + (east - centerEast) * scale + panX,
                        mapCanvas.height / 2 - (north - centerNorth) * scale + panY)
    }
    function nearestAt(x, y) {
        let best = -1, distance = 196
        for (let i = 0; i < frames.length; ++i) {
            if (!frameVisible(frames[i])) continue
            const p = screenPoint(Number(frames[i].eastMeters || 0), Number(frames[i].northMeters || 0))
            const d = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y)
            if (d < distance) { distance = d; best = i }
        }
        if (best >= 0) App.visualSurvey.selectedIndex = best
    }
    function frameVisible(frame) {
        if (filterMode === 1) return String(frame.qualityLabel || "") === "清晰"
        if (filterMode === 2) return String(frame.qualityLabel || "") !== "清晰"
        if (filterMode === 3) return Boolean(frame.anomaly)
        return true
    }
    function resetView() { zoom = 1; panX = 0; panY = 0; updateBounds() }

    color: Theme.surfaceRaised
    radius: Theme.radiusLarge
    border.width: 1
    border.color: Theme.line

    Connections {
        target: App.visualSurvey
        function onSurveyChanged() { root.updateBounds() }
        function onSelectedIndexChanged() {
            mapCanvas.requestPaint()
            notesEditor.text = String(root.selected.notes || "")
            anomalyCheck.checked = Boolean(root.selected.anomaly)
        }
    }
    Component.onCompleted: updateBounds()
    onZoomChanged: mapCanvas.requestPaint()
    onPanXChanged: mapCanvas.requestPaint()
    onPanYChanged: mapCanvas.requestPaint()
    onFilterModeChanged: mapCanvas.requestPaint()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: root.compact ? "影像地图" : "视觉巡检影像地图"
                        color: Theme.textPrimary; font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall; font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: App.visualSurvey.keyframeCount + " 张关键帧 · 采集覆盖 "
                              + Number(App.visualSurvey.spatialCoveragePercent).toFixed(0) + "%"
                              + " · 清晰帧 "
                              + Number(App.visualSurvey.qualityPassPercent).toFixed(0) + "%"
                        color: Theme.textMuted; font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny; elide: Text.ElideRight
                    }
                }
                SegmentedControl {
                    visible: !root.compact
                    Layout.preferredWidth: visible ? 258 : 0
                    options: ["全部", "清晰", "需复核", "异常"]
                    currentIndex: root.filterMode
                    onSelected: index => root.filterMode = index
                }
                CompactIconButton {
                    iconSource: "qrc:/icons/locate-fixed.svg"
                    accessibleLabel: "适应全部关键帧"
                    toolTipText: "显示全部"
                    onClicked: root.resetView()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: root.compact ? 170 : 300
                radius: Theme.radiusMedium
                color: Theme.darkMode ? "#111B25" : "#F7F9FB"
                border.width: 1; border.color: Theme.line
                clip: true

                Canvas {
                    id: mapCanvas
                    anchors.fill: parent
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset(); ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = Theme.darkMode ? "#263847" : "#DCE5ED"
                        ctx.lineWidth = 1
                        const spacing = 40
                        for (let x = 0; x < width; x += spacing) { ctx.beginPath(); ctx.moveTo(x,0); ctx.lineTo(x,height); ctx.stroke() }
                        for (let y = 0; y < height; y += spacing) { ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(width,y); ctx.stroke() }
                        if (!root.frames || root.frames.length === 0) return
                        ctx.strokeStyle = Theme.accent
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        let pathStarted = false
                        for (let i = 0; i < root.frames.length; ++i) {
                            const frame = root.frames[i]
                            if (!root.frameVisible(frame)) continue
                            const p = root.screenPoint(Number(frame.eastMeters || 0), Number(frame.northMeters || 0))
                            if (!pathStarted) { ctx.moveTo(p.x, p.y); pathStarted = true } else ctx.lineTo(p.x, p.y)
                        }
                        ctx.stroke()
                        for (let i = 0; i < root.frames.length; ++i) {
                            const frame = root.frames[i]
                            if (!root.frameVisible(frame)) continue
                            const p = root.screenPoint(Number(frame.eastMeters || 0), Number(frame.northMeters || 0))
                            const heading = Number(frame.headingDegrees || 0) * Math.PI / 180
                            const cone = root.compact ? 15 : 22
                            ctx.fillStyle = i === App.visualSurvey.selectedIndex
                                            ? "rgba(23,105,224,0.22)" : "rgba(23,105,224,0.09)"
                            ctx.beginPath(); ctx.moveTo(p.x, p.y)
                            ctx.lineTo(p.x + Math.sin(heading - 0.7) * cone,
                                       p.y - Math.cos(heading - 0.7) * cone)
                            ctx.lineTo(p.x + Math.sin(heading + 0.7) * cone,
                                       p.y - Math.cos(heading + 0.7) * cone)
                            ctx.closePath(); ctx.fill()
                            ctx.beginPath(); ctx.arc(p.x, p.y, i === App.visualSurvey.selectedIndex ? 6 : 4, 0, Math.PI * 2)
                            ctx.fillStyle = Boolean(frame.anomaly) ? Theme.danger
                                          : String(frame.qualityLabel || "") === "清晰" ? Theme.accent : Theme.warning
                            ctx.fill(); ctx.strokeStyle = Theme.surface; ctx.lineWidth = 2; ctx.stroke()
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onPressed: mouse => {
                        root.dragging = true; root.dragOrigin = Qt.point(mouse.x, mouse.y)
                        root.startPanX = root.panX; root.startPanY = root.panY
                    }
                    onPositionChanged: mouse => {
                        if (!root.dragging) return
                        root.panX = root.startPanX + mouse.x - root.dragOrigin.x
                        root.panY = root.startPanY + mouse.y - root.dragOrigin.y
                    }
                    onReleased: mouse => {
                        const moved = Math.abs(mouse.x - root.dragOrigin.x) + Math.abs(mouse.y - root.dragOrigin.y)
                        root.dragging = false
                        if (moved < 6) root.nearestAt(mouse.x, mouse.y)
                    }
                    onWheel: wheel => {
                        root.zoom = Math.max(0.5, Math.min(8, root.zoom * (wheel.angleDelta.y > 0 ? 1.15 : 0.87)))
                        wheel.accepted = true
                    }
                }
                Column {
                    anchors.centerIn: parent
                    visible: App.visualSurvey.keyframeCount === 0
                    spacing: 5
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: App.visualSurvey.active ? "正在等待第一张关键帧" : "还没有影像地图"
                        color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "飞行和转向后会自动扩展"
                        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny
                    }
                }
                Text {
                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 9
                    text: "滚轮缩放 · 拖动平移 · 点击圆点查看"
                    color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny
                }
                Text {
                    visible: !root.compact
                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 9
                    text: "采集覆盖：候选位置/方向中成功保存关键帧的比例；清晰帧单独统计"
                    color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny
                }
            }
            ListView {
                visible: !root.compact && root.frames.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 74 : 0
                orientation: ListView.Horizontal
                spacing: 8
                clip: true
                model: root.frames
                delegate: Rectangle {
                    id: thumbnailDelegate
                    required property var modelData
                    required property int index
                    width: 106; height: 68; radius: Theme.radiusSmall
                    visible: root.frameVisible(modelData)
                    color: Theme.surface
                    border.width: App.visualSurvey.selectedIndex === index ? 2 : 1
                    border.color: App.visualSurvey.selectedIndex === index ? Theme.accent
                                  : Boolean(modelData.anomaly) ? Theme.danger
                                  : String(modelData.qualityLabel || "") === "清晰" ? Theme.lineStrong : Theme.warning
                    Image {
                        anchors.fill: parent; anchors.margins: 3
                        source: thumbnailDelegate.modelData.source || ""
                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: false
                    }
                    MouseArea { anchors.fill: parent; onClicked: App.visualSurvey.selectedIndex = thumbnailDelegate.index }
                }
            }
        }

        Rectangle {
            visible: !root.compact
            Layout.preferredWidth: visible ? Math.min(360, root.width * 0.34) : 0
            Layout.fillHeight: true
            radius: Theme.radiusMedium
            color: Theme.surface
            border.width: 1; border.color: Theme.line
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 8
                Image {
                    Layout.fillWidth: true; Layout.preferredHeight: 190
                    source: root.selected.source || ""
                    fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false
                }
                Text {
                    Layout.fillWidth: true
                    text: root.selected.timeLabel
                          ? "关键帧 " + (Number(root.selected.index || 0) + 1) + " · " + root.selected.timeLabel
                          : "选择地图上的关键帧"
                    color: Theme.textPrimary; font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall; font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: root.selected.timeLabel
                          ? String(root.selected.areaLabel || "") + " · 高度 "
                            + Number(root.selected.altitudeMeters || 0).toFixed(1) + " m · 朝向 "
                            + Number(root.selected.headingDegrees || 0).toFixed(0) + "°"
                          : ""
                    color: Theme.textMuted; font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: root.selected.timeLabel
                          ? String(root.selected.qualityLabel || "") + " · "
                            + String(root.selected.qualityReason || "") + " · 质量 "
                            + Number(root.selected.qualityScore || 0).toFixed(0)
                          : ""
                    color: String(root.selected.qualityLabel || "") === "清晰" ? Theme.success : Theme.warning
                    font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny
                }
                TextArea {
                    id: notesEditor
                    visible: Boolean(root.selected.timeLabel)
                    Layout.fillWidth: true; Layout.preferredHeight: 72
                    placeholderText: "添加检查备注，例如墙面裂缝、窗户破损…"
                    wrapMode: TextEdit.Wrap
                    color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall
                    background: Rectangle { radius: Theme.radiusMedium; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.lineStrong }
                }
                RowLayout {
                    visible: Boolean(root.selected.timeLabel)
                    Layout.fillWidth: true
                    CheckBox {
                        id: anomalyCheck
                        text: "标记异常"
                        checked: Boolean(root.selected.anomaly)
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: "保存备注"; compact: true; kind: "secondary"
                        onClicked: App.updateVisualSurveyKeyframe(notesEditor.text, anomalyCheck.checked)
                    }
                }
                Item { Layout.fillHeight: true }
                ActionButton {
                    visible: App.visualSurvey.historical && Boolean(root.selected.timeLabel)
                    Layout.fillWidth: true
                    text: "跳到这张照片的回放时刻"
                    iconSource: "qrc:/icons/history.svg"
                    kind: "secondary"
                    onClicked: root.replayRequested(Number(root.selected.elapsedMs || 0))
                }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        Layout.fillWidth: true; text: "导出原图"; compact: true
                        iconSource: "qrc:/icons/file-down.svg"; kind: "secondary"
                        enabled: Boolean(root.selected.timeLabel)
                        onClicked: App.exportVisualSurveyKeyframe(Number(root.selected.index || 0))
                    }
                    ActionButton {
                        Layout.fillWidth: true; text: "打开位置"; compact: true
                        iconSource: "qrc:/icons/folder-open.svg"; kind: "secondary"
                        enabled: Boolean(root.selected.timeLabel)
                        onClicked: App.openVisualSurveyFolder()
                    }
                }
            }
        }
    }
}
