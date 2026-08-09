import QtQuick
import QtQuick.Controls.Basic
import FlightBench

ToolTip {
    id: root
    delay: 450
    timeout: 3500
    padding: 0
    implicitWidth: tipLabel.implicitWidth + 16
    implicitHeight: 26

    contentItem: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.lineStrong

        Text {
            id: tipLabel
            anchors.centerIn: parent
            text: root.text
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
        }
    }

    background: null
}
