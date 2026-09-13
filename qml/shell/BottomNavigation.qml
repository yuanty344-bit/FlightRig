pragma ComponentBehavior: Bound
import QtQuick
import "../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

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
        color: Theme.heroLine
        opacity: 0.72
    }

    SurfaceCard {
        id: dock
        objectName: "navigationDock"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(420, parent.width - 32)
        height: 52
        radius: Theme.radiusXLarge
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.heroEnd }
            GradientStop { position: 1; color: Theme.heroStart }
        }
        border.color: Theme.heroLine
        elevated: true

        readonly property real cellWidth: (width - 16) / 3

        Rectangle {
            id: selectionPill
            objectName: "navigationSelectionPill"
            x: 4 + root.currentIndex * (dock.cellWidth + 4)
            y: 4
            width: dock.cellWidth
            height: dock.height - 8
            radius: Theme.radiusLarge
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0; color: Qt.lighter(Theme.accent, 1.08) }
                GradientStop { position: 1; color: Theme.accent }
            }
            border.width: 1
            border.color: Theme.accentHover

            Behavior on x {
                NumberAnimation { duration: Theme.motionSlow; easing.type: Easing.OutCubic }
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 4

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
                    radius: Theme.radiusLarge
                    color: nav.hovered && !nav.selected ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
                    border.width: nav.visualFocus ? 1 : 0
                    border.color: Theme.accent
                    Behavior on color { ColorAnimation { duration: Theme.motionFast } }
                }

                contentItem: RowLayout {
                    spacing: 7
                    scale: nav.down ? 0.97 : 1
                    Behavior on scale {
                        NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutCubic }
                    }
                    Item { Layout.fillWidth: true }
                    ThemedIcon {
                        Layout.preferredWidth: 17
                        Layout.preferredHeight: 17
                        source: nav.iconSource
                        iconColor: nav.selected ? Theme.accentContent : Theme.heroIcon
                        opacity: nav.selected ? 1 : 0.82
                    }
                    Text {
                        text: nav.label
                        color: nav.selected ? Theme.accentContent : Theme.heroMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        font.weight: nav.selected ? Font.DemiBold : Font.Medium
                        Behavior on color { ColorAnimation { duration: Theme.motionFast } }
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            NavButton { objectName: "preparationNavigation"; pageIndex: 0; label: "准备"; iconSource: "qrc:/icons/clipboard-check.svg" }
            NavButton { objectName: "flightNavigation"; pageIndex: 1; label: "飞行"; iconSource: "qrc:/icons/plane.svg" }
            NavButton { objectName: "recordsNavigation"; pageIndex: 2; label: "记录"; iconSource: "qrc:/icons/scroll-text.svg" }
        }
    }
}
