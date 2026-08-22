pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Dialog {
    id: root

    property string selectedModelId: ""
    signal modelPicked(string modelId)

    function showPicker(modelId) {
        selectedModelId = String(modelId || "")
        open()
    }

    function payloadLabel(modelId) {
        if (modelId.indexOf("depth") >= 0) return "深度相机"
        if (modelId.indexOf("vision") >= 0) return "单目相机"
        if (modelId.indexOf("lidar") >= 0) return "二维激光雷达"
        if (modelId.indexOf("gimbal") >= 0) return "云台相机"
        return "标准四旋翼"
    }

    anchors.centerIn: Overlay.overlay
    width: Math.min(500, Overlay.overlay ? Overlay.overlay.width - 40 : 500)
    height: Math.min(520, Overlay.overlay ? Overlay.overlay.height - 40 : 520)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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
            Layout.preferredHeight: 76
            ColumnLayout {
                anchors.left: parent.left
                anchors.right: closeButton.left
                anchors.leftMargin: 20
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: "选择飞行器"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTitle
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "选择与测试载荷相符的 PX4 多旋翼机型"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                }
            }
            CompactIconButton {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.top: parent.top
                anchors.topMargin: 12
                iconSource: "qrc:/icons/x.svg"
                accessibleLabel: "关闭飞行器选择"
                toolTipText: "关闭"
                onClicked: root.close()
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.line
            }
        }

        ScrollView {
            id: modelScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            leftPadding: 16
            rightPadding: 12
            topPadding: 12
            bottomPadding: 12
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: modelScroll.availableWidth
                spacing: 7
                Repeater {
                    model: App.simulationModels
                    delegate: Rectangle {
                        id: option
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 70
                        radius: Theme.radiusMedium
                        color: mouse.containsMouse ? Theme.surfaceInteractive : Theme.surfaceRaised
                        border.width: String(modelData.id) === root.selectedModelId || activeFocus ? 1 : 0
                        border.color: activeFocus ? Theme.accent : Theme.lineStrong
                        activeFocusOnTab: true
                        Accessible.name: String(modelData.name)
                        Accessible.role: Accessible.Button

                        function choose() {
                            root.selectedModelId = String(modelData.id)
                            root.modelPicked(root.selectedModelId)
                            root.close()
                        }

                        Keys.onReturnPressed: choose()
                        Keys.onEnterPressed: choose()
                        Keys.onSpacePressed: choose()

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 13
                            spacing: 11
                            Rectangle {
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                                radius: 9
                                color: "transparent"
                                border.width: 1
                                border.color: String(option.modelData.id) === root.selectedModelId
                                              ? Theme.accent : Theme.lineStrong
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: Theme.accent
                                    visible: String(option.modelData.id) === root.selectedModelId
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: String(option.modelData.name)
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textBody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        text: root.payloadLabel(String(option.modelData.id))
                                        color: Theme.accent
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textTiny
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: String(option.modelData.description)
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textTiny
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: option.choose()
                        }
                    }
                }
            }
        }
    }
}
