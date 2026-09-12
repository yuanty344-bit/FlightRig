pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

MotionDialog {
    id: root
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 40 : 560)
    height: Math.min(520, parent ? parent.height - 40 : 520)
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    property string pendingDeleteId: ""

    background: DialogSurface {
        radius: Theme.radiusLarge
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.lineStrong
    }
    contentItem: ColumnLayout {
        spacing: 12
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            ColumnLayout {
                anchors.left: parent.left
                anchors.right: closeButton.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: "飞行记录回收站"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTitle
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: App.trashRetentionDays > 0
                          ? "删除的飞行记录保留 " + App.trashRetentionDays + " 天，可在此恢复"
                          : "删除的飞行记录不会自动清理，可在此恢复"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    elide: Text.ElideRight
                }
            }
            PanelCloseButton {
                id: closeButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconSource: "qrc:/icons/x.svg"
                accessibleLabel: "关闭飞行记录回收站"
                toolTipText: "关闭"
                onClicked: root.close()
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
        ListView {
            id: trashList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: App.trashedFlightSessions
            delegate: Rectangle {
                required property var modelData
                width: trashList.width
                height: 68
                radius: Theme.radiusMedium
                color: Theme.surface
                border.width: 1
                border.color: Theme.line
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    ThemedIcon { Layout.preferredWidth: 18; Layout.preferredHeight: 18; source: "qrc:/icons/archive-restore.svg"; iconColor: Theme.textMuted }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: String(modelData.displayName || modelData.testPlanName || modelData.missionRouteName || modelData.modelName || "飞行记录")
                            color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: String(modelData.startedLabel || "") + " · " + String(modelData.worldName || "场景未记录")
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            elide: Text.ElideRight
                        }
                    }
                    CompactIconButton {
                        iconSource: "qrc:/icons/archive-restore.svg"; accessibleLabel: "恢复飞行记录"; toolTipText: "恢复到记录列表"
                        onClicked: App.restoreSession(String(modelData.id || ""))
                    }
                    CompactIconButton {
                        iconSource: "qrc:/icons/archive-x.svg"; accessibleLabel: "永久删除飞行记录"; toolTipText: "永久删除"; danger: true
                        onClicked: { root.pendingDeleteId = String(modelData.id || ""); permanentDeleteDialog.open() }
                    }
                }
            }
            Text {
                visible: trashList.count === 0
                anchors.centerIn: parent
                text: "回收站是空的"
                color: Theme.textFaint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
            }
        }
    }

    MotionDialog {
        id: permanentDeleteDialog
        anchors.centerIn: parent
        width: Math.min(390, root.width - 40)
        modal: true
        padding: 20
        background: DialogSurface { color: Theme.surfaceRaised }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "永久删除这条飞行记录？"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            Text { Layout.fillWidth: true; text: "摘要、事件和遥测数据将无法恢复。"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textBody; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { Layout.preferredWidth: 82; text: "取消"; kind: "secondary"; onClicked: permanentDeleteDialog.close() }
                ActionButton {
                    Layout.preferredWidth: 100; text: "永久删除"; kind: "danger"
                    onClicked: { if (App.permanentlyDeleteSession(root.pendingDeleteId)) { root.pendingDeleteId = ""; permanentDeleteDialog.close() } }
                }
            }
        }
    }
}
