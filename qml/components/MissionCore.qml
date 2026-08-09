pragma ComponentBehavior: Bound
import QtQuick
import FlightBench

Item {
    id: root
    property bool dockerReady: false
    property bool simulationRunning: false
    property bool vehicleConnected: false
    property bool vehicleReady: false
    property bool busy: false
    readonly property int phase: vehicleReady ? 4 : vehicleConnected ? 3 : simulationRunning ? 2 : dockerReady ? 1 : 0
    readonly property color stateColor: vehicleReady ? Theme.success : simulationRunning ? Theme.accentHover : dockerReady ? Theme.accent : Theme.textFaint
    readonly property string stateText: busy ? "正在准备环境" : vehicleReady ? "飞行器已就绪" : vehicleConnected ? "正在完成健康检查" : simulationRunning ? "等待 PX4 链路" : dockerReady ? "可以启动仿真" : "等待 Docker Desktop"
    readonly property string actionText: busy ? "请稍候" : simulationRunning ? "停止会话" : "启动会话"
    signal triggered()

    implicitWidth: 206
    implicitHeight: 206

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 0.98
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: root.stateColor
        opacity: root.simulationRunning ? 0.18 : 0.08
        SequentialAnimation on scale {
            running: root.simulationRunning && !root.busy
            loops: Animation.Infinite
            NumberAnimation { from: 0.94; to: 1.03; duration: 1250; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.03; to: 0.94; duration: 1250; easing.type: Easing.InOutSine }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 0.86
        height: width
        radius: width / 2
        color: Theme.canvasSoft
        border.width: 1
        border.color: Theme.lineStrong
    }

    Item {
        id: orbit
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 0.80
        height: width

        Repeater {
            model: 16
            Rectangle {
                required property int index
                readonly property int segment: Math.floor(index / 4)
                width: index % 4 === 0 ? 17 : 7
                height: index % 4 === 0 ? 3 : 2
                radius: 2
                color: segment < root.phase ? root.stateColor : Theme.lineStrong
                opacity: segment < root.phase ? (index % 4 === 0 ? 0.95 : 0.52) : 0.55
                x: orbit.width / 2 + Math.cos(index * Math.PI / 8) * orbit.width * 0.47 - width / 2
                y: orbit.height / 2 + Math.sin(index * Math.PI / 8) * orbit.height * 0.47 - height / 2
                rotation: index * 22.5
            }
        }

        RotationAnimator on rotation {
            running: root.busy
            loops: Animation.Infinite
            from: 0
            to: 360
            duration: 1400
        }
    }

    Rectangle {
        id: core
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 0.58
        height: width
        radius: width / 2
        color: !root.dockerReady ? Theme.surfaceRaised : root.busy ? Theme.surfaceInteractive : root.simulationRunning ? Theme.accentSoft : Theme.accent
        border.width: 2
        border.color: root.stateColor
        scale: coreMouse.pressed ? 0.96 : coreMouse.containsMouse ? 1.025 : 1.0
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 180 } }

        Column {
            anchors.centerIn: parent
            spacing: 7
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 32
                height: 32
                source: root.simulationRunning ? "qrc:/icons/square.svg" : "qrc:/icons/play.svg"
                sourceSize: Qt.size(32, 32)
                opacity: !root.dockerReady || root.busy ? 0.38 : 1.0
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.dockerReady ? root.actionText : "不可启动"
                color: !root.dockerReady || root.busy ? Theme.textMuted : Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                font.weight: Font.DemiBold
            }
        }

        MouseArea {
            id: coreMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: root.dockerReady && !root.busy
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: root.triggered()
        }
    }
}
