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

    function numberText(value, suffix) {
        return Number(value || 0).toFixed(1) + suffix
    }

    function statusColor(status) {
        if (status === "记录中") return Theme.accent
        if (status === "有异常" || status === "未完成") return Theme.warning
        return Theme.success
    }

    function selectSession(sessionId) {
        if (!sessionId || sessionId.length === 0) return
        selectedSessionId = sessionId
        selectedSession = App.sessionDetails(sessionId)
        exportPath = ""
    }

    function ensureSelection() {
        if (App.flightSessionActive) {
            selectSession(String(App.activeFlightSession.id || ""))
            return
        }
        if (App.flightSessions.length > 0) {
            let exists = false
            for (let i = 0; i < App.flightSessions.length; ++i) {
                if (String(App.flightSessions[i].id || "") === selectedSessionId) {
                    exists = true
                    break
                }
            }
            if (!exists) selectSession(String(App.flightSessions[0].id || ""))
        } else {
            selectedSessionId = ""
            selectedSession = ({})
        }
    }

    Component.onCompleted: ensureSelection()

    Connections {
        target: App
        function onActiveFlightSessionChanged() { root.ensureSelection() }
        function onFlightSessionsChanged() {
            root.ensureSelection()
            if (root.selectedSessionId.length > 0)
                root.selectedSession = App.sessionDetails(root.selectedSessionId)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        anchors.topMargin: 20
        anchors.bottomMargin: 18
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: "记录"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: 24
                    font.weight: Font.Medium
                }
                Text {
                    text: "每次仿真自动保存为一个会话"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                }
            }
            ActionButton {
                Layout.preferredWidth: 114
                compact: true
                text: "保存位置"
                iconSource: "qrc:/icons/folder-open.svg"
                kind: "secondary"
                onClicked: App.openSessionsFolder()
            }
        }

        SurfaceCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    Layout.preferredWidth: Math.max(250, parent.width * 0.34)
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Item {
                            visible: App.flightSessionActive
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? 72 : 0
                            Rectangle {
                                anchors.fill: parent
                                color: root.selectedSessionId === String(App.activeFlightSession.id || "")
                                       ? Theme.accentSoft : activeMouse.containsMouse ? Theme.surfaceRaised : "transparent"
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 14
                                spacing: 10
                                Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: Theme.accent }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text { text: "当前会话"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                                    Text { text: String(App.activeFlightSession.durationLabel || "00:00") + " · 正在记录"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                                }
                            }
                            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
                            MouseArea {
                                id: activeMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.selectSession(String(App.activeFlightSession.id || ""))
                            }
                        }

                        ListView {
                            id: sessionList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: App.flightSessions
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Item {
                                id: sessionRow
                                required property int index
                                required property var modelData
                                width: sessionList.width
                                height: 74
                                activeFocusOnTab: true
                                Accessible.name: String(modelData.missionRouteName || modelData.modelName || "飞行会话")
                                Accessible.role: Accessible.Button
                                Keys.onReturnPressed: root.selectSession(String(modelData.id || ""))
                                Keys.onEnterPressed: root.selectSession(String(modelData.id || ""))
                                Keys.onSpacePressed: root.selectSession(String(modelData.id || ""))

                                Rectangle {
                                    anchors.fill: parent
                                    color: root.selectedSessionId === String(sessionRow.modelData.id || "")
                                           ? Theme.accentSoft : rowMouse.containsMouse ? Theme.surfaceRaised : "transparent"
                                }
                                Rectangle {
                                    visible: sessionRow.activeFocus
                                    anchors.fill: parent
                                    radius: Theme.radiusSmall
                                    color: "transparent"
                                    border.width: 1
                                    border.color: Theme.accent
                                }
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 16
                                    anchors.rightMargin: 14
                                    spacing: 3
                                    Item { Layout.fillHeight: true }
                                    Text {
                                        Layout.fillWidth: true
                                        text: String(sessionRow.modelData.missionRouteName || "").length > 0
                                              ? String(sessionRow.modelData.missionRouteName)
                                              : String(sessionRow.modelData.modelName || "飞行会话")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textSmall
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: String(sessionRow.modelData.startedLabel || "") + " · "
                                              + String(sessionRow.modelData.durationLabel || "00:00")
                                        color: Theme.textMuted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textTiny
                                        elide: Text.ElideRight
                                    }
                                    Item { Layout.fillHeight: true }
                                }
                                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
                                MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.selectSession(String(sessionRow.modelData.id || "")) }
                            }
                        }

                        Column {
                            visible: !App.flightSessionActive && App.flightSessions.length === 0
                            Layout.alignment: Qt.AlignCenter
                            spacing: 6
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "还没有飞行记录"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "启动一次仿真后会自动创建"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                        }
                    }

                    Rectangle { anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right; width: 1; color: Theme.line }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        visible: root.selectedSessionId.length > 0
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 56
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    text: String(root.selectedSession.missionRouteName || "").length > 0
                                          ? String(root.selectedSession.missionRouteName)
                                          : String(root.selectedSession.modelName || "飞行会话")
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textTitle
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: String(root.selectedSession.modelName || "") + " · " + String(root.selectedSession.status || "")
                                    color: root.statusColor(String(root.selectedSession.status || ""))
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textSmall
                                }
                            }
                            CompactIconButton {
                                iconSource: "qrc:/icons/folder-open.svg"
                                accessibleLabel: "打开当前会话目录"
                                toolTipText: "打开目录"
                                onClicked: App.openSessionFolder(root.selectedSessionId)
                            }
                            CompactIconButton {
                                iconSource: "qrc:/icons/file-down.svg"
                                accessibleLabel: "导出当前会话 CSV"
                                toolTipText: "导出 CSV"
                                onClicked: root.exportPath = App.exportSessionCsv(root.selectedSessionId)
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 78
                            Layout.minimumHeight: 78
                            Layout.maximumHeight: 78
                            spacing: 0

                            component SummaryValue: Item {
                                id: summaryValue
                                property string label: "摘要"
                                property string value: "—"
                                property bool divider: false
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Column {
                                    anchors.left: parent.left
                                    anchors.leftMargin: summaryValue.divider ? 18 : 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 7
                                    Text { text: summaryValue.label; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                                    Text { text: summaryValue.value; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: 18; font.weight: Font.DemiBold }
                                }
                                Rectangle { visible: summaryValue.divider; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: Theme.line }
                            }

                            SummaryValue { label: "持续时间"; value: String(root.selectedSession.durationLabel || "00:00") }
                            SummaryValue { label: "最大高度"; value: root.numberText(root.selectedSession.maxAltitude, " m"); divider: true }
                            SummaryValue { label: "最大速度"; value: root.numberText(root.selectedSession.maxHorizontalSpeed, " m/s"); divider: true }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
                        Item { Layout.preferredHeight: 18 }

                        Text {
                            text: "会话事件"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.Medium
                        }
                        Item { Layout.preferredHeight: 10 }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: root.selectedSession.events || []
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Item {
                                id: eventRow

                                required property int index
                                required property string modelData
                                width: ListView.view.width
                                height: 46
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 12
                                    Rectangle {
                                        Layout.preferredWidth: 7
                                        Layout.preferredHeight: 7
                                        radius: 4
                                        color: eventRow.index === 0 ? Theme.accent : Theme.success
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: eventRow.modelData
                                        color: Theme.textSecondary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textSmall
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Text {
                            visible: !root.selectedSession.events || root.selectedSession.events.length === 0
                            Layout.fillWidth: true
                            Layout.topMargin: 18
                            text: "暂无会话事件"
                            color: Theme.textFaint
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                        }

                        Text {
                            visible: root.exportPath.length > 0
                            Layout.fillWidth: true
                            text: "已导出到 " + root.exportPath
                            color: Theme.textFaint
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            elide: Text.ElideMiddle
                        }
                    }

                    Text {
                        visible: root.selectedSessionId.length === 0
                        anchors.centerIn: parent
                        text: "选择一个会话查看详情"
                        color: Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                    }
                }
            }
        }
    }
}
