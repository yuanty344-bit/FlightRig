pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Dialog {
    id: root
    property string sessionId: ""
    signal replayRequested(string sessionId, int elapsedMs)
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 36 : 1120, 1240)
    height: Math.min(parent ? parent.height - 36 : 760, 820)
    modal: true; padding: 0
    closePolicy: Dialog.CloseOnEscape
    onClosed: App.closeLoadedVisualSurvey()
    background: Rectangle { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.lineStrong }
    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 74
            Layout.leftMargin: 22; Layout.rightMargin: 14
            ColumnLayout {
                Layout.fillWidth: true; spacing: 2
                Text { text: "视觉巡检影像地图"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    text: App.visualSurvey.keyframeCount + " 张关键帧 · "
                          + App.visualSurvey.goodKeyframeCount + " 张清晰 · "
                          + App.visualSurvey.warningKeyframeCount + " 张需复核 · 自动跳过 "
                          + App.visualSurvey.rejectedCount + " 张 · 采集覆盖 "
                          + Number(App.visualSurvey.spatialCoveragePercent).toFixed(0) + "%"
                    color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; elide: Text.ElideRight
                }
            }
            CompactIconButton { iconSource: "qrc:/icons/x.svg"; accessibleLabel: "关闭影像地图"; toolTipText: "关闭"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
        VisualSurveyMap {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 14
            compact: false
            onReplayRequested: elapsedMs => {
                root.close()
                root.replayRequested(root.sessionId, elapsedMs)
            }
        }
    }
}
