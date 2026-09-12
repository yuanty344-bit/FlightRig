pragma ComponentBehavior: Bound
import QtQuick
import "../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Rectangle {
    id: root
    z: 10
    signal environmentRequested()
    signal settingsRequested()
    signal refreshRequested()

    color: Theme.canvasSoft
    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0; color: Theme.heroEnd }
        GradientStop { position: 0.58; color: Theme.heroStart }
        GradientStop { position: 1; color: "#1D3C5D" }
    }
    implicitHeight: 68

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 22
        anchors.rightMargin: 20
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            radius: 10
            color: "transparent"
            Image {
                anchors.centerIn: parent
                width: 34
                height: 34
                source: "qrc:/icons/flightbench-app.svg"
            }
        }

        ColumnLayout {
            spacing: 1
            Text {
                text: "FlightRig"
                color: Theme.heroText
                font.family: Theme.fontFamily
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                text: "PX4 本地仿真"
                color: Theme.heroMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
        }

        Item { Layout.fillWidth: true }

        Button {
            id: environmentButton
            readonly property bool waiting: App.busy
                                             || (App.simulationRunning && !App.vehicleConnected)
            readonly property bool environmentReady: App.dockerAvailable
                                                        && App.imageAvailable
                                                        && App.dockerNetworkAvailable
            readonly property string statusText: App.vehicleConnected ? "飞行器已连接"
                                                 : App.simulationRunning ? "正在连接飞行器"
                                                 : App.busy ? "正在检查环境"
                                                 : environmentReady ? "环境已就绪"
                                                                    : "环境需要处理"
            implicitWidth: environmentRow.implicitWidth + 24
            implicitHeight: 34
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.name: statusText + "，打开仿真环境"
            Accessible.role: Accessible.Button
            onClicked: root.environmentRequested()
            background: Rectangle {
                radius: Theme.radiusLarge
                color: environmentButton.hovered ? Qt.rgba(1, 1, 1, 0.12)
                                                 : Qt.rgba(1, 1, 1, 0.075)
                border.width: 1
                border.color: environmentButton.visualFocus ? "#79ADFF" : Theme.heroLine
                Behavior on color { ColorAnimation { duration: Theme.motionFast } }
            }
            contentItem: RowLayout {
                id: environmentRow
                spacing: 8
                Item {
                    Layout.preferredWidth: 10
                    Layout.preferredHeight: 10
                    Rectangle {
                        anchors.centerIn: parent
                        width: 10
                        height: 10
                        radius: 5
                        color: environmentButton.waiting ? Theme.accentSoft : "transparent"
                        opacity: 0.8
                        SequentialAnimation on scale {
                            running: environmentButton.waiting && !Theme.reducedMotion
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.65; to: 1.35; duration: 700; easing.type: Easing.OutCubic }
                            NumberAnimation { from: 1.35; to: 0.65; duration: 700; easing.type: Easing.InCubic }
                        }
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 7
                        height: 7
                        radius: 4
                        color: App.vehicleConnected ? Theme.success
                               : environmentButton.waiting ? Theme.accent
                               : environmentButton.environmentReady ? Theme.success : Theme.warning
                    }
                }
                Text {
                    text: environmentButton.statusText
                    color: Theme.heroText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                }
            }
        }

        CompactIconButton {
            implicitWidth: 36
            implicitHeight: 36
            iconSource: "qrc:/icons/refresh-cw.svg"
            accessibleLabel: "重新检测"
            toolTipText: "重新检测"
            enabled: !App.busy
            onDarkSurface: true
            onClicked: root.refreshRequested()
        }

        CompactIconButton {
            implicitWidth: 36
            implicitHeight: 36
            iconSource: "qrc:/icons/settings.svg"
            accessibleLabel: "打开设置"
            toolTipText: "设置"
            onDarkSurface: true
            onClicked: root.settingsRequested()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.heroLine
    }
}
