pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench
import "../components"

Item {
    id: root
    property string selectedSessionId: ""
    property var selectedSession: ({})
    property string exportPath: ""
    signal openHealth()

    function statusColor(status) {
        if (status === "记录中") return Theme.accent
        if (status === "有异常" || status === "未完成") return Theme.warning
        return Theme.success
    }

    function numberText(value, suffix) {
        return Number(value || 0).toFixed(1) + suffix
    }

    function openDetails(sessionId) {
        if (!sessionId || sessionId.length === 0) return
        selectedSessionId = sessionId
        selectedSession = App.sessionDetails(sessionId)
        exportPath = ""
        detailDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 26
        anchors.rightMargin: 26
        anchors.topMargin: 20
        anchors.bottomMargin: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            ColumnLayout {
                spacing: 3
                Text { text: "记录"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 22; font.weight: Font.Medium }
                Text { text: "飞行会话与本地遥测数据"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
            }
            Item { Layout.fillWidth: true }
            IconLink {
                text: "打开保存位置"
                iconSource: "qrc:/icons/folder-open.svg"
                onClicked: App.openSessionsFolder()
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: App.flightSessionActive ? 68 : 48
            enabled: App.flightSessionActive
            activeFocusOnTab: enabled
            Accessible.name: App.flightSessionActive ? "查看当前飞行会话" : "暂无当前飞行会话"
            Accessible.role: Accessible.Button
            Keys.onReturnPressed: root.openDetails(App.activeFlightSession.id)
            Keys.onEnterPressed: root.openDetails(App.activeFlightSession.id)
            Keys.onSpacePressed: root.openDetails(App.activeFlightSession.id)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 6
                spacing: 12
                Rectangle {
                    Layout.preferredWidth: 9
                    Layout.preferredHeight: 9
                    radius: 5
                    color: App.flightSessionActive ? Theme.accent : Theme.textFaint
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        text: App.flightSessionActive ? "正在记录 " + App.activeFlightSession.modelName : "暂无进行中的会话"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textBody
                        font.weight: Font.Medium
                    }
                    Text {
                        text: App.flightSessionActive
                              ? App.activeFlightSession.durationLabel + " · 最大高度 " + root.numberText(App.activeFlightSession.maxAltitude, " m") + " · " + App.activeFlightSession.sampleCount + " 个采样"
                              : "启动仿真后会自动开始记录。"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                }
                Text { visible: App.flightSessionActive; text: "查看"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
            }
            MouseArea {
                anchors.fill: parent
                enabled: App.flightSessionActive
                hoverEnabled: true
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.openDetails(App.activeFlightSession.id)
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            Text { text: "最近会话"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
            Item { Layout.fillWidth: true }
            Text { text: App.flightSessions.length + " 个"; color: Theme.textMuted; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textTiny }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: sessionList
                anchors.fill: parent
                clip: true
                model: App.flightSessions
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    id: sessionDelegate
                    required property int index
                    required property var modelData
                    width: sessionList.width
                    height: 60
                    activeFocusOnTab: true
                    Accessible.name: sessionDelegate.modelData.modelName + "，" + sessionDelegate.modelData.startedLabel + "，" + sessionDelegate.modelData.status
                    Accessible.role: Accessible.Button
                    Keys.onReturnPressed: root.openDetails(sessionDelegate.modelData.id)
                    Keys.onEnterPressed: root.openDetails(sessionDelegate.modelData.id)
                    Keys.onSpacePressed: root.openDetails(sessionDelegate.modelData.id)

                    Rectangle { anchors.fill: parent; color: sessionMouse.containsMouse ? Theme.surfaceRaised : "transparent"; opacity: 0.62 }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 8
                        spacing: 12
                        Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: root.statusColor(sessionDelegate.modelData.status) }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: sessionDelegate.modelData.modelName; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                                Text {
                                    visible: String(sessionDelegate.modelData.missionRouteName || "").length > 0
                                    text: String(sessionDelegate.modelData.missionRouteName || "")
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textTiny
                                }
                                Text { text: sessionDelegate.modelData.status; color: root.statusColor(sessionDelegate.modelData.status); font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                            }
                            Text { text: sessionDelegate.modelData.startedLabel + " · " + sessionDelegate.modelData.durationLabel; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                        }
                        ColumnLayout {
                            spacing: 3
                            Text { Layout.alignment: Qt.AlignRight; text: root.numberText(sessionDelegate.modelData.maxAltitude, " m"); color: Theme.textSecondary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall }
                            Text { Layout.alignment: Qt.AlignRight; text: "最大高度"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                        }
                    }
                    MouseArea { id: sessionMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openDetails(sessionDelegate.modelData.id) }
                }
            }

            Column {
                visible: App.flightSessions.length === 0
                anchors.centerIn: parent
                spacing: 7
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "还没有历史会话"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "完成一次仿真后，会话摘要会保存在这里。"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
            }
        }
    }

    Connections {
        target: App
        function onActiveFlightSessionChanged() {
            if (detailDialog.opened && root.selectedSessionId === App.activeFlightSession.id)
                root.selectedSession = App.sessionDetails(root.selectedSessionId)
        }
        function onFlightSessionsChanged() {
            if (detailDialog.opened && root.selectedSessionId.length > 0)
                root.selectedSession = App.sessionDetails(root.selectedSessionId)
        }
    }

    Dialog {
        id: detailDialog
        anchors.centerIn: parent
        width: Math.min(600, root.width - 42)
        height: Math.min(530, root.height - 34)
        modal: true
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { radius: Theme.radiusLarge; color: Theme.surface; border.width: 1; border.color: Theme.lineStrong }

        contentItem: ColumnLayout {
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text { text: root.selectedSession.modelName || "飞行会话"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
                    Text {
                        text: (root.selectedSession.startedLabel || "") + " · "
                              + (root.selectedSession.durationLabel || "00:00")
                              + (String(root.selectedSession.missionRouteName || "").length > 0
                                 ? " · " + root.selectedSession.missionRouteName : "")
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                }
                Text { text: root.selectedSession.status || ""; color: root.statusColor(root.selectedSession.status); font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                CompactIconButton {
                    iconSource: "qrc:/icons/x.svg"
                    accessibleLabel: "关闭详情"
                    toolTipText: "关闭"
                    onClicked: detailDialog.close()
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 18
                rowSpacing: 5
                Text { text: "最大高度"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                Text { text: "最大水平速度"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                Text { text: "最大垂直速度"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                Text { text: root.numberText(root.selectedSession.maxAltitude, " m"); color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textBody; font.weight: Font.Medium }
                Text { text: root.numberText(root.selectedSession.maxHorizontalSpeed, " m/s"); color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textBody; font.weight: Font.Medium }
                Text { text: root.numberText(root.selectedSession.maxVerticalSpeed, " m/s"); color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textBody; font.weight: Font.Medium }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: Number(root.selectedSession.missionTotalItems || 0) > 0
                spacing: 18
                Text {
                    text: "任务完成 " + Number(root.selectedSession.missionCompletionPercent || 0) + "%"
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                }
                Text {
                    text: "最大偏差 " + root.numberText(root.selectedSession.missionMaxCrossTrackError, " m")
                    color: Number(root.selectedSession.missionMaxCrossTrackError || 0) > 3
                           ? Theme.warning : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                }
                Text {
                    visible: String(root.selectedSession.missionInterruptionReason || "").length > 0
                    text: "中断：" + root.selectedSession.missionInterruptionReason
                    color: Theme.warning
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Item { Layout.fillWidth: true; visible: String(root.selectedSession.missionInterruptionReason || "").length === 0 }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                Text { text: "健康 " + (root.selectedSession.healthResult || "—"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                Text { text: "起飞 " + (root.selectedSession.takeoffResult || "—"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                Text { text: "降落 " + (root.selectedSession.landingResult || "—"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                Text { text: (root.selectedSession.issueCount || 0) + " 个问题"; color: Number(root.selectedSession.issueCount || 0) > 0 ? Theme.warning : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: (root.selectedSession.sampleCount || 0) + " 个遥测采样"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                Item { Layout.fillWidth: true }
                ActionButton { Layout.preferredWidth: 108; text: "打开目录"; iconSource: "qrc:/icons/folder-open.svg"; kind: "secondary"; compact: true; onClicked: App.openSessionFolder(root.selectedSessionId) }
                ActionButton { Layout.preferredWidth: 108; text: "导出 CSV"; iconSource: "qrc:/icons/file-down.svg"; compact: true; onClicked: root.exportPath = App.exportSessionCsv(root.selectedSessionId) }
            }

            Text { visible: root.exportPath.length > 0; Layout.fillWidth: true; text: "已导出到 " + root.exportPath; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideMiddle }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
            Text { text: "会话事件"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.selectedSession.events || []
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Item {
                    required property string modelData
                    width: ListView.view.width
                    height: Math.max(34, eventText.implicitHeight + 12)
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
                    Text { id: eventText; anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: modelData; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.Wrap }
                }
            }

            Text { visible: !root.selectedSession.events || root.selectedSession.events.length === 0; Layout.fillWidth: true; text: "暂无会话事件"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; horizontalAlignment: Text.AlignHCenter }
        }
    }
}
