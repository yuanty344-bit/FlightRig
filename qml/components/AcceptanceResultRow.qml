import QtQuick
import FlightBench

Item {
    id: root
    required property var result
    // The parent assigns width. Constant intrinsic width breaks the former
    // child preferredWidth -> RowLayout implicitWidth -> width cycle.
    implicitWidth: 400
    implicitHeight: Math.max(44, detailText.y + detailText.implicitHeight + 8,
                             labelText.implicitHeight + 16, statusText.implicitHeight + 16)
    readonly property bool stacked: width < 420
    readonly property string resultState: String(result.status || "warning")
    readonly property color stateColor: resultState === "pass" ? Theme.success
                                         : resultState === "fail" ? Theme.danger : Theme.warning
    Rectangle {
        x: 0
        y: 8 + Math.max(0, (labelText.implicitHeight - height) / 2)
        width: 7
        height: 7
        radius: 4
        color: root.stateColor
    }
    Text {
        id: labelText
        x: 17
        y: 8
        width: Math.max(1, root.stacked ? statusText.x - x - 10 : 120)
        text: String(root.result.label || "检查项")
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSmall
        wrapMode: Text.Wrap
    }
    Text {
        id: detailText
        x: root.stacked ? 17 : labelText.x + labelText.width + 10
        y: root.stacked ? labelText.y + Math.max(labelText.implicitHeight, statusText.implicitHeight) + 6 : 8
        width: Math.max(1, (root.stacked ? root.width : statusText.x - 10) - x)
        text: String(root.result.detail || "")
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textTiny
        wrapMode: Text.Wrap
    }
    Text {
        id: statusText
        anchors.right: parent.right
        y: 8
        text: root.resultState === "pass" ? "通过"
              : root.resultState === "fail" ? "未通过" : "提醒"
        color: root.stateColor
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textTiny
        font.weight: Font.DemiBold
    }
}
