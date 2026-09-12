pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import FlightBench
import "../../controls"

Rectangle {
    id: sensorSummary
    property var sensor: ({})
    property string sizeLabel: "0 B"
    property bool canOpenHistory: false
    property bool canDeleteData: false
    signal visualSurveyRequested()
    signal depthMapRequested()
    signal deleteRequested()
    visible: Boolean(sensor.available || sensor.deleted
                     || String(sensor.statusLabel || "").length > 0)
    Layout.fillWidth: true
    Layout.preferredHeight: visible
                            ? (Boolean(sensor.lidarMapAvailable)
                               || Boolean(sensor.depthMapAvailable)
                               || Boolean(sensor.visualSurveyAvailable) ? 128 : 94)
                            : 0
    Layout.topMargin: visible ? 12 : 0
    radius: Theme.radiusMedium
    color: Theme.surfaceRaised
    border.width: 1
    border.color: Theme.line

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        spacing: 10
        ThemedIcon {
            Layout.preferredWidth: 19
            Layout.preferredHeight: 19
            source: String(sensorSummary.sensor.kind || "").indexOf("lidar") >= 0
                    ? "qrc:/icons/radar.svg" : "qrc:/icons/camera.svg"
            iconColor: sensorSummary.sensor.available ? Theme.accent : Theme.textFaint
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Text {
                Layout.fillWidth: true
                text: sensorSummary.sensor.replayable
                      ? (sensorSummary.sensor.recordingComplete === false
                         ? "传感器数据 · 部分数据可回放"
                         : "传感器数据 · 可同步回放")
                      : sensorSummary.sensor.available
                        ? "传感器数据 · 仅预览统计" : "传感器数据"
                color: sensorSummary.sensor.replayable
                       && sensorSummary.sensor.recordingComplete === false
                       ? Theme.warning : Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                font.weight: Font.Medium
            }
            Text {
                Layout.fillWidth: true
                text: sensorSummary.sensor.deleted
                      ? "已单独删除，飞行遥测与报告仍保留"
                      : Number(sensorSummary.sensor.rgbFrameCount || 0)
                        + Number(sensorSummary.sensor.depthFrameCount || 0)
                        + " 帧画面 · "
                        + Number(sensorSummary.sensor.lidarScanCount || 0)
                        + " 次扫描 · "
                        + sensorSummary.sizeLabel
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                elide: Text.ElideRight
            }
            Text {
                visible: Boolean(sensorSummary.sensor.available)
                         && !Boolean(sensorSummary.sensor.deleted)
                Layout.fillWidth: true
                text: "可用率 "
                      + Number(sensorSummary.sensor.dataAvailabilityPercent || 0).toFixed(0)
                      + "% · 平均 "
                      + Number(sensorSummary.sensor.averageFrameRateHz || 0).toFixed(1)
                      + " Hz · 最长中断 "
                      + Number(sensorSummary.sensor.maximumDataGapSeconds || 0).toFixed(1)
                      + " s"
                      + (Number(sensorSummary.sensor.nearestObstacleMeters || 0) > 0
                         ? " · 最近障碍 "
                           + Number(sensorSummary.sensor.nearestObstacleMeters).toFixed(1)
                           + " m" : "")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                elide: Text.ElideRight
            }
            Text {
                visible: Boolean(sensorSummary.sensor.lidarMapAvailable)
                Layout.fillWidth: true
                text: "位姿图 SLAM · "
                      + Number((sensorSummary.sensor.lidarMap || {}).keyframeCount || 0)
                      + " 个关键帧 · "
                      + Number((sensorSummary.sensor.lidarMap || {}).loopClosureCount || 0)
                      + " 次回环 · 匹配置信度 "
                      + Math.round(Number((sensorSummary.sensor.lidarMap || {}).localizationConfidence || 0) * 100)
                      + "% · 已随飞行记录保存"
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                elide: Text.ElideRight
            }
            Text {
                visible: Boolean(sensorSummary.sensor.depthMapAvailable)
                Layout.fillWidth: true
                text: "DepthFusion 三维表面 · "
                      + Number((sensorSummary.sensor.depthObstacleMap || {}).confirmedVoxelCount || 0)
                      + " 个稳定体素 · 置信度 "
                      + Number((sensorSummary.sensor.depthObstacleMap || {}).mapConfidencePercent || 0).toFixed(0)
                      + "% · "
                      + Number((sensorSummary.sensor.depthObstacleMap || {}).meshTriangleCount || 0)
                      + " 个三角面 · 已随飞行记录保存"
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                elide: Text.ElideRight
            }
            Text {
                visible: Boolean(sensorSummary.sensor.visualSurveyAvailable)
                Layout.fillWidth: true
                readonly property var survey: sensorSummary.sensor.visualSurvey || ({})
                text: "视觉巡检影像地图 · "
                      + Number(survey.keyframeCount || 0) + " 张关键帧 · "
                      + Number(survey.goodKeyframeCount || 0) + " 张清晰 · 采集覆盖 "
                      + Number(survey.spatialCoveragePercent || 0).toFixed(0)
                      + "%"
                      + " · 已随飞行记录保存"
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                elide: Text.ElideRight
            }
        }
        CompactIconButton {
            visible: Boolean(sensorSummary.sensor.visualSurveyAvailable)
                     && !Boolean(sensorSummary.sensor.deleted)
            iconSource: "qrc:/icons/map-pinned.svg"
            accessibleLabel: "打开视觉巡检影像地图"
            toolTipText: "查看关键帧、位置与备注"
            enabled: sensorSummary.canOpenHistory
            disabledReason: "停止当前仿真后才能打开历史影像地图"
            onClicked: sensorSummary.visualSurveyRequested()
        }
        CompactIconButton {
            visible: Boolean(sensorSummary.sensor.depthMapAvailable)
                     && !Boolean(sensorSummary.sensor.deleted)
            iconSource: "qrc:/icons/box.svg"
            accessibleLabel: "打开保存的三维地图"
            toolTipText: "直接查看 DepthFusion 地图"
            enabled: sensorSummary.canOpenHistory
            disabledReason: "停止当前仿真后才能打开历史三维地图"
            onClicked: sensorSummary.depthMapRequested()
        }
        CompactIconButton {
            visible: Boolean(sensorSummary.sensor.replayable)
                     && sensorSummary.canDeleteData
            iconSource: "qrc:/icons/trash-2.svg"
            accessibleLabel: "仅删除本次传感器数据"
            toolTipText: "删除传感器数据"
            danger: true
            onClicked: sensorSummary.deleteRequested()
        }
    }
}
