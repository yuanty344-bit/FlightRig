import QtQuick
import QtQuick.Controls.Basic
import FlightBench

ToolTip {
    id: root
    property int maximumWidth: 360

    delay: 450
    timeout: 3500
    padding: 0
    implicitWidth: Math.min(maximumWidth, tipLabel.implicitWidth + 16)
    implicitHeight: Math.max(26, tipLabel.implicitHeight + 10)

    contentItem: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.lineStrong

        Text {
            id: tipLabel
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.topMargin: 5
            anchors.bottomMargin: 5
            text: root.text
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: null
}
