import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Button {
    id: root
    property string label: "详情"
    property bool expanded: false
    property bool topDivider: false
    signal activated()

    implicitHeight: topDivider ? 48 : 42
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: (expanded ? "收起" : "展开") + label
    Accessible.role: Accessible.Button
    onClicked: activated()

    background: Item {
        Rectangle {
            visible: root.topDivider
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.line
        }
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: root.topDivider ? 1 : 0
            radius: Theme.radiusSmall
            color: root.down ? Theme.surfaceInteractive
                             : root.hovered ? Theme.surfaceRaised : "transparent"
        }
        Rectangle {
            visible: root.visualFocus
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: "transparent"
            border.width: 1
            border.color: Theme.accent
        }
    }

    contentItem: RowLayout {
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: root.label
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
        }
        ThemedIcon {
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
            source: "qrc:/icons/chevron-down.svg"
            iconColor: root.hovered ? Theme.textPrimary : Theme.textSecondary
            rotation: root.expanded ? 180 : 0
            Behavior on rotation { NumberAnimation { duration: 120 } }
        }
    }
}
