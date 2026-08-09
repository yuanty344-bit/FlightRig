pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import FlightBench
import "../components"

Item {
    id: root
    signal openEnvironment()
    signal openFlight()
    signal openHealth()

    readonly property int phase: App.vehicleReady ? 4 : App.vehicleConnected ? 3 : App.simulationRunning ? 2 : App.dockerAvailable ? 1 : 0

    function stateFor(stage) {
        if (stage === 1) return App.dockerAvailable ? "可用" : "未连接"
        if (stage === 2) return App.simulationRunning ? "运行中" : "未启动"
        if (stage === 3) return App.vehicleConnected ? "已连接" : "等待连接"
        return App.vehicleReady ? "可以飞行" : "等待检查"
    }

    component StageRow: Item {
        id: stageRow
        property int stage: 1
        property string label: "阶段"
        property string detail: ""
        readonly property bool complete: root.phase >= stage
        Layout.fillWidth: true
        Layout.preferredHeight: 58

        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
        RowLayout {
            anchors.fill: parent
            spacing: 14
            Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: stageRow.complete ? Theme.success : Theme.textFaint }
            Text { Layout.preferredWidth: 126; text: stageRow.label; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
            Text { Layout.fillWidth: true; text: stageRow.detail; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; elide: Text.ElideMiddle }
            Text { text: root.stateFor(stageRow.stage); color: stageRow.complete ? Theme.success : Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
        }
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
                Text { text: "工作台"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 22; font.weight: Font.Medium }
                Text { text: "启动并连接一套本地 PX4 仿真环境"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
            }
            Item { Layout.fillWidth: true }
            IconLink {
                text: "环境详情"
                iconSource: "qrc:/icons/panel-right-open.svg"
                onClicked: root.openEnvironment()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.top: parent.top
                anchors.topMargin: 54
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(650, parent.width)
                spacing: 0

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
                StageRow { stage: 1; label: "Docker"; detail: "Desktop Engine" }
                StageRow { stage: 2; label: "PX4 SITL"; detail: App.imageAvailable ? "px4io/px4-sitl-gazebo · " + App.selectedModelName : "等待本机镜像检查" }
                StageRow { stage: 3; label: "MAVSDK"; detail: "监听 UDP 14540" }
                StageRow { stage: 4; label: "起飞准备"; detail: "连接后自动执行健康检查" }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 24
                    spacing: 10
                    Text { Layout.fillWidth: true; text: App.hasIssue ? App.issueMessage : App.statusMessage; color: App.hasIssue ? Theme.warning : Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; elide: Text.ElideRight }
                    ActionButton {
                        Layout.preferredWidth: App.hasIssue ? 118 : App.vehicleReady ? 118 : 128
                        text: App.hasIssue ? App.recoveryLabel : App.vehicleReady ? "进入飞行" : App.simulationRunning ? "停止仿真" : "启动仿真"
                        iconSource: App.hasIssue ? (App.recoveryAction === "health" ? "qrc:/icons/heart-pulse.svg" : "qrc:/icons/refresh-cw.svg") : App.vehicleReady ? "qrc:/icons/gauge.svg" : App.simulationRunning ? "qrc:/icons/square.svg" : "qrc:/icons/play.svg"
                        kind: App.hasIssue || (App.simulationRunning && !App.vehicleReady) ? "secondary" : "primary"
                        enabled: App.hasIssue ? !App.busy : App.vehicleReady || (App.dockerAvailable && App.imageAvailable && !App.busy)
                        onClicked: {
                            if (App.hasIssue && App.recoveryAction === "health")
                                root.openHealth()
                            else if (App.hasIssue)
                                App.recover()
                            else if (App.vehicleReady)
                                root.openFlight()
                            else if (App.simulationRunning)
                                App.stopSimulation()
                            else
                                App.startSimulation()
                        }
                    }
                }
            }
        }
    }
}
