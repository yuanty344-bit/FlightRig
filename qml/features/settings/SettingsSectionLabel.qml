import QtQuick
import QtQuick.Layouts
import FlightBench

Item {
    required property string text
    Layout.fillWidth: true
    Layout.preferredHeight: 34
    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 7
        text: parent.text
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textTiny
        font.weight: Font.Medium
    }
}
