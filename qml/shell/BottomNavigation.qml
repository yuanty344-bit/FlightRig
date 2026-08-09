pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import FlightBench
import "../components"

Rectangle {
    id: root
    property int currentIndex: 0
    signal navigate(int index)
    color: Theme.canvasSoft
    implicitHeight: 52

    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: Theme.line }

    RowLayout {
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 36)
        height: parent.height
        spacing: 8

        component NavButton: Rectangle {
            id: nav
            property int index: 0
            property string label: "页面"
            property url iconSource: ""
            readonly property bool selected: root.currentIndex === index
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: navMouse.containsMouse ? Theme.surfaceRaised : "transparent"
            border.width: activeFocus ? 1 : 0
            border.color: Theme.accent
            activeFocusOnTab: true
            Accessible.name: label
            Accessible.role: Accessible.Button
            Keys.onReturnPressed: root.navigate(nav.index)
            Keys.onEnterPressed: root.navigate(nav.index)
            Keys.onSpacePressed: root.navigate(nav.index)

            Row {
                anchors.centerIn: parent
                spacing: 8
                ThemedIcon { anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; source: nav.iconSource; iconColor: nav.selected ? Theme.textPrimary : Theme.textMuted; opacity: nav.selected ? 1.0 : 0.74 }
                Text { anchors.verticalCenter: parent.verticalCenter; text: nav.label; color: nav.selected ? Theme.textPrimary : Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: nav.selected ? Font.Medium : Font.Normal }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: nav.selected ? 56 : 0
                height: 2
                color: Theme.accent
                Behavior on width { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
            }

            MouseArea { id: navMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.navigate(nav.index) }
        }

        NavButton { index: 0; label: "工作台"; iconSource: "qrc:/icons/house.svg" }
        NavButton { index: 1; label: "飞行"; iconSource: "qrc:/icons/gauge.svg" }
        NavButton { index: 2; label: "记录"; iconSource: "qrc:/icons/scroll-text.svg" }
    }
}
