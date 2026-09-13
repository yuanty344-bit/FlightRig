import QtQuick
import QtQuick.Effects
import FlightBench

Rectangle {
    id: root
    property bool elevated: false
    property bool interactive: false
    readonly property bool hovered: hoverHandler.hovered

    radius: Theme.radiusLarge
    color: Theme.surface
    border.width: 1
    border.color: root.interactive && root.hovered ? Theme.lineStrong : Theme.line

    layer.enabled: root.elevated
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: Theme.shadow
        shadowBlur: 0.62
        shadowVerticalOffset: 3
    }

    Behavior on border.color {
        ColorAnimation { duration: Theme.motionFast }
    }

    HoverHandler { id: hoverHandler; enabled: root.interactive }
}
