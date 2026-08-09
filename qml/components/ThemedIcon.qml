import QtQuick
import QtQuick.Effects
import FlightBench

Item {
    id: root
    property url source: ""
    property color iconColor: Theme.textSecondary

    Image {
        id: sourceImage
        anchors.fill: parent
        source: root.source
        sourceSize: Qt.size(width, height)
        smooth: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: sourceImage
        colorization: 1.0
        colorizationColor: root.iconColor
    }
}
