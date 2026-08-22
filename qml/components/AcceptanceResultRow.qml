import QtQuick
import QtQuick.Layouts
import FlightBench

RowLayout {
    id: root
    required property var result
    spacing: 10
    Layout.preferredHeight: Math.max(28, detailText.implicitHeight)
    readonly property string state: String(result.status || "warning")
    readonly property color stateColor: state === "pass" ? Theme.success
                                         : state === "fail" ? Theme.danger : Theme.warning
    Rectangle { Layout.preferredWidth: 7; Layout.preferredHeight: 7; radius: 4; color: root.stateColor }
    Text {
        Layout.preferredWidth: Math.min(130, Math.max(92, root.width * 0.27))
        text: String(root.result.label || "检查项")
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSmall
        elide: Text.ElideRight
    }
    Text {
        id: detailText
        Layout.fillWidth: true
        text: String(root.result.detail || "")
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textTiny
        wrapMode: Text.WordWrap
        maximumLineCount: 3
        elide: Text.ElideRight
    }
    Text {
        text: root.state === "pass" ? "通过" : root.state === "fail" ? "未通过" : "提醒"
        color: root.stateColor
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textTiny
        font.weight: Font.DemiBold
    }
}
