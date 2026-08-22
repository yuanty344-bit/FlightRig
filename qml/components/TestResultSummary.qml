pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Dialog {
    id: root
    property var session: ({})
    signal recordsRequested()
    signal replayRequested(string sessionId)
    anchors.centerIn: parent
    width: Math.min(450, parent ? parent.width - 40 : 450)
    modal: true
    padding: 22
    closePolicy: Popup.CloseOnEscape
    readonly property string testOutcome: String(session.testResult || "not-rated")
    readonly property color resultColor: testOutcome === "passed" ? Theme.success
                                          : testOutcome === "warning" ? Theme.warning
                                          : testOutcome === "failed" || testOutcome === "aborted" ? Theme.danger
                                                                                                    : Theme.textMuted
    background: Rectangle { radius: Theme.radiusLarge; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.lineStrong }
    contentItem: ColumnLayout {
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            ThemedIcon {
                Layout.preferredWidth: 24; Layout.preferredHeight: 24
                source: root.testOutcome === "passed" ? "qrc:/icons/shield-check.svg" : "qrc:/icons/triangle-alert.svg"
                iconColor: root.resultColor
            }
            Text { Layout.fillWidth: true; text: "测试" + String(root.session.testResultLabel || "已结束"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            CompactIconButton { iconSource: "qrc:/icons/x.svg"; accessibleLabel: "关闭测试结果"; toolTipText: "关闭"; onClicked: root.close() }
        }
        Text { Layout.fillWidth: true; text: String(root.session.testResultReason || "本次会话已经保存"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textBody; wrapMode: Text.WordWrap }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 72; radius: Theme.radiusMedium; color: Theme.surface
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 18
                ColumnLayout { Layout.fillWidth: true; spacing: 3; Text { text: "持续时间"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny } Text { text: String(root.session.durationLabel || "00:00"); color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSubtitle } }
                ColumnLayout { Layout.fillWidth: true; spacing: 3; Text { text: "最大偏差"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny } Text { text: Number(root.session.missionMaxCrossTrackError || 0).toFixed(1) + " m"; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSubtitle } }
                ColumnLayout { Layout.fillWidth: true; spacing: 3; Text { text: "验收项目"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny } Text { text: Number(root.session.acceptancePassedCount || 0) + " / " + Number((root.session.acceptanceChecks || []).length); color: root.resultColor; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSubtitle } }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { Layout.fillWidth: true; text: "查看记录"; kind: "secondary"; onClicked: { root.close(); root.recordsRequested() } }
            ActionButton { Layout.fillWidth: true; text: "回放"; kind: "secondary"; enabled: Boolean(root.session.replayAvailable); disabledReason: "该会话缺少位置数据"; onClicked: { root.close(); root.replayRequested(String(root.session.id || "")) } }
            ActionButton { Layout.fillWidth: true; text: "打开报告"; iconSource: "qrc:/icons/file-chart-column.svg"; onClicked: App.generateSessionReport(String(root.session.id || "")) }
        }
    }
}
