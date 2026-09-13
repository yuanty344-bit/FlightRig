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
    property bool batchMode: false
    property var selectedTrashIds: []
    property var pendingDeleteIds: []
    readonly property int selectedTrashCount: selectedTrashIds.length

    function hasSelectedItem(sessionId) {
        return selectedTrashIds.indexOf(String(sessionId || "")) >= 0
    }

    function toggleSelectedItem(sessionId) {
        const id = String(sessionId || "")
        const next = selectedTrashIds.slice()
        const index = next.indexOf(id)
        if (index >= 0) next.splice(index, 1)
        else if (id.length > 0) next.push(id)
        selectedTrashIds = next
    }

    function allTrashIds() {
        const ids = []
        for (let index = 0; index < App.trashedFlightSessions.length; ++index)
            ids.push(String(App.trashedFlightSessions[index].id || ""))
        return ids
    }

    function leaveBatchMode() {
        batchMode = false
        selectedTrashIds = []
    }

    function requestPermanentDelete(sessionIds) {
        pendingDeleteIds = sessionIds.slice()
        if (pendingDeleteIds.length > 0) permanentDeleteDialog.open()
    }

    onClosed: leaveBatchMode()

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
                anchors.right: batchButton.left
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
            CompactIconButton {
                id: batchButton
                visible: App.trashedFlightSessions.length > 1 || root.batchMode
                anchors.right: closeButton.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                iconSource: "qrc:/icons/square.svg"
                accessibleLabel: root.batchMode ? "退出批量选择" : "批量选择回收站记录"
                toolTipText: root.batchMode ? "退出批量选择" : "批量永久删除"
                outlined: root.batchMode
                onClicked: root.batchMode ? root.leaveBatchMode() : root.batchMode = true
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
                id: trashRow
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
                    Rectangle {
                        visible: root.batchMode
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        radius: 4
                        color: root.hasSelectedItem(trashRow.modelData.id) ? Theme.accent : "transparent"
                        border.width: 1
                        border.color: root.hasSelectedItem(trashRow.modelData.id) ? Theme.accent : Theme.lineStrong
                        Text {
                            visible: root.hasSelectedItem(trashRow.modelData.id)
                            anchors.centerIn: parent
                            text: "✓"
                            color: Theme.accentContent
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            font.weight: Font.DemiBold
                        }
                    }
                    ThemedIcon { visible: !root.batchMode; Layout.preferredWidth: 18; Layout.preferredHeight: 18; source: "qrc:/icons/archive-restore.svg"; iconColor: Theme.textMuted }
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
                        visible: !root.batchMode
                        iconSource: "qrc:/icons/archive-restore.svg"; accessibleLabel: "恢复飞行记录"; toolTipText: "恢复到记录列表"
                        onClicked: App.restoreSession(String(modelData.id || ""))
                    }
                    CompactIconButton {
                        visible: !root.batchMode
                        iconSource: "qrc:/icons/archive-x.svg"; accessibleLabel: "永久删除飞行记录"; toolTipText: "永久删除"; danger: true
                        onClicked: { root.pendingDeleteId = String(modelData.id || ""); root.requestPermanentDelete([root.pendingDeleteId]) }
                    }
                }
                MouseArea {
                    visible: root.batchMode
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.toggleSelectedItem(trashRow.modelData.id)
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
        Rectangle {
            visible: root.batchMode
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 44 : 0
            radius: Theme.radiusMedium
            color: Theme.surface
            border.width: 1
            border.color: Theme.line
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                Text {
                    text: "已选 " + root.selectedTrashCount + " 项"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    Layout.preferredWidth: 72
                    compact: true
                    text: root.selectedTrashCount === App.trashedFlightSessions.length ? "清空选择" : "全选"
                    kind: "secondary"
                    onClicked: root.selectedTrashCount === App.trashedFlightSessions.length
                               ? root.selectedTrashIds = [] : root.selectedTrashIds = root.allTrashIds()
                }
                ActionButton {
                    Layout.preferredWidth: 104
                    compact: true
                    text: "永久删除"
                    kind: "danger"
                    enabled: root.selectedTrashCount > 0
                    disabledReason: "请至少选择一条飞行记录"
                    onClicked: root.requestPermanentDelete(root.selectedTrashIds)
                }
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
            Text { text: root.pendingDeleteIds.length > 1 ? "永久删除 " + root.pendingDeleteIds.length + " 条飞行记录？" : "永久删除这条飞行记录？"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            Text { Layout.fillWidth: true; text: "所选记录的摘要、事件和遥测数据将无法恢复。"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textBody; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { Layout.preferredWidth: 82; text: "取消"; kind: "secondary"; onClicked: permanentDeleteDialog.close() }
                ActionButton {
                    Layout.preferredWidth: 100; text: "永久删除"; kind: "danger"
                    onClicked: {
                        const ids = root.pendingDeleteIds.slice()
                        for (let index = 0; index < ids.length; ++index)
                            App.permanentlyDeleteSession(ids[index])
                        root.pendingDeleteId = ""
                        root.pendingDeleteIds = []
                        root.leaveBatchMode()
                        permanentDeleteDialog.close()
                    }
                }
            }
        }
    }
}
