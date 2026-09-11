pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Dialog {
    id: root

    property string selectedWorldId: ""
    property string worldFilter: "all"
    readonly property var availableWorlds: App.simulationWorlds
    signal worldPicked(string worldId)

    function showPicker(worldId) {
        selectedWorldId = String(worldId || "")
        worldFilter = "all"
        open()
    }

    function typeLabel(type, worldId) {
        if (worldId === "default") return "简单场地"
        if (type === "campus") return "校园"
        if (type === "park") return "公园"
        if (type === "urban") return "城市"
        return "其他"
    }

    function filteredWorlds() {
        const values = []
        for (let i = 0; i < root.availableWorlds.length; ++i) {
            const world = root.availableWorlds[i]
            if (worldFilter === "all" || String(world.sceneType || "other") === worldFilter)
                values.push(world)
        }
        return values
    }

    function capabilityText(world) {
        if (String(world.id) === "default") return "开阔地面 · 世界原点起飞"
        let parts = []
        if (Boolean(world.hasMap)) parts.push("二维地图")
        if (Boolean(world.hasPreview)) parts.push("三维白模")
        const width = Number(world.widthMeters || 0)
        const height = Number(world.heightMeters || 0)
        if (width > 0 && height > 0)
            parts.push(width.toFixed(0) + " × " + height.toFixed(0) + " m")
        const clearance = Number(world.recommendedTakeoffClearanceMeters || 0)
        if (clearance > 0) parts.push("起点净空 " + clearance.toFixed(0) + " m")
        return parts.join(" · ")
    }

    anchors.centerIn: Overlay.overlay
    width: Math.min(610, Overlay.overlay ? Overlay.overlay.width - 40 : 610)
    height: Math.min(650, Overlay.overlay ? Overlay.overlay.height - 40 : 650)
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
                    text: "选择仿真世界"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTitle
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "地图和三维白模会同时用于规划与回放"
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
                accessibleLabel: "关闭世界选择"
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

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 10
            Layout.bottomMargin: 4
            columns: 5
            uniformCellWidths: true
            columnSpacing: 5
            rowSpacing: 5
            Repeater {
                model: [
                    { id: "all", label: "全部" },
                    { id: "simple", label: "简单场地" },
                    { id: "campus", label: "校园" },
                    { id: "urban", label: "城市" },
                    { id: "park", label: "公园" }
                ]
                delegate: Button {
                    id: filterButton
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 30
                    padding: 0
                    text: String(modelData.label)
                    hoverEnabled: true
                    onClicked: root.worldFilter = String(modelData.id)
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: root.worldFilter === String(filterButton.modelData.id)
                               ? Theme.accentSoft
                               : filterButton.hovered ? Theme.surfaceInteractive : "transparent"
                        border.width: root.worldFilter === String(filterButton.modelData.id)
                                      || filterButton.visualFocus ? 1 : 0
                        border.color: filterButton.visualFocus ? Theme.accent : Theme.lineStrong
                    }
                    contentItem: Text {
                        text: filterButton.text
                        color: root.worldFilter === String(filterButton.modelData.id)
                               ? Theme.accent : Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        ScrollView {
            id: worldScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            leftPadding: 16
            rightPadding: 12
            topPadding: 8
            bottomPadding: 12
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            GridLayout {
                id: worldGrid
                width: worldScroll.availableWidth
                columns: width >= 500 ? 2 : 1
                columnSpacing: 9
                rowSpacing: 9
                Repeater {
                    model: root.filteredWorlds()
                    delegate: Rectangle {
                        id: option
                        required property var modelData
                        Layout.fillWidth: false
                        Layout.minimumWidth: worldGrid.columns === 2
                                             ? (worldGrid.width - worldGrid.columnSpacing) / 2
                                             : worldGrid.width
                        Layout.preferredWidth: Layout.minimumWidth
                        Layout.maximumWidth: Layout.minimumWidth
                        Layout.preferredHeight: 220
                        radius: Theme.radiusMedium
                        color: mouse.containsMouse ? Theme.surfaceInteractive : Theme.surfaceRaised
                        border.width: String(modelData.id) === root.selectedWorldId || activeFocus ? 1 : 0
                        border.color: activeFocus ? Theme.accent : Theme.lineStrong
                        activeFocusOnTab: true
                        Accessible.name: String(modelData.name)
                        Accessible.role: Accessible.Button

                        function choose() {
                            root.selectedWorldId = String(modelData.id)
                            root.worldPicked(root.selectedWorldId)
                            root.close()
                        }

                        Keys.onReturnPressed: choose()
                        Keys.onEnterPressed: choose()
                        Keys.onSpacePressed: choose()

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 7
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 92
                                radius: Theme.radiusSmall
                                color: Theme.canvasSoft
                                clip: true
                                Image {
                                    id: previewImage
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    source: {
                                        if (!root.opened || !Boolean(option.modelData.hasMap))
                                            return ""
                                        return Theme.darkMode
                                                ? String(option.modelData.darkMapSource || "")
                                                : String(option.modelData.lightMapSource || "")
                                    }
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    visible: status === Image.Ready
                                }
                                ThemedIcon {
                                    anchors.centerIn: parent
                                    width: 24
                                    height: 24
                                    source: root.opened
                                            ? (String(option.modelData.id) === "default"
                                               ? "qrc:/icons/maximize-2.svg"
                                               : "qrc:/icons/map-pinned.svg")
                                            : ""
                                    iconColor: Theme.textFaint
                                    visible: previewImage.status !== Image.Ready
                                }
                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 6
                                    text: root.typeLabel(String(option.modelData.sceneType || ""),
                                                         String(option.modelData.id))
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textTiny
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 7
                                Rectangle {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    radius: 8
                                    color: "transparent"
                                    border.width: 1
                                    border.color: String(option.modelData.id) === root.selectedWorldId
                                                  ? Theme.accent : Theme.lineStrong
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 7
                                        height: 7
                                        radius: 4
                                        color: Theme.accent
                                        visible: String(option.modelData.id) === root.selectedWorldId
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: String(option.modelData.name)
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textSmall
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(option.modelData.description)
                                color: Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.capabilityText(option.modelData)
                                color: Theme.textFaint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
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
