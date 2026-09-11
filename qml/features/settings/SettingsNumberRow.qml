import QtQuick
import QtQuick.Layouts
import FlightBench
import "../../controls"

Item {
    id: numberRow
    property int controlColumnWidth: 190
    required property string title
    property string summary: ""
    property real value: 0
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property int decimals: 0
    property string unit: ""
    signal valueEdited(real value)
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.preferredHeight: summary.length > 0 ? 58 : 48
    Column {
        anchors.left: parent.left
        anchors.right: numberSlot.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text { width: parent.width; text: numberRow.title; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium; elide: Text.ElideRight }
        Text { visible: numberRow.summary.length > 0; width: parent.width; text: numberRow.summary; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideRight }
    }
    Item {
        id: numberSlot
        width: numberRow.controlColumnWidth
        height: 38
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        NumericField {
            anchors.fill: parent
            value: numberRow.value
            from: numberRow.from; to: numberRow.to; stepSize: numberRow.stepSize
            decimals: numberRow.decimals; unit: numberRow.unit
            accessibleName: numberRow.title
            onValueEdited: value => numberRow.valueEdited(value)
        }
    }
}
