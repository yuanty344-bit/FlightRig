import QtQuick
import QtQuick.Layouts
import FlightBench
import "../../controls"

Rectangle {
    id: category
    required property string title
    required property string summary
    required property url iconSource
    property int targetPage: 0
    signal activated()
    Layout.fillWidth: true
    Layout.preferredHeight: 62
    radius: Theme.radiusMedium
    color: categoryMouse.containsMouse ? Theme.surfaceInteractive : Theme.surfaceRaised
    border.width: 1
    border.color: categoryMouse.containsMouse ? Theme.lineStrong : Theme.line
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        spacing: 11
        Rectangle {
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            radius: Theme.radiusMedium
            color: Theme.accentSoft
            ThemedIcon {
                anchors.centerIn: parent
                width: 17; height: 17
                source: category.iconSource
                iconColor: Theme.accent
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text { text: category.title; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
            Text { Layout.fillWidth: true; text: category.summary; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideRight }
        }
        ThemedIcon { Layout.preferredWidth: 14; Layout.preferredHeight: 14; source: "qrc:/icons/chevron-down.svg"; rotation: -90; iconColor: Theme.textFaint }
    }
    MouseArea {
        id: categoryMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { category.activated() }
    }
}
