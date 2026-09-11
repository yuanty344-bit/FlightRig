import QtQuick
import QtQuick.Layouts
import FlightBench

Item {
    id: valueLine
    property int controlColumnWidth: 190
    required property string label
    required property string value
    property bool multiline: false
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.preferredHeight: multiline
                            ? Math.max(34, valueText.implicitHeight + 8)
                            : 34
    Text {
        anchors.left: parent.left
        anchors.right: valueSlot.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: valueLine.label
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSmall
        elide: Text.ElideRight
    }
    Item {
        id: valueSlot
        width: valueLine.multiline
               ? Math.max(valueLine.controlColumnWidth, parent.width - 112)
               : valueLine.controlColumnWidth
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        Text {
            id: valueText
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignRight
            text: valueLine.value
            color: Theme.textPrimary
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.textSmall
            wrapMode: valueLine.multiline ? Text.WrapAnywhere : Text.NoWrap
            maximumLineCount: valueLine.multiline ? 2 : 1
            elide: valueLine.multiline ? Text.ElideRight : Text.ElideMiddle
        }
    }
}
