import QtQuick
import QtQuick.Layouts
import FlightBench
import "../../controls"

RowLayout {
    id: header
    signal returnRequested()
    required property string title
    property string subtitle: ""
    Layout.fillWidth: true
    Layout.preferredHeight: subtitle.length > 0 ? 58 : 44
    spacing: 8
    CompactIconButton {
        iconSource: "qrc:/icons/chevron-down.svg"
        iconRotation: 90
        accessibleLabel: "返回设置"
        toolTipText: "返回"
        onClicked: header.returnRequested()
    }
    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: 2
        Text {
            text: title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSubtitle
            font.weight: Font.DemiBold
        }
        Text {
            visible: subtitle.length > 0
            Layout.fillWidth: true
            text: subtitle
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
            elide: Text.ElideRight
        }
    }
}
