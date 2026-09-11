import QtQuick
import QtQuick.Shapes
import FlightBench

Item {
    id: marker
    required property int index
    required property real eastMeters
    required property real northMeters
    readonly property real tipY: 41
    property bool buildingRisk: false
    property bool selected: false
    property bool editingLocked: false
    required property Item dragSurface
    signal selectedRequested()
    signal dragged(real startEast, real startNorth, real deltaX, real deltaY)
    signal dragFinished()
    width: 36
    height: 45
    z: marker.selected ? 6 : 5

    Shape {
        x: 1
        y: 3
        width: 34
        height: 42
        opacity: marker.selected ? 0.28 : 0.18
        ShapePath {
            strokeWidth: 0
            fillColor: "#000000"
            startX: 17; startY: 2
            PathCubic { x: 3; y: 15; control1X: 8; control1Y: 2; control2X: 3; control2Y: 8 }
            PathCubic { x: 17; y: 40; control1X: 3; control1Y: 25; control2X: 12; control2Y: 34 }
            PathCubic { x: 31; y: 15; control1X: 22; control1Y: 34; control2X: 31; control2Y: 25 }
            PathCubic { x: 17; y: 2; control1X: 31; control1Y: 8; control2X: 26; control2Y: 2 }
        }
    }

    Shape {
        x: 1
        y: 0
        width: 34
        height: 42
        ShapePath {
            strokeWidth: marker.selected ? 2 : 1.5
            strokeColor: marker.buildingRisk ? Theme.danger
                         : marker.selected
                         ? Theme.accentHover : Theme.lineStrong
            fillColor: marker.buildingRisk ? Theme.dangerSoft
                       : marker.selected
                       ? Theme.accent : Theme.surface
            startX: 17; startY: 2
            PathCubic { x: 3; y: 15; control1X: 8; control1Y: 2; control2X: 3; control2Y: 8 }
            PathCubic { x: 17; y: 40; control1X: 3; control1Y: 25; control2X: 12; control2Y: 34 }
            PathCubic { x: 31; y: 15; control1X: 22; control1Y: 34; control2X: 31; control2Y: 25 }
            PathCubic { x: 17; y: 2; control1X: 31; control1Y: 8; control2X: 26; control2Y: 2 }
        }
    }

    Rectangle {
        x: 7
        y: 6
        width: 22
        height: 18
        radius: 9
        color: marker.selected
               ? Qt.lighter(marker.buildingRisk ? Theme.danger
                                               : Theme.accent, 1.12)
               : marker.buildingRisk ? Theme.dangerSoft
                                     : Theme.surfaceRaised
        border.width: 1
        border.color: marker.selected
                      ? Qt.rgba(1, 1, 1, 0.34)
                      : marker.buildingRisk ? Theme.danger : Theme.line
        Rectangle {
            x: 4; y: 3; width: 10; height: 4; radius: 2
            color: marker.selected
                   ? Qt.rgba(1, 1, 1, 0.24) : Qt.rgba(1, 1, 1, 0.55)
        }
        Text {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 1
            text: marker.index + 1
            color: marker.selected
                   ? Theme.accentContent : Theme.textPrimary
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.textTiny
            font.weight: Font.DemiBold
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: !marker.editingLocked
        hoverEnabled: true
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        property real startMapX: 0
        property real startMapY: 0
        property real startEast: 0
        property real startNorth: 0
        onPressed: mouse => {
            marker.selectedRequested()
            var mapPoint = marker.mapToItem(marker.dragSurface, mouse.x, mouse.y)
            startMapX = mapPoint.x
            startMapY = mapPoint.y
            startEast = marker.eastMeters
            startNorth = marker.northMeters
            mouse.accepted = true
        }
        onPositionChanged: mouse => {
            if (!pressed)
                return
            var mapPoint = marker.mapToItem(marker.dragSurface, mouse.x, mouse.y)
            marker.dragged(startEast, startNorth, mapPoint.x - startMapX, mapPoint.y - startMapY)
        }
        onReleased: marker.dragFinished()
        onClicked: mouse => mouse.accepted = true
    }
}
