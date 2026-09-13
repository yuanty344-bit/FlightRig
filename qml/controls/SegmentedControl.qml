pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Rectangle {
    id: root
    property var options: []
    property int currentIndex: 0
    signal selected(int index)

    implicitWidth: Math.max(120, options.length * 56)
    implicitHeight: 34
    radius: Theme.radiusMedium
    color: Theme.surfaceRaised
    border.width: 1
    border.color: Theme.line
    clip: true

    Rectangle {
        id: selectionPill
        objectName: "segmentedSelectionPill"
        visible: root.options.length > 0
        x: 3 + Math.min(Math.max(0, root.currentIndex), root.options.length - 1) * width
        y: 3
        width: root.options.length > 0 ? (root.width - 6) / root.options.length : 0
        height: root.height - 6
        radius: Theme.radiusSmall
        color: Theme.surface
        border.width: 1
        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                              Theme.darkMode ? 0.28 : 0.16)

        Behavior on x {
            NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic }
        }
        Behavior on width {
            NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutCubic }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 0

        Repeater {
            model: root.options
            delegate: Button {
                id: optionButton
                required property int index
                required property string modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 0
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: modelData
                Accessible.role: Accessible.Button
                onClicked: root.selected(index)
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: optionButton.hovered && root.currentIndex !== optionButton.index
                           ? Theme.surfaceInteractive : "transparent"
                    border.width: optionButton.visualFocus ? 1 : 0
                    border.color: Theme.accent
                    Behavior on color { ColorAnimation { duration: Theme.motionFast } }
                }
                contentItem: Text {
                    text: optionButton.modelData
                    color: root.currentIndex === optionButton.index
                           ? Theme.textPrimary : Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    font.weight: root.currentIndex === optionButton.index ? Font.Medium : Font.Normal
                    Behavior on color { ColorAnimation { duration: Theme.motionFast } }
                }
            }
        }
    }
}
