import QtQuick
import QtQuick.Layouts
import FlightBench
import "../../controls"

Item {
    id: choiceRow
    property int controlColumnWidth: 190
    required property string title
    required property var choices
    property var selectedValue: ""
    signal valuePicked(var value)
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.preferredHeight: 48
    Text {
        anchors.left: parent.left
        anchors.right: choiceSlot.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: choiceRow.title
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSmall
        font.weight: Font.Medium
        elide: Text.ElideRight
    }
    Item {
        id: choiceSlot
        width: choiceRow.controlColumnWidth
        height: 38
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        SelectField {
            anchors.fill: parent
            model: choiceRow.choices
            selectedValue: choiceRow.selectedValue
            onValuePicked: value => choiceRow.valuePicked(value)
        }
    }
}
