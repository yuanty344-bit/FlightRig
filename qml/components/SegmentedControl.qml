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

    RowLayout {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 2

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
                    color: root.currentIndex === optionButton.index
                           ? Theme.surface
                           : optionButton.hovered ? Theme.surfaceInteractive : "transparent"
                    border.width: optionButton.activeFocus ? 1 : 0
                    border.color: Theme.accent
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
                }
            }
        }
    }
}
