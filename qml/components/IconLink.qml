import QtQuick
import QtQuick.Controls.Basic
import FlightBench

Button {
    id: root
    property url iconSource: ""
    property color foregroundColor: Theme.accentHover
    property int sidePadding: 14

    implicitWidth: linkRow.implicitWidth + sidePadding * 2
    implicitHeight: 34
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    Accessible.role: Accessible.Button

    background: Rectangle {
        radius: Theme.radiusMedium
        color: root.down ? Theme.surfaceInteractive
                         : root.hovered ? Theme.surfaceRaised : "transparent"
        border.width: root.activeFocus ? 1 : 0
        border.color: Theme.accent
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    contentItem: Item {
        implicitWidth: linkRow.implicitWidth
        implicitHeight: linkRow.implicitHeight
        Row {
            id: linkRow
            anchors.centerIn: parent
            spacing: 7
            ThemedIcon {
                visible: root.iconSource.toString().length > 0
                width: 16
                height: 16
                anchors.verticalCenter: parent.verticalCenter
                source: root.iconSource
                iconColor: root.foregroundColor
                opacity: 0.90
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                color: root.foregroundColor
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                font.weight: Font.Medium
            }
        }
    }
}
