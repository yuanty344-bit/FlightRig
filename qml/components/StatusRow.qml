import QtQuick
import QtQuick.Layouts
import FlightBench

Item {
    id: root
    property string label: "状态"
    property string value: "—"
    property bool complete: false
    property string state: complete ? "complete" : "pending"
    property bool dividerVisible: true
    property url iconSource: "qrc:/icons/circle-check.svg"
    property color stateColor: state === "complete" ? Theme.success
                               : state === "current" ? Theme.accent
                               : state === "problem" ? Theme.warning
                               : Theme.textFaint
    implicitHeight: 54

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 12

        ThemedIcon {
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
            source: root.iconSource
            iconColor: root.stateColor
        }
        Text {
            Layout.fillWidth: true
            text: root.label
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
        }
        Text {
            text: root.value
            color: root.state === "problem" ? Theme.warning
                   : root.state === "current" ? Theme.accent
                   : root.state === "complete" ? Theme.textMuted
                   : Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
        }
    }

    Rectangle {
        visible: root.dividerVisible
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.line
    }
}
