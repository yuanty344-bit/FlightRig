import QtQuick
import FlightBench

Rectangle {
    id: control
    property bool checked: false
    signal toggled()
    implicitWidth: 38
    implicitHeight: 22
    radius: 11
    color: checked ? Theme.accent : Theme.lineStrong
    Rectangle {
        width: 16; height: 16; radius: 8
        y: 3
        x: control.checked ? control.width - width - 3 : 3
        color: "white"
        Behavior on x { NumberAnimation { duration: Theme.animationDuration(100); easing.type: Easing.OutCubic } }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: control.toggled() }
}
