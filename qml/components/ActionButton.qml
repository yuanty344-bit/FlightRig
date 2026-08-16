import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Button {
    id: root
    property url iconSource: ""
    property string kind: "primary"
    property bool compact: false
    property string toolTipText: ""
    readonly property color fillColor: kind === "danger" ? Theme.dangerSoft : kind === "secondary" ? Theme.surfaceRaised : Theme.accent
    readonly property color foregroundColor: kind === "danger" ? Theme.danger : kind === "secondary" ? Theme.textPrimary : Theme.accentContent

    implicitWidth: 120
    implicitHeight: compact ? 32 : 36
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    Accessible.role: Accessible.Button

    background: Rectangle {
        radius: Theme.radiusMedium
        color: !root.enabled ? Theme.surfaceRaised : root.down ? Qt.darker(root.fillColor, 1.15) : root.hovered ? Qt.lighter(root.fillColor, 1.08) : root.fillColor
        border.width: 1
        border.color: !root.enabled ? Theme.lineStrong : root.activeFocus ? Theme.accentHover : root.kind === "danger" ? "#6B3A3B" : root.kind === "secondary" ? Theme.lineStrong : Theme.accent
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    contentItem: RowLayout {
        spacing: Theme.space8
        Item { Layout.fillWidth: true }
        ThemedIcon {
            visible: root.iconSource.toString().length > 0
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            source: root.iconSource
            iconColor: root.enabled ? root.foregroundColor : Theme.textMuted
            opacity: root.enabled ? 0.95 : 0.45
        }
        Text {
            text: root.text
            color: root.enabled ? root.foregroundColor : Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
            font.weight: Font.DemiBold
        }
        Item { Layout.fillWidth: true }
    }

    HoverHandler { id: toolTipHover }
    AppToolTip {
        visible: toolTipHover.hovered && root.toolTipText.length > 0
        text: root.toolTipText
    }
}
