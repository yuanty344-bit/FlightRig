pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs as PlatformDialogs
import FlightBench

MotionDialog {
    id: root
    property string pendingId: ""
    readonly property var pendingPlan: {
        for (var i = 0; i < App.testPlans.length; ++i) {
            if (String(App.testPlans[i].id || "") === pendingId)
                return App.testPlans[i]
        }
        return ({})
    }
    signal applyRequested(string planId)
    signal createRequested()
    signal editRequested(var plan)
    signal duplicateRequested(string planId)
    signal deleteRequested(string planId)

    function showPicker() {
        pendingId = App.selectedTestPlanId
        open()
    }

    function planSummary(plan) {
        var parts = [String(plan.typeLabel || "自由飞行"),
                     String(plan.modelName || ""), String(plan.worldName || "")]
        if (plan.requiresRoute && !plan.routeAvailable)
            parts.push("需选择航线")
        else if (plan.routeAvailable)
            parts.push(String(plan.routeName || "已选航线"))
        else
            parts.push("起飞 " + Number(plan.takeoffAltitudeMeters || 5).toFixed(1) + " m")
        if (String(plan.acceptancePreset || "standard") !== "standard")
            parts.push(String(plan.acceptancePresetLabel || "严格检查"))
        const sensor = plan.sensorSettings || ({})
        if (String(sensor.usage || "off") === "record")
            parts.push("记录传感器")
        else if (String(sensor.usage || "off") === "preview")
            parts.push("预览传感器")
        if (String(plan.id || "") === App.selectedTestPlanId && App.selectedTestPlanModified)
            parts.push("当前配置已调整")
        return parts.join(" · ")
    }

    anchors.centerIn: parent
    width: Math.min(570, parent ? parent.width - 40 : 570)
    height: Math.min(470,
                     Math.max(382, 160 + Math.min(App.testPlans.length, 4) * 74),
                     parent ? parent.height - 36 : 470)
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
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
                anchors.right: headerActions.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: "选择测试方案"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTitle
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: "选择已有方案，或把当前配置保存为新方案。"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    elide: Text.ElideRight
                }
            }

            RowLayout {
                id: headerActions
                anchors.right: closeButton.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                CompactIconButton {
                    iconSource: "qrc:/icons/upload.svg"
                    accessibleLabel: "导入测试方案"
                    toolTipText: "导入方案文件"
                    enabled: !App.simulationRunning && !App.busy
                    disabledReason: "停止仿真后才能导入方案"
                    onClicked: importDialog.open()
                }
                CompactIconButton {
                    iconSource: "qrc:/icons/file-down.svg"
                    accessibleLabel: "导出选中的测试方案"
                    toolTipText: "导出选中方案"
                    enabled: root.pendingId.length > 0
                    disabledReason: "请先选择一个方案"
                    onClicked: App.exportTestPlan(root.pendingId)
                }
                CompactIconButton {
                    iconSource: "qrc:/icons/folder-open.svg"
                    accessibleLabel: "打开测试方案目录"
                    toolTipText: "打开保存位置"
                    onClicked: App.openPlansFolder()
                }
            }

            PanelCloseButton {
                id: closeButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconSource: "qrc:/icons/x.svg"
                accessibleLabel: "关闭测试方案选择"
                toolTipText: "关闭"
                onClicked: root.close()
            }
        }

        ScrollView {
            id: planScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal: InsetScrollBar { policy: ScrollBar.AlwaysOff }
            ScrollBar.vertical: InsetScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                width: Math.max(0, planScroll.availableWidth)
                spacing: 6
                Repeater {
                    model: App.testPlans
                    delegate: Rectangle {
                        id: planOption
                        required property var modelData
                        width: parent ? parent.width : 0
                        height: 68
                        radius: Theme.radiusMedium
                        color: root.pendingId === String(modelData.id || "")
                               ? Theme.accentSoft
                               : optionMouse.containsMouse ? Theme.surfaceInteractive : Theme.surface
                        border.width: root.pendingId === String(modelData.id || "") || activeFocus ? 1 : 0
                        border.color: activeFocus ? Theme.accentHover : Theme.accent
                        activeFocusOnTab: true
                        Accessible.name: String(modelData.name || "测试方案")
                        Accessible.role: Accessible.Button
                        Keys.onReturnPressed: root.pendingId = String(modelData.id || "")
                        Keys.onEnterPressed: root.pendingId = String(modelData.id || "")
                        Keys.onSpacePressed: root.pendingId = String(modelData.id || "")

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 12
                            spacing: 12
                            Rectangle {
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                                radius: 9
                                color: "transparent"
                                border.width: 1
                                border.color: root.pendingId === String(planOption.modelData.id || "")
                                              ? Theme.accent : Theme.lineStrong
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: Theme.accent
                                    visible: root.pendingId === String(planOption.modelData.id || "")
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        text: String(planOption.modelData.name || "未命名方案")
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textSmall
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.planSummary(planOption.modelData)
                                    color: planOption.modelData.requiresRoute && !planOption.modelData.routeAvailable
                                           || (String(planOption.modelData.id || "") === App.selectedTestPlanId
                                               && App.selectedTestPlanModified)
                                           ? Theme.warning : Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textTiny
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        MouseArea {
                            id: optionMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.pendingId = String(planOption.modelData.id || "")
                            onDoubleClicked: {
                                root.pendingId = String(planOption.modelData.id || "")
                                root.applyRequested(root.pendingId)
                            }
                        }
                    }
                }
                Item {
                    width: parent ? parent.width : 0
                    height: 132
                    visible: App.testPlans.length === 0
                    Column {
                        anchors.centerIn: parent
                        spacing: 8
                        ThemedIcon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 24
                            height: 24
                            source: "qrc:/icons/clipboard-check.svg"
                            iconColor: Theme.textFaint
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "还没有保存的测试方案"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "点击下方“新建方案”保存当前设置"
                            color: Theme.textFaint
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                Layout.preferredWidth: 112
                compact: true
                text: "新建方案"
                iconSource: "qrc:/icons/save.svg"
                kind: "secondary"
                enabled: !App.simulationRunning && !App.busy
                toolTipText: enabled ? "把当前机型、场景和飞行设置保存为新方案"
                                     : "停止仿真后才能保存方案"
                onClicked: root.createRequested()
            }
            Item { Layout.fillWidth: true }
            CompactIconButton {
                visible: root.pendingId.length > 0
                iconSource: "qrc:/icons/pencil.svg"
                accessibleLabel: "编辑测试方案"
                toolTipText: "编辑方案"
                enabled: !App.simulationRunning && !App.busy
                disabledReason: "停止仿真后才能编辑方案"
                onClicked: root.editRequested(root.pendingPlan)
            }
            CompactIconButton {
                visible: root.pendingId.length > 0
                iconSource: "qrc:/icons/copy.svg"
                accessibleLabel: "复制测试方案"
                toolTipText: "复制方案"
                enabled: !App.simulationRunning && !App.busy
                disabledReason: "停止仿真后才能复制方案"
                onClicked: root.duplicateRequested(root.pendingId)
            }
            CompactIconButton {
                visible: root.pendingId.length > 0
                danger: true
                iconSource: "qrc:/icons/trash-2.svg"
                accessibleLabel: "删除测试方案"
                toolTipText: "删除方案"
                enabled: !App.simulationRunning && !App.busy
                disabledReason: "停止仿真后才能删除方案"
                onClicked: root.deleteRequested(root.pendingId)
            }
            ActionButton {
                Layout.preferredWidth: 88
                compact: true
                text: "使用"
                enabled: root.pendingId.length > 0 && !App.simulationRunning && !App.busy
                toolTipText: enabled ? "应用选中的机型、世界和起飞高度"
                                     : "停止仿真后才能更换方案"
                onClicked: root.applyRequested(root.pendingId)
            }
        }
    }

    PlatformDialogs.FileDialog {
        id: importDialog
        title: "导入 FlightBench 测试方案"
        fileMode: PlatformDialogs.FileDialog.OpenFile
        nameFilters: ["FlightBench 测试方案 (*.fbplan.json *.json)"]
        onAccepted: {
            const result = App.importTestPlan(selectedFile)
            if (result.success) root.pendingId = String(result.id || App.selectedTestPlanId)
        }
    }

}
