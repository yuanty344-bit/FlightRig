import QtQuick
import QtQuick.Effects
import FlightBench

Rectangle {
    radius: Theme.radiusXLarge
    color: Theme.surface
    border.width: 1
    border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                          Theme.darkMode ? 0.42 : 0.25)

    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: Theme.shadow
        shadowBlur: 0.82
        shadowVerticalOffset: 8
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 22
        anchors.top: parent.top
        anchors.topMargin: Theme.edgeInset
        width: 68
        height: 3
        radius: 2
        color: Theme.accent
    }

}
