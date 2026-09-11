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
    implicitHeight: 68

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 22
        anchors.rightMargin: 20
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            radius: 9
            color: Theme.accent
            Image {
                anchors.centerIn: parent
                width: 22
                height: 22
                source: "qrc:/icons/flightbench-app.svg"
            }
        }

        ColumnLayout {
            spacing: 1
            Text {
                text: "FlightBench"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                text: "PX4 本地仿真"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
        }

        Item { Layout.fillWidth: true }

        Button {
            id: environmentButton
            implicitWidth: environmentRow.implicitWidth + 24
            implicitHeight: 34
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.name: App.dockerAvailable ? "Docker 可用，打开仿真环境" : "检查 Docker 环境"
            Accessible.role: Accessible.Button
            onClicked: root.environmentRequested()
            background: Rectangle {
                radius: Theme.radiusMedium
                color: environmentButton.hovered ? Theme.surfaceRaised : Theme.surface
                border.width: 1
                border.color: environmentButton.visualFocus ? Theme.accent : Theme.line
            }
            contentItem: RowLayout {
                id: environmentRow
                spacing: 8
                Rectangle {
                    Layout.preferredWidth: 7
                    Layout.preferredHeight: 7
                    radius: 4
                    color: App.dockerAvailable ? Theme.success : Theme.textFaint
                }
                Text {
                    text: App.dockerAvailable ? "Docker 可用" : "检查 Docker"
                    color: Theme.textSecondary
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
            onClicked: root.refreshRequested()
        }

        CompactIconButton {
            implicitWidth: 36
            implicitHeight: 36
            iconSource: "qrc:/icons/sliders-horizontal.svg"
            accessibleLabel: "打开设置"
            toolTipText: "设置"
            onClicked: root.settingsRequested()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.line
    }
}
