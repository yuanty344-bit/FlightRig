pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Item {
    id: root

    signal flightRequested()
    signal planEditRequested()

    property bool advancedOpen: false
    property bool useCustomContainerName: App.containerName !== "flightbench-px4"
    property double simulationStartedAtMs: App.simulationRunning ? Date.now() : 0
    property double runtimeClockMs: Date.now()

    readonly property var currentPlan: App.selectedTestPlan || ({})
    readonly property var currentSensorSettings: currentPlan.sensorSettings || ({})
    readonly property bool hasPlan: String(currentPlan.id || "").length > 0
    readonly property bool planComplete: hasPlan && Boolean(currentPlan.complete)
    readonly property bool sensorBlockingIssue: App.sensorServiceEnabled
                                                 && App.sensorServiceSupported
                                                 && !App.sensorServiceImageAvailable
    readonly property bool sensorRuntimeIssue: App.sensorServiceEnabled
                                                && App.sensorServiceSupported
                                                && App.simulationRunning
                                                && (App.sensorServiceProblemCode === "sensor-start-failed"
                                                    || App.sensorServiceProblemCode === "sensor-container-stopped"
                                                    || App.sensorServiceProblemCode === "docker-offline")
    readonly property bool environmentReady: App.dockerAvailable && App.imageAvailable
                                             && App.dockerNetworkAvailable
                                             && !root.sensorBlockingIssue
    readonly property bool environmentIssue: App.issueCode === "docker-missing"
                                                || App.issueCode === "docker-offline"
                                                || App.issueCode === "image-missing"
                                                || App.issueCode === "container-conflict"
                                                || App.issueCode === "start-failed"
                                                || App.issueCode === "stop-failed"
                                                || App.issueCode === "model-invalid"
                                                || App.issueCode === "model-assets-missing"
                                                || App.issueCode === "world-invalid"
                                                || App.issueCode === "world-assets-missing"
                                                || App.issueCode === "container-name-invalid"
                                                || App.issueCode === "network-missing"
                                                || App.issueCode === "network-failed"
    readonly property bool starting: App.busy && !App.simulationRunning
    readonly property bool connecting: App.simulationRunning && !App.vehicleConnected
    readonly property bool runningReady: App.simulationRunning && App.vehicleConnected
    readonly property var selectedWorldMetadata: App.selectedWorldMap || ({})

    function endActionLabel(action) {
        if (action === "land") return "自动降落"
        if (action === "rtl") return "返航"
        return "悬停"
    }

    function stateTitle() {
        if (environmentIssue || sensorBlockingIssue || sensorRuntimeIssue) return "环境需要处理"
        if (App.landingAndStopping) return "正在降落并停止"
        if (runningReady) return "仿真运行中"
        if (connecting) return "正在建立连接"
        if (starting) return "正在创建仿真环境"
        if (environmentReady) return "环境已就绪"
        return "正在检查运行环境"
    }

    function stateSummary() {
        if (environmentIssue) return App.issueMessage
        if (sensorBlockingIssue) return "已选择 ROS 2 数据服务，但本机尚未生成对应镜像。"
        if (sensorRuntimeIssue) return App.sensorServiceStatus
        if (App.landingAndStopping) return "请保持仿真运行，飞行器落地并锁定后会自动停止。"
        if (runningReady) return App.selectedModelName + " · " + App.selectedWorldName
        if (connecting) return "PX4 已启动，正在等待 MAVSDK 收到飞行器心跳。"
        if (starting) return "正在创建临时容器并加载所选世界。"
        if (environmentReady) return "Docker、PX4 镜像和通信端口均正常。"
        return "正在确认 Docker Desktop 与本机 PX4 镜像。"
    }

    function stateColor() {
        if (environmentIssue || sensorBlockingIssue || sensorRuntimeIssue) return Theme.warning
        if (runningReady) return Theme.success
        if (starting || connecting || App.landingAndStopping) return Theme.accent
        if (environmentReady) return Theme.success
        return Theme.textFaint
    }

    function readyCount() {
        return (App.dockerAvailable ? 1 : 0)
                + (App.imageAvailable ? 1 : 0)
                + (App.dockerNetworkAvailable ? 1 : 0)
                + (App.sensorServiceEnabled && App.sensorServiceSupported
                   ? (App.sensorServiceImageAvailable ? 1 : 0) : 0)
    }

    function modelPayload(modelId) {
        if (modelId.indexOf("depth") >= 0) return "深度相机"
        if (modelId.indexOf("mono_cam") >= 0) return "单目相机"
        if (modelId.indexOf("lidar") >= 0) return "二维激光雷达"
        if (modelId.indexOf("gimbal") >= 0) return "云台相机"
        return "标准四旋翼"
    }

    function sensorDataReady() {
        if (App.selectedSensorType === "lidar-2d") return App.lidarScanPoints.length > 0
        if (App.selectedSensorType === "depth-camera")
            return App.cameraRgbReady || App.cameraDepthReady
        return App.cameraRgbReady
    }

    function sensorUsageLabel() {
        const usage = String(root.currentSensorSettings.usage || "off")
        if (!App.sensorServiceEnabled) return "不启动"
        if (usage === "record") return "预览并记录"
        return "仅预览"
    }

    function sensorPerformanceLabel() {
        const mode = String(root.currentSensorSettings.performance || "standard")
        const labels = { eco: "节能", standard: "标准", smooth: "流畅", auto: "自动" }
        const effective = App.sensorEffectivePerformanceMode === "eco" ? "节能"
                        : App.sensorEffectivePerformanceMode === "smooth" ? "流畅" : "标准"
        if (mode === "auto" && App.sensorServiceRunning)
            return "自动（当前" + effective + " · " + App.sensorTargetFramesPerSecond + " FPS）"
        return String(labels[mode] || "标准") + " · "
                + (mode === "eco" ? "5" : mode === "smooth" ? "15" : "10") + " FPS"
    }

    function formatRuntime() {
        if (simulationStartedAtMs <= 0) return ""
        const totalSeconds = Math.max(0, Math.floor((runtimeClockMs - simulationStartedAtMs) / 1000))
        const minutes = Math.floor(totalSeconds / 60)
        const seconds = totalSeconds % 60
        return (minutes < 10 ? "0" : "") + minutes + ":"
                + (seconds < 10 ? "0" : "") + seconds
    }

    function startDisabledReason() {
        if (App.replayActive) return "请先退出历史回放"
        if (!planComplete) return "请先完善当前测试方案"
        if (!App.dockerAvailable) return "请先启动 Docker Desktop"
        if (!App.imageAvailable) return "本机没有找到 PX4 SITL 镜像"
        if (!App.dockerNetworkAvailable) return "FlightBench 容器网络尚未就绪"
        if (root.sensorBlockingIssue) return "请先生成 ROS 2 数据服务镜像"
        if (!App.containerNameAvailable) return "这个容器名称已经被占用"
        if (App.busy) return "正在执行另一项操作"
        return ""
    }

    component SmallToggle: Rectangle {
        id: toggle
        property bool checked: false
        property string accessibleName: "开关"
        signal toggled()
        implicitWidth: 38
        implicitHeight: 22
        radius: 11
        color: checked ? Theme.accent : Theme.lineStrong
        activeFocusOnTab: true
        Accessible.name: accessibleName
        Accessible.role: Accessible.CheckBox
        Accessible.checked: checked
        Keys.onReturnPressed: toggled()
        Keys.onEnterPressed: toggled()
        Keys.onSpacePressed: toggled()
        Rectangle {
            width: 16
            height: 16
            radius: 8
            y: 3
            x: toggle.checked ? 19 : 3
            color: toggle.checked ? Theme.accentContent : Theme.surface
            Behavior on x { NumberAnimation { duration: Theme.animationDuration(100) } }
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: toggle.toggled()
        }
    }

    component ServiceRow: Item {
        id: serviceRow
        property string label: "服务"
        property string value: "等待"
        property string statusKind: "pending"
        property url iconSource: "qrc:/icons/circle-check.svg"
        property bool dividerVisible: true
        readonly property color stateColor: statusKind === "complete" ? Theme.success
                                                  : statusKind === "current" ? Theme.accent
                                                  : statusKind === "problem" ? Theme.warning
                                                  : Theme.textFaint
        implicitHeight: 44
        RowLayout {
            anchors.fill: parent
            spacing: 10
            ThemedIcon {
                Layout.preferredWidth: 15
                Layout.preferredHeight: 15
                source: serviceRow.iconSource
                iconColor: serviceRow.stateColor
            }
            Text {
                Layout.fillWidth: true
                text: serviceRow.label
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
            }
            Text {
                text: serviceRow.value
                color: serviceRow.statusKind === "problem" ? Theme.warning
                       : serviceRow.statusKind === "current" ? Theme.accent
                       : serviceRow.statusKind === "complete" ? Theme.textMuted
                       : Theme.textFaint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
        }
        Rectangle {
            visible: serviceRow.dividerVisible
            anchors.left: parent.left
            anchors.leftMargin: 25
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.line
        }
    }

    component ValueLine: Item {
        id: valueLine
        property string label: "参数"
        property string value: "—"
        implicitHeight: Math.max(36, valueLabel.implicitHeight + 8)
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 92
            text: valueLine.label
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
        }
        Text {
            id: valueLabel
            anchors.left: parent.left
            anchors.leftMargin: 104
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
            text: valueLine.value
            color: Theme.textPrimary
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.textTiny
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }

    Timer {
        interval: 1000
        repeat: true
        running: App.simulationRunning
        onTriggered: root.runtimeClockMs = Date.now()
    }

    Connections {
        target: App
        function onSimulationRunningChanged() {
            if (App.simulationRunning) {
                root.simulationStartedAtMs = Date.now()
                root.runtimeClockMs = root.simulationStartedAtMs
            } else {
                root.simulationStartedAtMs = 0
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ScrollView {
            id: environmentScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            leftPadding: 20
            rightPadding: 14
            topPadding: 18
            bottomPadding: 18
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: environmentScroll.availableWidth
                spacing: 14

                SurfaceCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(88, statusContent.implicitHeight + 28)
                    color: root.environmentIssue ? Theme.warningSoft
                           : root.runningReady ? Theme.successSoft : Theme.surfaceRaised
                    border.color: root.environmentIssue ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.42)
                                  : root.runningReady ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.34)
                                                      : Theme.line
                    RowLayout {
                        id: statusContent
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 11
                        Rectangle {
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 5
                            color: root.stateColor()
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: root.stateTitle()
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textBody
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.stateSummary()
                                color: Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                visible: !root.environmentIssue && !App.simulationRunning
                                Layout.fillWidth: true
                                text: root.readyCount() + " / "
                                      + (App.sensorServiceEnabled && App.sensorServiceSupported ? "4" : "3")
                                      + " 项基础检查通过"
                                color: Theme.textFaint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                            }
                        }
                        CompactIconButton {
                            iconSource: "qrc:/icons/refresh-cw.svg"
                            accessibleLabel: "重新检测运行环境"
                            toolTipText: "重新检测"
                            enabled: !App.busy
                            disabledReason: "正在执行环境操作"
                            onClicked: App.refresh()
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "本次仿真"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.DemiBold
                        }
                        CompactIconButton {
                            iconSource: root.hasPlan ? "qrc:/icons/pencil.svg" : "qrc:/icons/save.svg"
                            accessibleLabel: root.hasPlan ? "编辑当前测试方案" : "创建测试方案"
                            toolTipText: root.hasPlan ? "编辑方案" : "创建方案"
                            enabled: !App.simulationRunning && !App.busy
                            disabledReason: "停止仿真后才能编辑方案"
                            onClicked: root.planEditRequested()
                        }
                    }

                    SurfaceCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(112, runSummary.implicitHeight + 28)
                        color: Theme.surfaceRaised
                        ColumnLayout {
                            id: runSummary
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Rectangle {
                                    Layout.preferredWidth: 38
                                    Layout.preferredHeight: 38
                                    radius: Theme.radiusMedium
                                    color: Theme.accentSoft
                                    ThemedIcon {
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18
                                        source: "qrc:/icons/plane.svg"
                                        iconColor: Theme.accent
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: App.selectedModelName
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textBody
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: App.selectedWorldName
                                        color: Theme.textMuted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textTiny
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    visible: App.simulationRunning
                                    text: "运行中 " + root.formatRuntime()
                                    color: Theme.success
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textTiny
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
                            Text {
                                Layout.fillWidth: true
                                text: root.hasPlan
                                      ? "起飞 " + Number(root.currentPlan.takeoffAltitudeMeters || 5).toFixed(1)
                                        + " m · " + root.endActionLabel(String(root.currentPlan.endAction || "hold"))
                                      : "起飞 " + App.takeoffAltitude.toFixed(1) + " m · 未使用测试方案"
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                            }
                            Text {
                                visible: root.hasPlan
                                Layout.fillWidth: true
                                text: "来自方案：" + String(root.currentPlan.name || "未命名方案")
                                      + (App.selectedTestPlanModified ? " · 当前设置已修改" : "")
                                color: App.selectedTestPlanModified ? Theme.warning : Theme.textFaint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                                elide: Text.ElideRight
                            }
                        }
                    }

                    RowLayout {
                        visible: !root.hasPlan
                        Layout.fillWidth: true
                        spacing: 8
                        ActionButton {
                            Layout.fillWidth: true
                            compact: true
                            kind: "secondary"
                            text: "选择飞行器"
                            iconSource: "qrc:/icons/plane.svg"
                            enabled: !App.simulationRunning && !App.busy
                            disabledReason: "停止仿真后才能更换飞行器"
                            onClicked: modelDialog.showPicker(App.selectedModelId)
                        }
                        ActionButton {
                            Layout.fillWidth: true
                            compact: true
                            kind: "secondary"
                            text: "选择世界"
                            iconSource: "qrc:/icons/map-pinned.svg"
                            enabled: !App.simulationRunning && !App.busy
                            disabledReason: "停止仿真后才能更换世界"
                            onClicked: worldDialog.showPicker(App.selectedWorldId)
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 4
                        text: "基础服务"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        font.weight: Font.DemiBold
                    }
                    ServiceRow {
                        Layout.fillWidth: true
                        label: "Docker Desktop"
                        value: App.dockerAvailable ? "可用" : "未连接"
                        statusKind: App.dockerAvailable ? "complete" : root.environmentIssue ? "problem" : "current"
                        iconSource: App.dockerAvailable ? "qrc:/icons/circle-check.svg" : "qrc:/icons/circle-alert.svg"
                    }
                    ServiceRow {
                        Layout.fillWidth: true
                        label: "PX4 仿真镜像"
                        value: App.imageAvailable ? "已安装" : App.dockerAvailable ? "未找到" : "等待检查"
                        statusKind: App.imageAvailable ? "complete" : App.dockerAvailable ? "problem" : "pending"
                        iconSource: App.imageAvailable ? "qrc:/icons/package.svg" : "qrc:/icons/circle-alert.svg"
                    }
                    ServiceRow {
                        visible: App.sensorServiceSupported
                        Layout.fillWidth: true
                        label: "ROS 2 数据服务"
                        value: !App.sensorServiceEnabled ? "未启用"
                               : App.sensorServiceConnected && root.sensorDataReady()
                                 ? "数据正常"
                               : App.sensorServiceRunning
                                 ? (App.sensorServiceConnected ? "等待数据" : "连接中")
                               : root.sensorRuntimeIssue ? "启动失败"
                               : App.sensorServiceImageAvailable ? "随仿真启动" : "镜像未找到"
                        statusKind: !App.sensorServiceEnabled ? "pending"
                                    : App.sensorServiceConnected && root.sensorDataReady()
                                      ? "complete"
                                    : App.sensorServiceRunning ? "current"
                                    : root.sensorRuntimeIssue ? "problem"
                                    : App.sensorServiceImageAvailable ? "complete" : "problem"
                        iconSource: App.selectedSensorType === "lidar-2d"
                                    ? "qrc:/icons/radar.svg" : "qrc:/icons/camera.svg"
                    }
                    ServiceRow {
                        Layout.fillWidth: true
                        label: "MAVSDK 通信"
                        value: "UDP 14540"
                        statusKind: root.environmentReady ? "complete" : "pending"
                        iconSource: "qrc:/icons/route.svg"
                    }
                    ServiceRow {
                        Layout.fillWidth: true
                        label: "飞行器连接"
                        value: App.vehicleConnected ? "已连接" : App.simulationRunning ? "等待心跳" : "尚未连接"
                        statusKind: App.vehicleConnected ? "complete" : App.simulationRunning ? "current" : "pending"
                        iconSource: App.vehicleConnected ? "qrc:/icons/circle-check.svg" : "qrc:/icons/locate-fixed.svg"
                        dividerVisible: false
                    }
                }

                ColumnLayout {
                    visible: root.starting || root.connecting
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 4
                        text: "启动进度"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        font.weight: Font.DemiBold
                    }
                    ServiceRow { Layout.fillWidth: true; label: "检查资源"; value: "已完成"; statusKind: "complete" }
                    ServiceRow { Layout.fillWidth: true; label: "创建容器"; value: root.starting ? "进行中" : "已完成"; statusKind: root.starting ? "current" : "complete" }
                    ServiceRow { Layout.fillWidth: true; label: "启动 PX4"; value: App.simulationRunning ? "已完成" : "等待"; statusKind: App.simulationRunning ? "complete" : "pending" }
                    ServiceRow {
                        visible: App.sensorServiceEnabled && App.sensorServiceSupported
                        Layout.fillWidth: true
                        label: "启动 ROS 2"
                        value: App.sensorServiceConnected && root.sensorDataReady()
                               ? "数据正常"
                               : App.sensorServiceRunning ? "等待话题" : "等待"
                        statusKind: App.sensorServiceConnected && root.sensorDataReady()
                                    ? "complete"
                                    : App.sensorServiceRunning ? "current" : "pending"
                    }
                    ServiceRow { Layout.fillWidth: true; label: "建立连接"; value: App.vehicleConnected ? "已完成" : App.simulationRunning ? "进行中" : "等待"; statusKind: App.vehicleConnected ? "complete" : App.simulationRunning ? "current" : "pending"; dividerVisible: false }
                }

                ColumnLayout {
                    visible: App.sensorServiceSupported
                    Layout.fillWidth: true
                    spacing: 0
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        Column {
                            anchors.left: parent.left
                            anchors.right: rosServiceToggleSlot.left
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                width: parent.width
                                text: "ROS 2 传感器数据"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSmall
                                font.weight: Font.Medium
                            }
                            Text {
                                width: parent.width
                                text: App.sensorServiceEnabled
                                      ? "随本次仿真启动独立数据容器"
                                      : "关闭时只启动 PX4 仿真容器"
                                color: Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                                elide: Text.ElideRight
                            }
                        }
                        Item {
                            id: rosServiceToggleSlot
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 48
                            height: 24
                            SmallToggle {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                checked: App.sensorServiceEnabled
                                accessibleName: "启动 ROS 2 传感器数据服务"
                                enabled: !App.simulationRunning && !App.busy
                                onToggled: App.sensorServiceEnabled = !App.sensorServiceEnabled
                            }
                        }
                    }
                    Text {
                        visible: App.sensorServiceEnabled && !App.sensorServiceImageAvailable
                        Layout.fillWidth: true
                        text: "需要先双击 scripts\\build-ros-sensor-image.cmd 生成数据服务镜像。"
                        color: Theme.warning
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        wrapMode: Text.WordWrap
                    }
                    Rectangle {
                        visible: App.sensorServiceEnabled
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        radius: Theme.radiusMedium
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: Theme.line
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text { text: "数据策略"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                                Text { text: root.sensorUsageLabel(); color: App.sensorSessionRecording ? Theme.accent : Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                            }
                            Text {
                                text: root.sensorPerformanceLabel()
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }
                }

                ColumnLayout {
                    visible: !App.simulationRunning
                    Layout.fillWidth: true
                    spacing: 0
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        Column {
                            anchors.left: parent.left
                            anchors.right: containerNameToggleSlot.left
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text { width: parent.width; text: "容器名称"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                            Text { width: parent.width; text: root.useCustomContainerName ? "使用自定义临时名称" : "退出仿真后自动删除"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideRight }
                        }
                        Item {
                            id: containerNameToggleSlot
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 48
                            height: 24
                            SmallToggle {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                checked: root.useCustomContainerName
                                accessibleName: "自定义容器名称"
                                enabled: !App.simulationRunning && !App.busy
                                onToggled: {
                                    root.useCustomContainerName = !root.useCustomContainerName
                                    if (!root.useCustomContainerName)
                                        App.containerName = "flightbench-px4"
                                    else
                                        containerNameField.forceActiveFocus(Qt.TabFocusReason)
                                }
                            }
                        }
                    }
                    TextField {
                        id: containerNameField
                        visible: root.useCustomContainerName
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        text: App.containerName
                        enabled: !App.simulationRunning && !App.busy
                        maximumLength: 63
                        selectByMouse: true
                        color: Theme.textPrimary
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.textSmall
                        leftPadding: 12
                        rightPadding: 12
                        validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9][A-Za-z0-9_.-]{0,62}$/ }
                        onEditingFinished: {
                            if (acceptableInput) App.containerName = text
                            else text = App.containerName
                        }
                        background: Rectangle {
                            radius: Theme.radiusMedium
                            color: Theme.surfaceRaised
                            border.width: 1
                        border.color: containerNameField.activeFocus ? Theme.accent
                                          : containerNameField.acceptableInput && App.containerNameAvailable
                                            ? Theme.line : Theme.warning
                        }
                    }
                    Text {
                        visible: root.useCustomContainerName
                                 && (!containerNameField.acceptableInput || !App.containerNameAvailable)
                        Layout.fillWidth: true
                        Layout.topMargin: 5
                        text: !containerNameField.acceptableInput
                              ? "名称需以字母或数字开头，只能包含字母、数字、点、短横线和下划线。"
                              : "这个名称已被现有容器占用，请更换名称或在 Docker Desktop 中删除旧容器。"
                        color: Theme.warning
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        wrapMode: Text.WordWrap
                    }
                }

                DisclosureRow {
                    Layout.fillWidth: true
                    label: "高级信息"
                    expanded: root.advancedOpen
                    topDivider: true
                    onActivated: root.advancedOpen = !root.advancedOpen
                }
                ColumnLayout {
                    visible: root.advancedOpen
                    Layout.fillWidth: true
                    spacing: 0
                    ValueLine { Layout.fillWidth: true; label: "镜像"; value: "px4io/px4-sitl-gazebo:latest" }
                    ValueLine { Layout.fillWidth: true; label: "飞行器 ID"; value: App.selectedModelId }
                    ValueLine { Layout.fillWidth: true; label: "世界 ID"; value: App.selectedWorldId }
                    ValueLine { Layout.fillWidth: true; label: "容器"; value: App.containerName }
                    ValueLine { Layout.fillWidth: true; label: "容器网络"; value: App.dockerNetworkAvailable ? "flightbench-net" : "未就绪" }
                    ValueLine { Layout.fillWidth: true; label: "MAVLink"; value: "UDP 14540" }
                    ValueLine { Layout.fillWidth: true; label: "图形输出"; value: "无界面（HEADLESS）" }
                    ValueLine {
                        Layout.fillWidth: true
                        label: "载荷服务"
                        value: !App.sensorServiceSupported ? "本批次未接入"
                               : App.sensorServiceEnabled ? "用户已选择启动 ROS 2" : "用户选择不启动"
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        text: root.modelPayload(App.selectedModelId) === "标准四旋翼"
                              ? "当前飞行器不需要额外的传感器数据服务。"
                              : App.sensorServiceSupported
                                ? "当前飞行器搭载" + root.modelPayload(App.selectedModelId)
                                  + "；是否启动 ROS 2 数据容器完全由用户选择。"
                                : "当前飞行器搭载" + root.modelPayload(App.selectedModelId)
                                  + "；这一载荷尚未接入实时数据。"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        spacing: 8
                        ActionButton {
                            Layout.fillWidth: true
                            compact: true
                            kind: "secondary"
                            text: "复制信息"
                            iconSource: "qrc:/icons/copy.svg"
                            onClicked: App.copyEnvironmentInformation()
                        }
                        ActionButton {
                            Layout.fillWidth: true
                            compact: true
                            kind: "secondary"
                            text: "导出诊断"
                            iconSource: "qrc:/icons/file-chart-column.svg"
                            onClicked: App.exportDiagnosticReport()
                        }
                    }
                    ActionButton {
                        visible: !root.hasPlan
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        compact: true
                        kind: "secondary"
                        text: "恢复环境默认值"
                        iconSource: "qrc:/icons/rotate-ccw.svg"
                        enabled: !App.simulationRunning && !App.busy
                        disabledReason: "停止仿真后才能恢复环境默认值"
                        onClicked: {
                            App.selectedModelId = "gz_x500"
                            App.selectedWorldId = "default"
                            App.containerName = "flightbench-px4"
                            App.sensorServiceEnabled = false
                            root.useCustomContainerName = false
                        }
                    }
                }

                Text {
                    visible: root.environmentIssue
                    Layout.fillWidth: true
                    text: "处理建议：" + (App.recoveryLabel || "重新检测运行环境")
                    color: Theme.warning
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: App.simulationRunning ? 64 : 62
            color: Theme.surface
            border.width: 0
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: Theme.line }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 14
                anchors.topMargin: 11
                anchors.bottomMargin: 11
                spacing: 8
                ActionButton {
                    visible: App.simulationRunning && App.vehicleConnected
                    Layout.fillWidth: true
                    compact: true
                    kind: "secondary"
                    text: "打开飞行页"
                    iconSource: "qrc:/icons/plane.svg"
                    onClicked: root.flightRequested()
                }
                ActionButton {
                    visible: App.simulationRunning
                    Layout.fillWidth: true
                    compact: true
                    kind: "secondary"
                    text: App.landingAndStopping ? "正在降落…"
                          : (App.inAir || App.armed) ? "降落并停止" : "停止仿真"
                    iconSource: (App.inAir || App.armed) ? "qrc:/icons/plane-landing.svg" : "qrc:/icons/square.svg"
                    enabled: !App.busy && !App.landingAndStopping
                    disabledReason: App.landingAndStopping ? "正在等待飞行器安全落地" : "正在执行另一项操作"
                    onClicked: {
                        if (App.inAir || App.armed) stopConfirmDialog.open()
                        else App.stopSimulation()
                    }
                }
                ActionButton {
                    visible: !App.simulationRunning && !root.environmentIssue
                    Layout.fillWidth: true
                    text: root.starting ? "正在启动…" : "启动仿真"
                    iconSource: "qrc:/icons/play.svg"
                    enabled: root.environmentReady && root.planComplete && !App.busy
                             && !App.replayActive && containerNameField.acceptableInput
                             && App.containerNameAvailable
                    disabledReason: root.startDisabledReason()
                    onClicked: App.startSimulation()
                }
                ActionButton {
                    visible: !App.simulationRunning && root.environmentIssue
                    Layout.fillWidth: true
                    text: App.recoveryLabel || "重新检测"
                    iconSource: App.recoveryAction === "environment"
                                ? "qrc:/icons/pencil.svg" : "qrc:/icons/refresh-cw.svg"
                    kind: "secondary"
                    enabled: !App.busy
                    disabledReason: "正在执行环境操作"
                    onClicked: {
                        if (App.recoveryAction === "environment") {
                            root.useCustomContainerName = true
                            containerNameField.forceActiveFocus(Qt.TabFocusReason)
                        } else {
                            App.recover()
                        }
                    }
                }
            }
        }
    }

    SimulationModelPicker {
        id: modelDialog
        onModelPicked: modelId => App.selectedModelId = String(modelId || "")
    }

    SimulationWorldPicker {
        id: worldDialog
        onWorldPicked: worldId => App.selectedWorldId = String(worldId || "")
    }

    Dialog {
        id: stopConfirmDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(390, Overlay.overlay ? Overlay.overlay.width - 40 : 390)
        modal: true
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: Theme.radiusLarge
            color: Theme.surface
            border.width: 1
            border.color: Theme.lineStrong
        }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: "降落并停止仿真？"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSubtitle
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: "飞行器仍处于解锁或飞行状态。FlightBench 会先执行降落，确认落地并锁定后再停止容器。"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                ActionButton { compact: true; kind: "secondary"; text: "取消"; onClicked: stopConfirmDialog.close() }
                ActionButton {
                    compact: true
                    text: "确认降落"
                    iconSource: "qrc:/icons/plane-landing.svg"
                    onClicked: {
                        stopConfirmDialog.close()
                        App.landAndStopSimulation()
                    }
                }
            }
        }
    }
}
