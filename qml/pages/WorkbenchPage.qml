pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench
import "../components"

Item {
    id: root
    signal openEnvironment()
    signal openFlight()
    signal openMission(bool showLibrary)
    signal openHealth()
    signal loadRoute(string routeId)

    readonly property var currentPlan: App.selectedTestPlan || ({})
    readonly property bool noPlan: !Boolean(currentPlan.id)
    readonly property bool planNeedsRoute: Boolean(currentPlan.requiresRoute)
                                                && !Boolean(currentPlan.routeAvailable)
    readonly property bool environmentReady: App.dockerAvailable && App.imageAvailable
    readonly property bool compactLayout: width < 820 || height < 470
    property string pendingDeletePlanId: ""

    function launchTitle() {
        if (App.busy) return "正在处理"
        if (App.vehicleConnected) return "仿真已连接"
        if (App.simulationRunning) return "仿真正在启动"
        if (root.noPlan) return "先保存一个测试方案"
        if (root.planNeedsRoute) return "测试方案尚未完整"
        return "仿真尚未启动"
    }

    function launchDescription() {
        if (App.hasIssue) return App.issueMessage
        if (root.noPlan) return "设置机型和场景，再把当前配置保存为方案"
        return App.testRunGuidance
    }

    function primaryText() {
        if (App.hasIssue) return App.recoveryLabel
        if (root.noPlan) return "创建方案"
        if (App.vehicleReady) return Boolean(root.currentPlan.routeAvailable) ? "打开航线" : "进入飞行"
        if (App.simulationRunning) return "停止仿真"
        if (root.planNeedsRoute) return "完善方案"
        return "启动仿真"
    }

    function primaryHint() {
        if (App.busy) return "请等待当前操作完成"
        if (App.hasIssue) return App.issueMessage
        if (root.noPlan) return "保存当前机型、场景和飞行设置"
        if (App.vehicleReady) return "进入飞行页面"
        if (App.simulationRunning) return "停止当前本地仿真"
        if (root.planNeedsRoute) return "请先为测试方案选择一条航线"
        if (!App.dockerAvailable) return "请先启动 Docker Desktop"
        if (!App.imageAvailable) return "未找到 PX4 SITL 镜像"
        return "启动 " + App.selectedModelName + " 本地仿真"
    }

    function runPrimaryAction() {
        if (!App.simulationRunning && root.noPlan) planEditor.prepare({})
        else if (!App.simulationRunning && root.planNeedsRoute) planEditor.prepare(root.currentPlan)
        else if (App.hasIssue && App.recoveryAction === "health") root.openHealth()
        else if (App.hasIssue && App.recoveryAction === "environment") root.openEnvironment()
        else if (App.hasIssue) App.recover()
        else if (App.vehicleReady && Boolean(root.currentPlan.routeAvailable))
            root.loadRoute(String(root.currentPlan.routeId || ""))
        else if (App.vehicleReady) root.openFlight()
        else if (App.simulationRunning) App.abortTest()
        else preflightDialog.open()
    }

    function planSummary() {
        if (root.noPlan) return "保存当前设置后即可重复使用"
        if (App.selectedTestPlanModified)
            return "当前环境与已保存方案不同 · 可更新方案或重新应用"
        if (root.planNeedsRoute)
            return String(root.currentPlan.modelName || "") + " · "
                   + String(root.currentPlan.worldName || "") + " · 需选择航线"
        if (root.currentPlan.routeAvailable)
            return String(root.currentPlan.routeName || "航线") + " · "
                   + Number(root.currentPlan.routeWaypointCount || 0) + " 个航点 · "
                   + Number(root.currentPlan.routeDistanceMeters || 0) + " m"
        return String(root.currentPlan.modelName || "") + " · "
               + String(root.currentPlan.worldName || "") + " · 起飞 "
               + Number(root.currentPlan.takeoffAltitudeMeters || 5).toFixed(1) + " m"
    }

    function editCurrentPlan() {
        planEditor.prepare(root.noPlan ? ({}) : root.currentPlan)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compactLayout ? 20 : 28
        anchors.rightMargin: root.compactLayout ? 20 : 28
        anchors.topMargin: root.compactLayout ? 12 : 22
        anchors.bottomMargin: root.compactLayout ? 10 : 18
        spacing: root.compactLayout ? 10 : 14

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compactLayout ? 48 : 56
            spacing: 2
            Text {
                text: "工作台"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: 24
                font.weight: Font.Medium
            }
            Text {
                text: "设置环境，保存方案，然后开始一次仿真"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compactLayout ? 236 : 260

            SurfaceCard {
                id: launchCard
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: conditionCard.left
                anchors.rightMargin: root.compactLayout ? 12 : 16

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: root.compactLayout ? 18 : 24
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.compactLayout ? 50 : 58
                        spacing: 14
                        Rectangle {
                            Layout.preferredWidth: root.compactLayout ? 44 : 50
                            Layout.preferredHeight: root.compactLayout ? 44 : 50
                            radius: 12
                            color: Theme.accentSoft
                            ThemedIcon {
                                anchors.centerIn: parent
                                width: 18
                                height: 18
                                source: root.noPlan ? "qrc:/icons/save.svg"
                                                    : App.simulationRunning ? "qrc:/icons/square.svg"
                                                                            : "qrc:/icons/play.svg"
                                iconColor: Theme.accent
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: root.launchTitle()
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSubtitle
                                font.weight: Font.Medium
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.launchDescription()
                                color: App.hasIssue ? Theme.warning : Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSmall
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Item { Layout.preferredHeight: root.compactLayout ? 10 : 18 }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

                    Item {
                        id: configItem
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.compactLayout ? 62 : 72

                        component ConfigCell: Item {
                            id: configCell
                            property string label: "项目"
                            property string value: "—"
                            property bool divider: false
                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: configCell.divider ? 16 : 0
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - (configCell.divider ? 16 : 0)
                                spacing: 6
                                Text { text: configCell.label; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                                Text { width: parent.width; text: configCell.value; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium; elide: Text.ElideRight }
                            }
                            Rectangle { visible: configCell.divider; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: Theme.line }
                        }

                        ConfigCell {
                            id: configModelCell
                            label: "飞行器"
                            value: App.selectedModelName
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: parent.width / 3
                        }
                        ConfigCell {
                            id: configWorldCell
                            label: "场景"
                            value: App.selectedWorldName
                            divider: true
                            anchors.left: configModelCell.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: parent.width / 3
                        }
                        ConfigCell {
                            id: configLinkCell
                            label: "连接"
                            value: "UDP 14540"
                            divider: true
                            anchors.left: configWorldCell.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: parent.width / 3
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
                    Item { Layout.preferredHeight: root.compactLayout ? 10 : 18 }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        ActionButton {
                            Layout.preferredWidth: 116
                            text: root.primaryText()
                            iconSource: root.noPlan ? "qrc:/icons/save.svg"
                                                   : App.simulationRunning && !App.vehicleReady
                                                     ? "qrc:/icons/square.svg" : "qrc:/icons/play.svg"
                            kind: App.hasIssue ? "secondary" : "primary"
                            toolTipText: root.primaryHint()
                            enabled: App.hasIssue ? !App.busy
                                                  : root.noPlan
                                                    || root.planNeedsRoute
                                                    || App.vehicleReady
                                                    || (App.dockerAvailable && App.imageAvailable && !App.busy)
                            onClicked: root.runPrimaryAction()
                        }
                        ActionButton {
                            Layout.preferredWidth: 120
                            text: "更改环境"
                            iconSource: "qrc:/icons/sliders-horizontal.svg"
                            kind: "secondary"
                            enabled: !App.simulationRunning && !App.busy
                            onClicked: root.openEnvironment()
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            SurfaceCard {
                id: conditionCard
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 266

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 18
                            anchors.verticalCenter: parent.verticalCenter
                            text: "测试进度"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.Medium
                        }
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
                    }
                    Repeater {
                        model: App.testPlanSteps
                        StatusRow {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: (conditionCard.height - 50) / 4
                            label: String(modelData.label || "")
                            value: String(modelData.value || "")
                            statusKind: String(modelData.state || "pending")
                            complete: statusKind === "complete"
                            dividerVisible: index < App.testPlanSteps.length - 1
                        }
                    }
                }
            }
        }

        SurfaceCard {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compactLayout ? 58 : 64

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 18
                spacing: 14
                ThemedIcon {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    source: "qrc:/icons/clipboard-check.svg"
                    iconColor: root.planNeedsRoute ? Theme.warning : Theme.accent
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 480
                        text: root.noPlan ? "还没有测试方案"
                                          : "当前方案 · " + String(root.currentPlan.name || "未命名")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 480
                        text: root.planSummary()
                        color: root.planNeedsRoute || App.selectedTestPlanModified
                               ? Theme.warning : Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        elide: Text.ElideRight
                    }
                }
                Row {
                    spacing: 8
                    CompactIconButton {
                        visible: !root.noPlan
                        iconSource: "qrc:/icons/pencil.svg"
                        accessibleLabel: "编辑当前测试方案"
                        toolTipText: "编辑方案"
                        enabled: !App.simulationRunning && !App.busy
                        onClicked: planEditor.prepare(root.currentPlan)
                    }
                    ActionButton {
                        width: implicitWidth
                        compact: true
                        text: root.noPlan ? "新建方案" : "更换方案"
                        kind: "secondary"
                        enabled: !App.simulationRunning && !App.busy
                        toolTipText: enabled
                                     ? (root.noPlan ? "保存当前设置为测试方案" : "选择已保存的方案")
                                     : "停止仿真后才能更换方案"
                        onClicked: root.noPlan ? planEditor.prepare({}) : planPicker.showPicker()
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    TestPlanPicker {
        id: planPicker
        onApplyRequested: planId => {
            App.selectedTestPlanId = planId
            planPicker.close()
        }
        onCreateRequested: {
            planPicker.close()
            planEditor.prepare({})
        }
        onEditRequested: plan => {
            planPicker.close()
            App.selectedTestPlanId = String(plan.id || "")
            planEditor.prepare(App.selectedTestPlan)
        }
        onDuplicateRequested: planId => {
            var response = App.duplicateTestPlan(planId)
            if (response.success)
                planPicker.pendingId = String(response.id || App.selectedTestPlanId)
        }
        onDeleteRequested: planId => {
            root.pendingDeletePlanId = planId
            deletePlanDialog.open()
        }
    }

    TestPlanEditDialog {
        id: planEditor
        onSaveRequested: draft => {
            var response = App.saveCurrentTestPlan(draft)
            if (response.success)
                planEditor.close()
        }
        onRouteEditRequested: draft => {
            var response = App.saveCurrentTestPlan(draft)
            if (response.success) {
                planEditor.close()
                root.loadRoute(String(draft.routeId || ""))
            }
        }
    }

    PreflightSummary {
        id: preflightDialog
        onStartRequested: {
            preflightDialog.close()
            App.startSimulation()
        }
        onPlanRequested: {
            preflightDialog.close()
            planEditor.prepare(root.currentPlan)
        }
        onEnvironmentRequested: {
            preflightDialog.close()
            root.openEnvironment()
        }
        onRouteRequested: {
            preflightDialog.close()
            if (root.currentPlan.routeAvailable)
                root.loadRoute(String(root.currentPlan.routeId || ""))
            else
                planEditor.prepare(root.currentPlan)
        }
    }

    Dialog {
        id: deletePlanDialog
        anchors.centerIn: parent
        width: Math.min(380, root.width - 40)
        modal: true
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: Theme.radiusLarge
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.lineStrong
        }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: "删除测试方案？"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSubtitle
                font.weight: Font.Medium
            }
            Text {
                Layout.fillWidth: true
                text: "这个方案将从本机删除，航线和飞行记录不会受到影响。"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    Layout.preferredWidth: 82
                    text: "取消"
                    kind: "secondary"
                    onClicked: deletePlanDialog.close()
                }
                ActionButton {
                    Layout.preferredWidth: 82
                    text: "删除"
                    kind: "danger"
                    onClicked: {
                        var response = App.deleteTestPlan(root.pendingDeletePlanId)
                        if (response.success)
                            planPicker.pendingId = App.selectedTestPlanId
                        root.pendingDeletePlanId = ""
                        deletePlanDialog.close()
                    }
                }
            }
        }
    }
}
