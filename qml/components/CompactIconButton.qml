import QtQuick
import QtQuick.Controls.Basic
import FlightBench

Button {
    id: root
    property url iconSource: ""
    property string accessibleLabel: "操作"
    property string toolTipText: ""
    property bool outlined: false
    property bool danger: false
    property color normalIconColor: danger ? Theme.danger : Theme.textSecondary

    implicitWidth: 32
    implicitHeight: 32
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: accessibleLabel
    Accessible.role: Accessible.Button

    background: Rectangle {
        radius: Theme.radiusSmall
        color: !root.enabled ? "transparent"
                             : root.down ? Theme.surfaceInteractive
                             : root.hovered ? (root.danger ? Theme.dangerSoft : Theme.surfaceInteractive)
                                            : "transparent"
        border.width: root.visualFocus || root.outlined ? 1 : 0
        border.color: root.visualFocus ? Theme.accent : Theme.line
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    contentItem: Item {
        ThemedIcon {
            anchors.centerIn: parent
            width: 16
            height: 16
            source: root.iconSource
            iconColor: root.enabled ? root.normalIconColor : Theme.textFaint
            opacity: root.enabled ? 0.92 : 0.42
        }
    }

    HoverHandler { id: toolTipHover }
    AppToolTip {
        visible: toolTipHover.hovered && root.toolTipText.length > 0
        text: root.toolTipText
    }
}
