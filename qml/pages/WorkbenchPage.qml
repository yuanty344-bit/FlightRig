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

    readonly property var recentRoute: App.savedRoutes.length > 0 ? App.savedRoutes[0] : ({})
    readonly property bool environmentReady: App.dockerAvailable && App.imageAvailable
    readonly property bool compactLayout: width < 820 || height < 470

    function launchTitle() {
        if (App.busy) return "正在处理"
        if (App.vehicleConnected) return "仿真已连接"
        if (App.simulationRunning) return "仿真正在启动"
        return "仿真尚未启动"
    }

    function launchDescription() {
        if (App.hasIssue) return App.issueMessage
        if (App.vehicleReady) return "飞行器已就绪，可以进入飞行"
        if (App.vehicleConnected) return "正在完成起飞前健康检查"
        if (App.simulationRunning) return "正在等待 PX4 与 MAVSDK 建立连接"
        return root.environmentReady ? "环境已就绪，可以直接开始" : "正在检查本机仿真环境"
    }

    function primaryText() {
        if (App.hasIssue) return App.recoveryLabel
        if (App.vehicleReady) return "进入飞行"
        if (App.simulationRunning) return "停止仿真"
        return "启动仿真"
    }

    function primaryHint() {
        if (App.busy) return "请等待当前操作完成"
        if (App.hasIssue) return App.issueMessage
        if (App.vehicleReady) return "进入飞行页面"
        if (App.simulationRunning) return "停止当前本地仿真"
        if (!App.dockerAvailable) return "请先启动 Docker Desktop"
        if (!App.imageAvailable) return "未找到 PX4 SITL 镜像"
        return "启动 " + App.selectedModelName + " 本地仿真"
    }

    function runPrimaryAction() {
        if (App.hasIssue && App.recoveryAction === "health") root.openHealth()
        else if (App.hasIssue) App.recover()
        else if (App.vehicleReady) root.openFlight()
        else if (App.simulationRunning) App.stopSimulation()
        else App.startSimulation()
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
                text: "准备"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: 24
                font.weight: Font.Medium
            }
            Text {
                text: "配置环境，然后开始一次仿真"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compactLayout ? 236 : 260
            spacing: root.compactLayout ? 12 : 16

            SurfaceCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 430

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
                                source: App.simulationRunning ? "qrc:/icons/square.svg" : "qrc:/icons/play.svg"
                                iconColor: Theme.accent
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
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

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.compactLayout ? 62 : 72
                        spacing: 0

                        component ConfigCell: Item {
                            id: configCell
                            property string label: "项目"
                            property string value: "—"
                            property bool divider: false
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: configCell.divider ? 16 : 0
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6
                                Text { text: configCell.label; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                                Text { text: configCell.value; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                            }
                            Rectangle { visible: configCell.divider; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: Theme.line }
                        }

                        ConfigCell { label: "飞行器"; value: App.selectedModelName }
                        ConfigCell { label: "场景"; value: App.selectedWorldName; divider: true }
                        ConfigCell { label: "连接"; value: "UDP 14540"; divider: true }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
                    Item { Layout.preferredHeight: root.compactLayout ? 10 : 18 }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        ActionButton {
                            Layout.preferredWidth: 116
                            text: root.primaryText()
                            iconSource: App.simulationRunning && !App.vehicleReady
                                        ? "qrc:/icons/square.svg" : "qrc:/icons/play.svg"
                            kind: App.hasIssue ? "secondary" : "primary"
                            toolTipText: root.primaryHint()
                            enabled: App.hasIssue ? !App.busy
                                                  : App.vehicleReady
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
                Layout.preferredWidth: 266
                Layout.fillHeight: true

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
                            text: "启动条件"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.Medium
                        }
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
                    }
                    StatusRow { Layout.fillWidth: true; label: "Docker Desktop"; value: App.dockerAvailable ? "可用" : "未连接"; complete: App.dockerAvailable }
                    StatusRow { Layout.fillWidth: true; label: "PX4 镜像"; value: App.imageAvailable ? "已安装" : "未找到"; complete: App.imageAvailable }
                    StatusRow { Layout.fillWidth: true; label: "监听地址"; value: "UDP 14540"; complete: true; dividerVisible: false }
                    Item { Layout.fillHeight: true }
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
                    source: "qrc:/icons/route.svg"
                    iconColor: App.savedRoutes.length > 0 ? Theme.accent : Theme.textFaint
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: App.savedRoutes.length > 0 ? "继续最近航线" : "还没有保存的航线"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        font.weight: Font.Medium
                    }
                    Text {
                        text: App.savedRoutes.length > 0
                              ? String(root.recentRoute.name || "未命名航线") + " · "
                                + Number(root.recentRoute.waypointCount || 0) + " 个航点 · "
                                + Number(root.recentRoute.distanceMeters || 0) + " m"
                              : "在飞行页规划航点后即可保存和复用"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                }
                CompactIconButton {
                    iconSource: "qrc:/icons/folder-open.svg"
                    accessibleLabel: "打开航线库"
                    toolTipText: "航线库"
                    onClicked: root.openMission(true)
                }
                ActionButton {
                    Layout.preferredWidth: 72
                    compact: true
                    text: App.savedRoutes.length > 0 ? "载入" : "规划"
                    kind: "secondary"
                    onClicked: {
                        if (App.savedRoutes.length > 0)
                            root.loadRoute(String(root.recentRoute.id || ""))
                        else
                            root.openMission(false)
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
