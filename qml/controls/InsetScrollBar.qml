import QtQuick
import QtQuick.Controls.Basic
import FlightBench

ScrollBar {
    id: root

    // Qt's AsNeeded policy can leave a full-length indicator visible for
    // attached views whose content exactly fits. Keep scrolling enabled on the
    // view, but only paint the indicator when there is something to scroll.
    readonly property bool contentOverflows: size > 0 && size < 0.999

    visible: policy === ScrollBar.AlwaysOn
             || (policy === ScrollBar.AsNeeded && contentOverflows)

    implicitWidth: orientation === Qt.Vertical ? Theme.scrollBarExtent : 0
    implicitHeight: orientation === Qt.Horizontal ? Theme.scrollBarExtent : 0
    leftPadding: orientation === Qt.Vertical ? Theme.scrollBarEdgeGap : 2
    rightPadding: orientation === Qt.Vertical ? Theme.scrollBarEdgeGap : 2
    topPadding: orientation === Qt.Horizontal ? Theme.scrollBarEdgeGap : 2
    bottomPadding: orientation === Qt.Horizontal ? Theme.scrollBarEdgeGap : 2

    contentItem: Rectangle {
        implicitWidth: 4
        implicitHeight: 4
        radius: 2
        color: root.pressed ? Theme.accent : Theme.textMuted
        opacity: root.active || root.pressed ? 0.78 : 0.38
        Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
        Behavior on color { ColorAnimation { duration: Theme.motionFast } }
    }

    background: Item { }
}
