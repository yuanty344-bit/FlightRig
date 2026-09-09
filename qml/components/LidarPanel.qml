pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import FlightBench

SurfaceCard {
    id: root

    readonly property bool dataReady: App.sensorServiceConnected
                                      && App.lidarScanPoints.length > 0
    readonly property bool mapReady: App.lidarMapping.available
    readonly property real maximumRangeMeters: 30.0
    property int viewMode: 0
    property real mapZoom: 1.0
    property real mapPanX: 0.0
    property real mapPanY: 0.0

    color: Theme.surface

    function directionLabel(angle) {
        if (!root.dataReady) return "—"
        const normalized = ((Number(angle) + 180) % 360 + 360) % 360 - 180
        const absolute = Math.abs(normalized)
        if (absolute < 15) return "正前方"
        if (absolute >= 165) return "正后方"
        if (normalized > 0)
            return absolute < 75 ? "左前方" : absolute < 105 ? "左侧" : "左后方"
        return absolute < 75 ? "右前方" : absolute < 105 ? "右侧" : "右后方"
    }

    function statusLabel() {
        if (root.dataReady) return "扫描正常"
        if (App.sensorServiceProblemCode.length > 0) return "需要处理"
        if (App.sensorServiceRunning) return "等待扫描"
        return "尚未启动"
    }

    function resetMapView() {
        root.mapZoom = 1.0
        root.mapPanX = 0.0
        root.mapPanY = 0.0
    }

    function setMapZoom(nextZoom, anchorX, anchorY) {
        const oldZoom = root.mapZoom
        const bounded = Math.max(0.75, Math.min(6.0, Number(nextZoom)))
        if (Math.abs(bounded - oldZoom) < 0.001) return
        const centerX = mapViewport.width / 2
        const centerY = mapViewport.height / 2
        const relativeX = Number(anchorX) - centerX
        const relativeY = Number(anchorY) - centerY
        const ratio = bounded / oldZoom
        root.mapPanX = relativeX - (relativeX - root.mapPanX) * ratio
        root.mapPanY = relativeY - (relativeY - root.mapPanY) * ratio
        root.mapZoom = bounded
    }

    function syncMapRendering() {
        App.lidarMapping.setVisualizationActive(root.visible && root.viewMode === 1)
    }

    onViewModeChanged: {
        root.syncMapRendering()
        if (root.viewMode === 1 && root.mapReady)
            App.lidarMapping.refreshVisualization()
    }
    onVisibleChanged: root.syncMapRendering()
    Component.onCompleted: root.syncMapRendering()
    Component.onDestruction: App.lidarMapping.setVisualizationActive(false)

    Connections {
        target: App
        function onLidarScanChanged() { scanCanvas.requestPaint() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ThemedIcon {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                source: "qrc:/icons/radar.svg"
                iconColor: root.dataReady ? Theme.success : Theme.textMuted
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    text: "二维激光雷达"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSubtitle
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: "机头朝上 · 机体周围实时距离"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    elide: Text.ElideRight
                }
            }
            SegmentedControl {
                Layout.preferredWidth: 186
                options: ["实时雷达", "二维 SLAM"]
                currentIndex: root.viewMode
                onSelected: index => root.viewMode = index
            }
            Rectangle {
                Layout.preferredWidth: statusText.implicitWidth + 22
                Layout.preferredHeight: 28
                radius: 14
                color: root.dataReady ? Theme.successSoft
                       : App.sensorServiceProblemCode.length > 0 ? Theme.warningSoft
                                                                  : Theme.surfaceRaised
                border.width: 1
                border.color: root.dataReady
                              ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.28)
                              : Theme.line
                Row {
                    anchors.centerIn: parent
                    spacing: 7
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 7
                        height: 7
                        radius: 4
                        color: root.dataReady ? Theme.success
                               : App.sensorServiceRunning ? Theme.accent : Theme.textFaint
                    }
                    Text {
                        id: statusText
                        text: root.statusLabel()
                        color: root.dataReady ? Theme.success : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        font.weight: Font.Medium
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 250
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 360
                radius: Theme.radiusLarge
                color: Theme.darkMode ? "#0E1721" : "#F6F9FC"
                border.width: 1
                border.color: Theme.line
                clip: true

                Canvas {
                    id: scanCanvas
                    visible: root.viewMode === 0
                    anchors.fill: parent
                    anchors.margins: 16
                    antialiasing: true

                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        const cx = width / 2
                        const cy = height / 2
                        const radius = Math.max(30, Math.min(width, height) * 0.405)
                        const scale = radius / root.maximumRangeMeters
                        const grid = Theme.darkMode ? "#2B3C4C" : "#D9E2EB"

                        ctx.lineWidth = 1
                        ctx.strokeStyle = grid
                        for (let ring = 1; ring <= 3; ++ring) {
                            ctx.beginPath()
                            ctx.arc(cx, cy, radius * ring / 3, 0, Math.PI * 2)
                            ctx.stroke()
                        }
                        ctx.beginPath()
                        ctx.moveTo(cx - radius, cy)
                        ctx.lineTo(cx + radius, cy)
                        ctx.moveTo(cx, cy - radius)
                        ctx.lineTo(cx, cy + radius)
                        ctx.stroke()

                        ctx.fillStyle = Theme.textFaint
                        ctx.font = "11px '" + Theme.fontFamily + "'"
                        ctx.textAlign = "center"
                        ctx.fillText("前", cx, cy - radius - 8)
                        ctx.fillText("后", cx, cy + radius + 15)
                        ctx.fillText("右", cx + radius + 12, cy + 4)
                        ctx.fillText("左", cx - radius - 12, cy + 4)
                        ctx.textAlign = "left"
                        ctx.fillText("10", cx + radius / 3 + 4, cy - 5)
                        ctx.fillText("20", cx + radius * 2 / 3 + 4, cy - 5)
                        ctx.fillText("30 m", cx + radius + 4, cy - 5)

                        if (root.dataReady) {
                            const points = App.lidarScanPoints
                            ctx.fillStyle = Theme.accent
                            for (let index = 0; index < points.length; ++index) {
                                const point = points[index]
                                const px = cx + Number(point.x || 0) * scale
                                const py = cy + Number(point.y || 0) * scale
                                ctx.fillRect(px - 1.25, py - 1.25, 2.5, 2.5)
                            }

                            const nearestAngle = App.lidarNearestAngleDegrees * Math.PI / 180
                            const nearestRange = App.lidarNearestDistanceMeters
                            const nx = cx - Math.sin(nearestAngle) * nearestRange * scale
                            const ny = cy - Math.cos(nearestAngle) * nearestRange * scale
                            ctx.lineWidth = 1.5
                            ctx.strokeStyle = Theme.warning
                            ctx.beginPath()
                            ctx.moveTo(cx, cy)
                            ctx.lineTo(nx, ny)
                            ctx.stroke()
                            ctx.fillStyle = Theme.warning
                            ctx.beginPath()
                            ctx.arc(nx, ny, 4, 0, Math.PI * 2)
                            ctx.fill()
                        }

                        // A small top-view aircraft marker makes the frame of
                        // reference unambiguous: the pointed nose always faces up.
                        ctx.fillStyle = Theme.accent
                        ctx.beginPath()
                        ctx.moveTo(cx, cy - 12)
                        ctx.lineTo(cx + 7, cy + 8)
                        ctx.lineTo(cx, cy + 4)
                        ctx.lineTo(cx - 7, cy + 8)
                        ctx.closePath()
                        ctx.fill()
                        ctx.strokeStyle = Theme.surface
                        ctx.lineWidth = 1.5
                        ctx.stroke()
                    }
                }

                Item {
                    id: mapViewport
                    visible: root.viewMode === 1 && root.mapReady
                    anchors.fill: parent
                    clip: true

                    Item {
                        id: mapLayer
                        width: mapViewport.width * root.mapZoom
                        height: mapViewport.height * root.mapZoom
                        x: (mapViewport.width - width) / 2 + root.mapPanX
                        y: (mapViewport.height - height) / 2 + root.mapPanY

                        Image {
                            anchors.fill: parent
                            anchors.margins: 10 * root.mapZoom
                            source: App.lidarMapping.mapDataUrl
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            retainWhileLoading: true
                            cache: false
                            mipmap: true
                            smooth: true
                        }
                    }

                    MouseArea {
                        id: mapNavigationArea
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        hoverEnabled: true
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                        property real previousX: 0
                        property real previousY: 0

                        onPressed: mouse => {
                            previousX = mouse.x
                            previousY = mouse.y
                        }
                        onPositionChanged: mouse => {
                            if (!pressed) return
                            root.mapPanX += mouse.x - previousX
                            root.mapPanY += mouse.y - previousY
                            previousX = mouse.x
                            previousY = mouse.y
                        }
                        onWheel: wheel => {
                            const factor = wheel.angleDelta.y > 0 ? 1.2 : (1 / 1.2)
                            root.setMapZoom(root.mapZoom * factor, wheel.x, wheel.y)
                            wheel.accepted = true
                        }
                        onDoubleClicked: root.resetMapView()
                    }

                    Rectangle {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 12
                        width: mapTools.implicitWidth + 10
                        height: 38
                        radius: 9
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.line

                        Row {
                            id: mapTools
                            anchors.centerIn: parent
                            spacing: 2
                            CompactIconButton {
                                iconSource: "qrc:/icons/zoom-out.svg"
                                accessibleLabel: "缩小地图"
                                toolTipText: "缩小二维 SLAM 地图"
                                enabled: root.mapZoom > 0.751
                                disabledReason: "已是最小比例"
                                onClicked: root.setMapZoom(root.mapZoom / 1.25,
                                                           mapViewport.width / 2,
                                                           mapViewport.height / 2)
                            }
                            CompactIconButton {
                                iconSource: "qrc:/icons/zoom-in.svg"
                                accessibleLabel: "放大地图"
                                toolTipText: "放大二维 SLAM 地图"
                                enabled: root.mapZoom < 5.99
                                disabledReason: "已是最大比例"
                                onClicked: root.setMapZoom(root.mapZoom * 1.25,
                                                           mapViewport.width / 2,
                                                           mapViewport.height / 2)
                            }
                            CompactIconButton {
                                iconSource: "qrc:/icons/locate-fixed.svg"
                                accessibleLabel: "复位地图视图"
                                toolTipText: "恢复自适应大小和居中位置"
                                onClicked: root.resetMapView()
                            }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 12
                        width: mapHint.implicitWidth + 18
                        height: 28
                        radius: 14
                        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.9)
                        border.width: 1
                        border.color: Theme.line
                        Text {
                            id: mapHint
                            anchors.centerIn: parent
                            text: "滚轮缩放 · 拖动平移 · 双击复位 · "
                                  + Math.round(root.mapZoom * 100) + "%"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                        }
                    }
                }

                Rectangle {
                    visible: root.viewMode === 0 ? !root.dataReady : !root.mapReady
                    anchors.centerIn: parent
                    width: emptyRow.implicitWidth + 26
                    height: 36
                    radius: 18
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.line
                    Row {
                        id: emptyRow
                        anchors.centerIn: parent
                        spacing: 8
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 7
                            height: 7
                            radius: 4
                            color: App.sensorServiceRunning ? Theme.accent : Theme.textFaint
                        }
                        Text {
                            text: root.viewMode === 0
                                  ? (App.sensorServiceRunning ? "等待雷达数据" : "启动仿真后显示")
                                  : (App.sensorServiceRunning
                                     ? App.lidarMapping.statusMessage
                                     : "启动 LiDAR 仿真并飞行后逐步建图")
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.Medium
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 228
                Layout.minimumWidth: 205
                Layout.maximumWidth: 250
                Layout.fillHeight: true
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 88
                    radius: Theme.radiusLarge
                    color: root.viewMode === 0 && root.dataReady
                           && App.lidarNearestDistanceMeters < 3.0
                           ? Theme.warningSoft : Theme.surfaceRaised
                    border.width: 1
                    border.color: root.viewMode === 0 && root.dataReady
                                  && App.lidarNearestDistanceMeters < 3.0
                                  ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.35)
                                  : Theme.line
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 2
                        Text {
                            text: root.viewMode === 0 ? "最近障碍" : "已探索面积"
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                        }
                        Text {
                            text: root.viewMode === 0
                                  ? (root.dataReady
                                     ? App.lidarNearestDistanceMeters.toFixed(2) + " m" : "—")
                                  : (root.mapReady
                                     ? App.lidarMapping.exploredAreaSquareMeters.toFixed(1) + " m²" : "—")
                            color: root.viewMode === 0 && root.dataReady
                                   && App.lidarNearestDistanceMeters < 3.0
                                   ? Theme.warning : Theme.textPrimary
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.textTitle
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: root.viewMode === 0
                                  ? root.directionLabel(App.lidarNearestAngleDegrees)
                                  : (root.mapReady
                                     ? App.lidarMapping.widthMeters.toFixed(1) + " × "
                                       + App.lidarMapping.heightMeters.toFixed(1) + " m"
                                     : "随飞行范围自动扩展")
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.viewMode === 0 ? 120 : 142
                    radius: Theme.radiusLarge
                    color: Theme.surfaceRaised
                    border.width: 1
                    border.color: Theme.line
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 7
                        Text {
                            text: "扫描信息"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.DemiBold
                        }
                        Repeater {
                            model: root.viewMode === 0 ? [
                                { label: "覆盖范围", value: root.dataReady
                                  ? Math.round(App.lidarFieldOfViewDegrees) + "°" : "—" },
                                { label: "最远距离", value: "30 m" },
                                { label: "刷新频率", value: root.dataReady
                                  ? App.lidarScanHz.toFixed(1) + " Hz" : "—" },
                                { label: "显示点数", value: root.dataReady
                                  ? String(App.lidarScanPoints.length) : "—" }
                            ] : [
                                { label: "SLAM 关键帧", value: String(App.lidarMapping.keyframeCount) },
                                { label: "位姿图约束", value: String(App.lidarMapping.poseGraphConstraintCount) },
                                { label: "扫描匹配", value: String(App.lidarMapping.scanMatchedCount)
                                  + " · " + Math.round(App.lidarMapping.localizationConfidence * 100) + "%" },
                                { label: "回环 / 优化", value: String(App.lidarMapping.loopClosureCount)
                                  + " / " + String(App.lidarMapping.optimizationCount) },
                                { label: "已过滤倾斜扫描", value: String(App.lidarMapping.tiltRejectedScanCount) }
                            ]
                            delegate: RowLayout {
                                id: infoRow
                                required property var modelData
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: String(infoRow.modelData.label)
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textTiny
                                }
                                Text {
                                    text: String(infoRow.modelData.value)
                                    color: Theme.textPrimary
                                    font.family: Theme.monoFontFamily
                                    font.pixelSize: Theme.textTiny
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.viewMode === 0 ? 70 : 92
                    radius: Theme.radiusLarge
                    color: Theme.accentSoft
                    border.width: 1
                    border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 3
                        Text {
                            text: root.viewMode === 0 ? "怎么看这张图" : "地图含义"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.viewMode === 0
                                  ? "上方是机头；点越靠近中心，障碍越近。"
                                  : "深色是障碍，白色是空闲区域；紫色虚线表示回环约束，位姿图优化后会重建整张地图。"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                RowLayout {
                    visible: root.viewMode === 1
                    Layout.fillWidth: true
                    spacing: 4
                    CompactIconButton {
                        iconSource: "qrc:/icons/trash-2.svg"
                        accessibleLabel: "清空二维 SLAM 地图"
                        toolTipText: "清空当前二维 SLAM 地图并重新开始建图"
                        enabled: root.mapReady && !App.replayActive
                        disabledReason: App.replayActive ? "回放地图会随时间轴自动重建" : "地图还是空的"
                        onClicked: App.lidarMapping.clear()
                    }
                    CompactIconButton {
                        iconSource: "qrc:/icons/save.svg"
                        accessibleLabel: "保存二维 SLAM 地图"
                        toolTipText: "保存 PNG、PGM、YAML 和 SLAM 元数据"
                        enabled: root.mapReady
                        disabledReason: "飞行一段距离后才能保存地图"
                        onClicked: App.saveLidarMap()
                    }
                    CompactIconButton {
                        iconSource: "qrc:/icons/folder-open.svg"
                        accessibleLabel: "打开地图文件夹"
                        toolTipText: "打开已导出的 LiDAR 地图文件夹"
                        onClicked: App.openLidarMapsFolder()
                    }
                    Item { Layout.fillWidth: true }
                }

                Item { Layout.fillHeight: true }
                Text {
                    Layout.fillWidth: true
                    text: "仅监测，不控制飞行"
                    color: Theme.textFaint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
