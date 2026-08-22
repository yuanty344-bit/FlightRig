pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench
import "../components"

Rectangle {
    id: root
    property int currentIndex: 0
    signal navigate(int index)

    color: Theme.canvasSoft
    implicitHeight: 68

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.line
    }

    RowLayout {
        anchors.centerIn: parent
        width: Math.min(430, parent.width - 40)
        height: parent.height
        spacing: 18

        component NavButton: Button {
            id: nav
            required property int pageIndex
            required property string label
            required property url iconSource
            readonly property bool selected: root.currentIndex === pageIndex

            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.name: label
            Accessible.role: Accessible.Button
            onClicked: root.navigate(pageIndex)

            background: Rectangle {
                radius: Theme.radiusMedium
                color: nav.hovered ? Theme.surfaceRaised : "transparent"
                border.width: nav.visualFocus ? 1 : 0
                border.color: Theme.accent
            }

            contentItem: ColumnLayout {
                spacing: 3
                Item { Layout.fillHeight: true }
                ThemedIcon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 17
                    Layout.preferredHeight: 17
                    source: nav.iconSource
                    iconColor: nav.selected ? Theme.accent : Theme.textMuted
                    opacity: nav.selected ? 1 : 0.82
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: nav.label
                    color: nav.selected ? Theme.accent : Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    font.weight: nav.selected ? Font.Medium : Font.Normal
                }
                Item { Layout.fillHeight: true }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: nav.selected ? 64 : 0
                height: 2
                color: Theme.accent
                Behavior on width { NumberAnimation { duration: Theme.animationDuration(130); easing.type: Easing.OutCubic } }
            }
        }

        NavButton { pageIndex: 0; label: "准备"; iconSource: "qrc:/icons/clipboard-check.svg" }
        NavButton { pageIndex: 1; label: "飞行"; iconSource: "qrc:/icons/plane.svg" }
        NavButton { pageIndex: 2; label: "记录"; iconSource: "qrc:/icons/scroll-text.svg" }
    }
}
