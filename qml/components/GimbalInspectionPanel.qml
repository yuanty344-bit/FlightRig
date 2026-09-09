pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

ColumnLayout {
    id: root
    signal chooseTargetRequested()
    property bool expanded: true
    readonly property var inspection: App.gimbalInspection
    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: "目标锁定与多角度巡检" + (root.inspection.hasTarget
                ? " · 已拍 " + root.inspection.savedCount + "/4 · 合格 " + root.inspection.completedCount + "/4" : "")
            wrapMode: Text.Wrap
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
            font.bold: true
        }
        CompactIconButton {
            iconSource: "qrc:/icons/chevron-down.svg"
            iconRotation: root.expanded ? 180 : 0
            accessibleLabel: root.expanded ? "收起巡检" : "展开巡检"
            toolTipText: accessibleLabel
            onClicked: root.expanded = !root.expanded
        }
    }
    ColumnLayout {
        visible: root.expanded
        Layout.fillWidth: true
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: root.inspection.status
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                width: Math.min(220, parent.width)
                height: 36
                text: root.inspection.name
                readOnly: root.inspection.historical
                placeholderText: "目标名称"
                maximumLength: 80
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                leftPadding: 10
                background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusMedium; border.color: Theme.lineStrong }
                onEditingFinished: root.inspection.name = text
                Accessible.name: "巡检目标名称"
            }
            ActionButton {
                visible: !root.inspection.historical
                text: root.inspection.hasTarget ? "重新选点" : "选择目标"
                kind: "secondary"
                compact: true
                enabled: App.simulationRunning && App.sensorServiceConnected
                disabledReason: "启动云台仿真并连接 ROS 2 数据服务后才能选点"
                onClicked: {
                    root.inspection.picking = true
                    if (root.inspection.picking) root.chooseTargetRequested()
                }
            }
            ActionButton {
                visible: !root.inspection.historical
                text: root.inspection.active ? "暂停锁定" : "开始锁定"
                compact: true
                enabled: root.inspection.hasTarget && App.simulationRunning
                disabledReason: "先在三维视图选择建筑表面目标"
                onClicked: root.inspection.active ? root.inspection.pause() : root.inspection.start()
            }
        }
        Text {
            Layout.fillWidth: true
            text: "坐标锁定，只控制云台，不自动飞行。以选点时方向为正面：左/右斜视约 35°，较高视角增加约 25°。"
            wrapMode: Text.Wrap
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
        }
        GridLayout {
            Layout.fillWidth: true
            columns: width < 600 ? 2 : 4
            columnSpacing: 8
            rowSpacing: 8
            Repeater {
                model: root.inspection.views
                delegate: Rectangle {
                    id: tile
                    required property int index
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: root.inspection.historical ? 110 : 50
                    radius: Theme.radiusMedium
                    color: root.inspection.guideView === tile.index ? Theme.accentSoft : Theme.surfaceRaised
                    border.color: root.inspection.guideView === tile.index ? Theme.accent : Theme.line
                    Image {
                        anchors.fill: parent
                        anchors.margins: 5
                        anchors.bottomMargin: 29
                        source: root.inspection.historical ? tile.modelData.source : ""
                        visible: root.inspection.historical
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        sourceSize.width: 300
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        text: tile.modelData.label + " · " + (tile.modelData.complete ? "质量合格"
                            : tile.modelData.saved ? "已拍·需复核" : "待采集")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: tile.modelData.complete ? Theme.success : Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                    TapHandler {
                        onTapped: {
                            root.inspection.selectedView = tile.index
                            if (root.inspection.historical && tile.modelData.saved)
                                App.seekReplay(Number(tile.modelData.elapsedMs || 0))
                        }
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: tile.modelData.label + (tile.modelData.complete ? "质量合格"
                        : tile.modelData.saved ? "已保存，质量需复核" : "待采集")
                    activeFocusOnTab: true
                    Keys.onReturnPressed: {
                        root.inspection.selectedView = tile.index
                        if (root.inspection.historical && tile.modelData.saved)
                            App.seekReplay(Number(tile.modelData.elapsedMs || 0))
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 235
            visible: !root.inspection.historical && !!root.inspection.observation.available
            color: Theme.surfaceRaised
            radius: Theme.radiusMedium
            border.color: Theme.line
            Canvas {
                id: overview
                anchors.fill: parent
                anchors.margins: 10
                readonly property var observation: root.inspection.observation
                readonly property color ink: Theme.textPrimary
                readonly property color accent: Theme.accent
                onObservationChanged: requestPaint()
                onInkChanged: requestPaint()
                onAccentChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (!observation.available) return
                    const range = Math.max(27, Math.abs(observation.x) + 5, Math.abs(observation.y) + 5)
                    const scale = Math.min((width - 80) / (2 * range), (height - 85) / (2 * range))
                    const cx = width / 2
                    const cy = height / 2
                    ctx.font = "12px sans-serif"
                    ctx.textAlign = "center"
                    ctx.fillStyle = ink
                    ctx.fillText("俯视方向参考（不是障碍地图）", cx, 12)
                    ctx.fillRect(cx - 4, cy - 4, 8, 8)
                    ctx.fillText("目标", cx, cy - 12)
                    const labels = ["正面", "左斜视", "右斜视"]
                    const angles = [0, 35, -35]
                    for (let i = 0; i < 3; ++i) {
                        const a = angles[i] * Math.PI / 180
                        const x = cx - 20 * Math.sin(a) * scale
                        const y = cy + 20 * Math.cos(a) * scale
                        ctx.strokeStyle = ink
                        ctx.beginPath(); ctx.arc(x, y, 5, 0, 2*Math.PI); ctx.stroke()
                        ctx.fillStyle = ink
                        ctx.textAlign = i === 1 ? "right" : i === 2 ? "left" : "center"
                        ctx.fillText(labels[i], x + (i === 1 ? -8 : i === 2 ? 8 : 0), y + (i === 0 ? 19 : 4))
                    }
                    ctx.textAlign = "center"
                    ctx.fillStyle = accent
                    const px = cx + observation.x * scale
                    const py = cy + observation.y * scale
                    ctx.beginPath(); ctx.arc(px, py, 5, 0, 2*Math.PI); ctx.fill()
                    ctx.fillText("当前位置", Math.max(32, Math.min(width-32, px)), Math.max(28, py-10))
                    ctx.fillStyle = ink
                    ctx.fillText("较高视角：与正面同方向，另需升高", cx, height-3)
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !root.inspection.historical && !!root.inspection.observation.available
            text: {
                const o = root.inspection.observation
                if (!o.available) return ""
                return "所选视角 20 m 参考点：向" + (o.deltaEast >= 0 ? "东 " : "西 ")
                    + Math.abs(o.deltaEast).toFixed(1) + " m，向" + (o.deltaNorth >= 0 ? "北 " : "南 ")
                    + Math.abs(o.deltaNorth).toFixed(1) + " m，" + (o.deltaUp >= 0 ? "升高 " : "降低 ")
                    + Math.abs(o.deltaUp).toFixed(1) + " m。不是键盘左右方向，也未验证沿途安全，请自行避障。"
                    + (o.alignmentError >= 0 ? " 当前对准误差 " + o.alignmentError.toFixed(1) + "°（要求 ≤1.5°）。" : "")
            }
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
        }
        Text {
            Layout.fillWidth: true
            text: root.inspection.guidance || ""
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
        }
        Flow {
            visible: !root.inspection.historical
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                text: root.inspection.automatic ? "自动匹配并采集" : "手动选择并采集"
                kind: "secondary"
                compact: true
                toolTipText: "自动模式匹配当前位置符合的未完成视角；点击卡片查看参考，关闭自动模式后才固定采集所选视角"
                onClicked: root.inspection.automatic = !root.inspection.automatic
            }
            ActionButton {
                text: "采集本视角"
                kind: "secondary"
                compact: true
                enabled: root.inspection.active && root.inspection.canRecord
                disabledReason: "需开始锁定，并在测试方案中选择预览并记录"
                onClicked: root.inspection.capture()
            }
            ActionButton {
                text: "补拍本视角"
                kind: "secondary"
                compact: true
                enabled: root.inspection.hasTarget && root.inspection.canRecord
                disabledReason: "选择目标并启用记录后才能补拍"
                onClicked: root.inspection.retake()
            }
        }
        Text {
            Layout.fillWidth: true
            text: "已拍与质量合格分别统计。低纹理照片保存为需复核，不能当作质量合格。4 个方向不代表建筑表面覆盖率；遮挡只检查已知建筑。"
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
            wrapMode: Text.Wrap
        }
    }
}
