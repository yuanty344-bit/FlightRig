pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Dialog {
    id: root
    signal startRequested()
    signal planRequested()
    signal environmentRequested()
    signal routeRequested()

    readonly property var summary: App.preflightSummary || ({})
    readonly property var checks: summary.checks || []

    anchors.centerIn: parent
    width: Math.min(610, parent ? parent.width - 40 : 610)
    height: Math.min(600, parent ? parent.height - 36 : 600)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.lineStrong
    }

    contentItem: ColumnLayout {
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 16
                spacing: 12
                ThemedIcon {
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    source: root.summary.canStart ? "qrc:/icons/shield-check.svg"
                                                  : "qrc:/icons/triangle-alert.svg"
                    iconColor: root.summary.canStart ? Theme.success : Theme.warning
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        text: "启动前检查"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTitle
                        font.weight: Font.Medium
                    }
                    Text {
                        Layout.fillWidth: true
                        text: String(root.summary.summary || "正在检查…")
                        color: root.summary.canStart ? Theme.textMuted : Theme.warning
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        elide: Text.ElideRight
                    }
                }
                CompactIconButton {
                    iconSource: "qrc:/icons/x.svg"
                    accessibleLabel: "关闭启动检查"
                    toolTipText: "关闭"
                    onClicked: root.close()
                }
            }
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
        }

        ListView {
            id: checkList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            clip: true
            spacing: 2
            model: root.checks
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: checkRow
                required property var modelData
                width: ListView.view.width
                height: Math.max(50, detailText.implicitHeight + 28)
                readonly property string status: String(modelData.status || "pass")
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 12
                    ThemedIcon {
                        Layout.preferredWidth: 17
                        Layout.preferredHeight: 17
                        source: checkRow.status === "pass" ? "qrc:/icons/circle-check.svg"
                                : checkRow.status === "warning" ? "qrc:/icons/circle-alert.svg"
                                                                 : "qrc:/icons/triangle-alert.svg"
                        iconColor: checkRow.status === "pass" ? Theme.success
                                   : checkRow.status === "warning" ? Theme.warning : Theme.danger
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: String(checkRow.modelData.label || "检查项")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.Medium
                        }
                        Text {
                            id: detailText
                            Layout.fillWidth: true
                            text: String(checkRow.modelData.detail || "")
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            wrapMode: Text.WordWrap
                        }
                    }
                    ActionButton {
                        visible: String(checkRow.modelData.action || "").length > 0
                                 && checkRow.status !== "pass"
                        compact: true
                        implicitWidth: String(checkRow.modelData.action) === "auto-fix" ? 82 : 72
                        text: String(checkRow.modelData.action) === "auto-fix" ? "自动修正" : "处理"
                        kind: "secondary"
                        onClicked: {
                            const action = String(checkRow.modelData.action || "")
                            if (action === "auto-fix") App.autoFixSelectedPlanRoute()
                            else if (action === "environment") root.environmentRequested()
                            else if (action === "route") root.routeRequested()
                            else root.planRequested()
                        }
                    }
                }
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: Theme.line }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: Number(root.summary.warningCount || 0) > 0 && root.summary.canStart
                          ? "提醒不会阻止启动，测试结果仍会记录这些项目。" : ""
                    color: Theme.textFaint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    elide: Text.ElideRight
                }
                ActionButton {
                    implicitWidth: 84
                    text: "返回"
                    kind: "secondary"
                    onClicked: root.close()
                }
                ActionButton {
                    implicitWidth: 118
                    text: "启动仿真"
                    iconSource: "qrc:/icons/rocket.svg"
                    enabled: Boolean(root.summary.canStart)
                    disabledReason: String(root.summary.summary || "存在必须处理的问题")
                    onClicked: root.startRequested()
                }
            }
        }
    }
}
