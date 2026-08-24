import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Button {
    id: root
    property url iconSource: ""
    property string kind: "primary"
    property bool compact: false
    property string accessibleLabel: ""
    property string toolTipText: ""
    property string disabledReason: ""
    // Fixed action labels must stay readable.  Only values whose text is
    // genuinely dynamic (for example, a long world name) opt into eliding.
    property bool allowTextElide: false
    readonly property string effectiveToolTip: root.enabled
                                                ? root.toolTipText
                                                : (root.disabledReason.length > 0
                                                   ? root.disabledReason
                                                   : (root.toolTipText.length > 0
                                                      ? root.toolTipText
                                                      : "当前状态下不可用"))
    readonly property color fillColor: kind === "danger" ? Theme.dangerSoft : kind === "secondary" ? Theme.surfaceRaised : Theme.accent
    readonly property color foregroundColor: kind === "danger" ? Theme.danger : kind === "secondary" ? Theme.textPrimary : Theme.accentContent
    readonly property real calculatedContentWidth: buttonLabel.implicitWidth
                                                   + (iconSource.toString().length > 0 ? 24 : 0)
                                                   + 32

    implicitWidth: Math.max(120, calculatedContentWidth)
    implicitHeight: Math.max(compact ? 32 : 36, buttonLabel.implicitHeight + 14)
    Layout.minimumWidth: root.text.length === 0
                         ? (root.compact ? 38 : 44)
                         : (root.allowTextElide
                            ? 80
                            : root.calculatedContentWidth)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: accessibleLabel.length > 0 ? accessibleLabel : text
    Accessible.description: effectiveToolTip
    Accessible.role: Accessible.Button

    background: Rectangle {
        radius: Theme.radiusMedium
        color: !root.enabled ? Theme.surfaceRaised : root.down ? Qt.darker(root.fillColor, 1.15) : root.hovered ? Qt.lighter(root.fillColor, 1.08) : root.fillColor
        border.width: 1
        border.color: !root.enabled ? Theme.lineStrong : root.visualFocus ? Theme.accentHover : root.kind === "danger" ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.45) : root.kind === "secondary" ? Theme.lineStrong : Theme.accent
        Behavior on color { ColorAnimation { duration: Theme.animationDuration(100) } }
    }

    contentItem: RowLayout {
        clip: true
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
            id: buttonLabel
            Layout.maximumWidth: Math.max(0, root.availableWidth
                                             - (root.iconSource.toString().length > 0 ? 24 : 0)
                                             - 24)
            text: root.text
            color: root.enabled ? root.foregroundColor : Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
            font.weight: Font.DemiBold
            elide: root.allowTextElide ? Text.ElideRight : Text.ElideNone
        }
        Item { Layout.fillWidth: true }
    }

    HoverHandler { id: toolTipHover }
    AppToolTip {
        visible: toolTipHover.hovered && root.effectiveToolTip.length > 0
        text: root.effectiveToolTip
    }
}
