pragma ComponentBehavior: Bound
import QtQuick
import "../controls"
import "../features/plans"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

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

    function completedStepCount() {
        const steps = App.testPlanSteps || []
        let count = 0
        for (let index = 0; index < steps.length; ++index) {
            if (String(steps[index].state || "") === "complete") ++count
        }
        return count
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.canvasSoft }
            GradientStop { position: 1; color: Theme.canvas }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: -150
        anchors.top: parent.top
        anchors.topMargin: -230
        width: 520
        height: 520
        radius: 260
        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                       Theme.darkMode ? 0.075 : 0.045)
    }

    ColumnLayout {
        z: 1
        anchors.fill: parent
        anchors.leftMargin: root.compactLayout ? 20 : 28
        anchors.rightMargin: root.compactLayout ? 20 : 28
        anchors.topMargin: root.compactLayout ? 12 : 22
        anchors.bottomMargin: root.compactLayout ? 10 : 18
        spacing: root.compactLayout ? 10 : 14

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compactLayout ? 48 : 56
            spacing: 12
            Rectangle {
                Layout.preferredWidth: 3
                Layout.preferredHeight: root.compactLayout ? 32 : 38
                radius: 2
                color: Theme.accent
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: "飞行测试工作台"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: root.compactLayout ? 23 : 27
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "配置环境、执行测试并保留每一次飞行证据"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                }
            }
            Rectangle {
                visible: !root.compactLayout
                Layout.preferredWidth: 112
                Layout.preferredHeight: 28
                radius: 14
                color: Theme.accentSoft
                border.width: 1
                border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                Text {
                    anchors.centerIn: parent
                    text: "PX4 · LOCAL"
                    color: Theme.accent
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.textTiny
                    font.weight: Font.DemiBold
                }
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
                elevated: true
                clip: true
                border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                                      Theme.darkMode ? 0.5 : 0.36)
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.heroStart }
                    GradientStop { position: 1; color: Theme.heroEnd }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: -75
                    anchors.top: parent.top
                    anchors.topMargin: -105
                    width: 250
                    height: 250
                    radius: 125
                    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.13)
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 25
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -85
                    width: 180
                    height: 180
                    radius: 90
                    color: Qt.rgba(0.22, 0.78, 0.67, 0.065)
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: root.compactLayout ? 18 : 24
                    anchors.top: parent.top
                    anchors.topMargin: Theme.edgeInset
                    width: 72
                    height: 3
                    radius: 2
                    color: Theme.accent
                }

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
                            color: Theme.heroSurface
                            border.width: 1
                            border.color: Theme.heroLine
                            ThemedIcon {
                                anchors.centerIn: parent
                                width: 18
                                height: 18
                                source: root.noPlan ? "qrc:/icons/save.svg"
                                                    : App.simulationRunning ? "qrc:/icons/square.svg"
                                                                            : "qrc:/icons/play.svg"
                                iconColor: "#79ADFF"
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: root.launchTitle()
                                color: Theme.heroText
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSubtitle
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.launchDescription()
                                color: App.hasIssue ? "#F6BB69" : Theme.heroMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSmall
                                elide: Text.ElideRight
                            }
                        }
                        Rectangle {
                            visible: !root.compactLayout
                            Layout.preferredWidth: 76
                            Layout.preferredHeight: 28
                            radius: 14
                            color: Qt.rgba(1, 1, 1, 0.07)
                            border.width: 1
                            border.color: Theme.heroLine
                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 6
                                Rectangle {
                                    Layout.preferredWidth: 6
                                    Layout.preferredHeight: 6
                                    radius: 3
                                    color: App.vehicleReady ? "#55D6B1"
                                           : App.simulationRunning ? "#79ADFF" : Theme.heroMuted
                                }
                                Text {
                                    text: App.vehicleReady ? "就绪"
                                          : App.simulationRunning ? "运行中" : "待机"
                                    color: Theme.heroText
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textTiny
                                    font.weight: Font.Medium
                                }
                            }
                        }
                    }

                    Item { Layout.preferredHeight: root.compactLayout ? 10 : 14 }

                    Item {
                        id: configItem
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.compactLayout ? 62 : 72

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusMedium
                            color: Qt.rgba(1, 1, 1, 0.055)
                            border.width: 1
                            border.color: Theme.heroLine
                        }

                        component ConfigCell: Item {
                            id: configCell
                            property string label: "项目"
                            property string value: "—"
                            property bool divider: false
                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: configCell.divider ? 20 : 16
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(0, parent.width
                                                   - (configCell.divider ? 20 : 16) - 12)
                                spacing: 6
                                Text { text: configCell.label; color: Theme.heroMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                                Text { width: parent.width; text: configCell.value; color: Theme.heroText; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            }
                            Rectangle { visible: configCell.divider; anchors.left: parent.left; anchors.top: parent.top; anchors.topMargin: 12; anchors.bottom: parent.bottom; anchors.bottomMargin: 12; width: 1; color: Theme.heroLine }
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

                    Item { Layout.preferredHeight: root.compactLayout ? 10 : 14 }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        ActionButton {
                            Layout.preferredWidth: 116
                            text: root.primaryText()
                            iconSource: root.noPlan ? "qrc:/icons/save.svg"
                                                   : App.simulationRunning && !App.vehicleReady
                                                     ? "qrc:/icons/square.svg" : "qrc:/icons/play.svg"
                            kind: App.hasIssue ? "glass" : "primary"
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
                            kind: "glass"
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
                color: Theme.darkMode ? Theme.surface : Theme.canvasSoft

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            Text {
                                Layout.fillWidth: true
                                text: "测试进度"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSmall
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: root.completedStepCount() + " / "
                                      + Math.max(1, (App.testPlanSteps || []).length)
                                color: Theme.accent
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.textTiny
                                font.weight: Font.DemiBold
                            }
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 3
                            color: Theme.line
                            Rectangle {
                                width: parent.width * root.completedStepCount()
                                       / Math.max(1, (App.testPlanSteps || []).length)
                                height: parent.height
                                color: Theme.accent
                                Behavior on width {
                                    NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic }
                                }
                            }
                        }
                    }
                    Repeater {
                        model: App.testPlanSteps
                        StatusRow {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: (conditionCard.height - 54) / 4
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

            ThemedIcon {
                id: currentPlanIcon
                anchors.left: parent.left
                anchors.leftMargin: Theme.panelInset
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                source: "qrc:/icons/clipboard-check.svg"
                iconColor: root.planNeedsRoute ? Theme.warning : Theme.accent
            }
            ColumnLayout {
                anchors.left: currentPlanIcon.right
                anchors.leftMargin: 14
                anchors.right: planActions.left
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    Layout.fillWidth: true
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
                    text: root.planSummary()
                    color: root.planNeedsRoute || App.selectedTestPlanModified
                           ? Theme.warning : Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    elide: Text.ElideRight
                }
            }
            Row {
                id: planActions
                anchors.right: parent.right
                anchors.rightMargin: Theme.panelInset
                anchors.verticalCenter: parent.verticalCenter
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

    MotionDialog {
        id: deletePlanDialog
        anchors.centerIn: parent
        width: Math.min(380, root.width - 40)
        modal: true
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: DialogSurface {
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
