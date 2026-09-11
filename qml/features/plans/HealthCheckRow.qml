import QtQuick
import QtQuick.Layouts
import FlightBench

Item {
    id: root
    property string label: "检查项"
    property string description: ""
    property string detail: complete ? "已通过" : "等待"
    property bool complete: false
    property bool dividerVisible: true
    property bool showDetail: true

    implicitHeight: 56
    Layout.maximumHeight: implicitHeight

    RowLayout {
        anchors.fill: parent
        spacing: Theme.space12
        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: root.complete ? Theme.success : Theme.textFaint
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text { text: root.label; color: root.complete ? Theme.textSecondary : Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: root.complete ? Font.Normal : Font.Medium }
            Text { visible: root.description.length > 0; Layout.fillWidth: true; text: root.description; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideRight }
        }
        Text { visible: root.showDetail; text: root.detail; color: root.complete ? Theme.success : Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; font.weight: Font.Medium }
    }
    Rectangle { visible: root.dividerVisible; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
}
