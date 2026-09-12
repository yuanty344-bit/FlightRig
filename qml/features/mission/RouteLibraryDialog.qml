import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench
import "../../controls"

MotionDialog {
    id: root
    required property Item host
    property var routes: []
    property string selectedRouteId: ""
    property string operationMessage: ""
    property bool editingLocked: false
    required property var durationFormatter
    signal routeSelected(string routeId, string routeName)
    signal loadRequested(string routeId)
    signal folderRequested()
    signal newRequested()
    signal deleteRequested()
    signal duplicateRequested()
    signal renameRequested()
    parent: host
    anchors.centerIn: parent
    width: Math.min(520, root.host.width - 40)
    height: Math.min(root.routes.length === 0 ? 320 : 440, root.host.height - 40)
    modal: true
    padding: 16
    background: DialogSurface {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.lineStrong
    }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "航线库"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTitle
                font.weight: Font.Medium
            }
            Item { Layout.fillWidth: true }
            CompactIconButton {
                iconSource: "qrc:/icons/folder-open.svg"
                accessibleLabel: "打开航线保存目录"
                toolTipText: "打开保存目录"
                onClicked: root.folderRequested()
            }
            PanelCloseButton {
                iconSource: "qrc:/icons/x.svg"
                accessibleLabel: "关闭航线库"
                toolTipText: "关闭"
                onClicked: root.close()
            }
        }

        Text {
            Layout.fillWidth: true
            text: "加载时会以无人机当前位置为新起点，保持航线形状不变。"
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusMedium
            color: Theme.surface
            border.width: 1
            border.color: Theme.line

            Text {
                anchors.centerIn: parent
                visible: root.routes.length === 0
                text: "还没有保存的航线"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
            }

            ListView {
                id: routeLibraryList
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                spacing: 4
                model: root.routes
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: InsetScrollBar {
                    id: routeLibraryScrollBar
                    objectName: "routeLibraryScrollBar"
                    policy: routeLibraryList.contentHeight > routeLibraryList.height + 1
                            ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }
                delegate: Rectangle {
                    id: routeLibraryRow
                    required property var modelData
                    width: Math.max(0, routeLibraryList.width
                                         - (routeLibraryScrollBar.visible
                                            ? Theme.scrollBarExtent : 0))
                    height: 62
                    radius: Theme.radiusSmall
                    color: root.selectedRouteId === modelData.id
                           ? Theme.accentSoft
                           : routeRowMouse.containsMouse ? Theme.surfaceRaised
                                                        : "transparent"
                    border.width: activeFocus ? 1 : 0
                    border.color: Theme.accent
                    activeFocusOnTab: true
                    Accessible.name: modelData.name + "，" + modelData.waypointCount + " 个航点"
                    Accessible.role: Accessible.Button
                    Keys.onSpacePressed: selectRoute()
                    Keys.onReturnPressed: root.loadRequested(modelData.id)
                    Keys.onEnterPressed: root.loadRequested(modelData.id)

                    function selectRoute() {
                        root.routeSelected(modelData.id, modelData.name)
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2
                        height: 34
                        radius: 1
                        visible: root.selectedRouteId === modelData.id
                        color: Theme.accent
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSmall
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.waypointCount + " 个航点  ·  "
                                      + modelData.distanceMeters + " m  ·  约 "
                                      + root.durationFormatter(modelData.estimatedSeconds)
                                color: Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                            }
                        }
                        Text {
                            text: modelData.updatedLabel
                            color: Theme.textFaint
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.textTiny
                        }
                    }
                    MouseArea {
                        id: routeRowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            parent.selectRoute()
                            root.contentItem.forceActiveFocus(Qt.MouseFocusReason)
                        }
                        onDoubleClicked: root.loadRequested(modelData.id)
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.textTiny + 4
            text: root.operationMessage
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                Layout.preferredWidth: 76
                compact: true
                text: "新建"
                kind: "secondary"
                enabled: !root.editingLocked
                onClicked: root.newRequested()
            }
            CompactIconButton {
                iconSource: "qrc:/icons/trash-2.svg"
                accessibleLabel: "删除所选航线"
                toolTipText: "删除航线"
                danger: true
                enabled: root.selectedRouteId.length > 0 && !root.editingLocked
                onClicked: root.deleteRequested()
            }
            CompactIconButton {
                iconSource: "qrc:/icons/copy.svg"
                accessibleLabel: "复制所选航线"
                toolTipText: "复制航线"
                enabled: root.selectedRouteId.length > 0 && !root.editingLocked
                onClicked: root.duplicateRequested()
            }
            CompactIconButton {
                iconSource: "qrc:/icons/pencil.svg"
                accessibleLabel: "重命名所选航线"
                toolTipText: "重命名"
                enabled: root.selectedRouteId.length > 0 && !root.editingLocked
                onClicked: root.renameRequested()
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                Layout.preferredWidth: 84
                compact: true
                text: "加载"
                enabled: root.selectedRouteId.length > 0 && !root.editingLocked
                onClicked: root.loadRequested(root.selectedRouteId)
            }
        }
    }
}
