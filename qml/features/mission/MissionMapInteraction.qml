import QtQuick

MouseArea {
    id: root
    property bool editingLocked: false
    property real previousX: 0
    property real previousY: 0
    property real pressX: 0
    property real pressY: 0
    property bool moved: false
    signal focusRequested()
    signal panRequested(real deltaX, real deltaY)
    signal pointRequested(real pixelX, real pixelY)
    signal zoomRequested(real pixelX, real pixelY, real factor)
    hoverEnabled: true
    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
    onPressed: mouse => {
        previousX = mouse.x
        previousY = mouse.y
        pressX = mouse.x
        pressY = mouse.y
        moved = false
        focusRequested()
    }
    onPositionChanged: mouse => {
        if (!pressed) return
        const deltaX = mouse.x - previousX
        const deltaY = mouse.y - previousY
        if (Math.hypot(mouse.x - pressX, mouse.y - pressY) >= 4)
            moved = true
        if (moved) panRequested(deltaX, deltaY)
        previousX = mouse.x
        previousY = mouse.y
    }
    onReleased: mouse => {
        if (!moved && mouse.button === Qt.LeftButton && !editingLocked)
            pointRequested(mouse.x, mouse.y)
    }
    onWheel: wheel => {
        zoomRequested(wheel.x, wheel.y, Math.pow(1.0015, wheel.angleDelta.y))
        wheel.accepted = true
    }
}
