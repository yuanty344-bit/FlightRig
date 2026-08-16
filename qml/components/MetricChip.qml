import QtQuick
import QtQuick.Layouts
import FlightBench

Rectangle {
    id: root
    property string label: "参数"
    property string value: "—"
    property color valueColor: Theme.textPrimary

    implicitWidth: content.implicitWidth + 22
    implicitHeight: 34
    radius: Theme.radiusMedium
    color: Theme.surface
    border.width: 1
    border.color: Theme.line

    RowLayout {
        id: content
        anchors.centerIn: parent
        spacing: 7
        Text {
            text: root.label
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
        }
        Text {
            text: root.value
            color: root.valueColor
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.textSmall
            font.weight: Font.DemiBold
        }
    }
}
