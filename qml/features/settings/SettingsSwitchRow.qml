import QtQuick
import QtQuick.Layouts
import FlightBench

Item {
    id: switchRow
    required property string title
    property string summary: ""
    property bool checked: false
    signal toggled()
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.preferredHeight: summary.length > 0
                            ? Math.max(48, switchTextColumn.implicitHeight + 10)
                            : 44
    Column {
        id: switchTextColumn
        anchors.left: parent.left
        anchors.right: switchSlot.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text { width: parent.width; text: switchRow.title; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium; elide: Text.ElideRight }
        Text {
            visible: switchRow.summary.length > 0
            width: parent.width
            text: switchRow.summary
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }
    Item {
        id: switchSlot
        width: 38
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        SettingsToggle {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            checked: switchRow.checked
            onToggled: switchRow.toggled()
        }
    }
}
