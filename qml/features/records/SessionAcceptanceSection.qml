pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import FlightBench
import "../plans"

ColumnLayout {
    id: root
    property var session: ({})
    visible: Boolean(root.session.acceptanceChecks
                     && root.session.acceptanceChecks.length > 0)
    Layout.fillWidth: true
    Layout.topMargin: visible ? 12 : 0
    spacing: 8
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: "验收明细"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
        Text {
            text: Number(root.session.acceptancePassedCount || 0) + " 通过 · "
                  + Number(root.session.acceptanceWarningCount || 0) + " 提醒 · "
                  + Number(root.session.acceptanceFailedCount || 0) + " 未通过"
            color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny
        }
    }
    Repeater {
        model: root.session.acceptanceChecks || []
        delegate: AcceptanceResultRow { required property var modelData; Layout.fillWidth: true; result: modelData }
    }
}
