import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench
import "../../controls"

Rectangle {
    id: root
    property bool compactEditor: false
    property bool selectionValid: false
    property bool editingLocked: false
    property bool gimbalMission: false
    property int selectedIndex: -1
    property int pointCount: 0
    property var point: ({})
    readonly property var displayedPoint: point || ({})
    // Selection and model-entry bindings may update in different evaluations.
    // Missing values are display fallbacks only; never write them to the route.
    function pointNumber(role, fallback) {
        if (!selectionValid) return fallback
        const value = displayedPoint[role]
        if (value === undefined || value === null || value === "") return fallback
        const number = Number(value)
        return Number.isFinite(number) ? number : fallback
    }
    property string endAction: "rtl"
    signal pointValueEdited(string role, var value)
    signal endActionSelected(string action)
    signal removeRequested()
    signal clearRequested()
    signal focusRequested()
    Layout.preferredWidth: 252
    Layout.fillHeight: true
    radius: Theme.radiusLarge
    color: Theme.surface
    border.width: 1
    border.color: Theme.line

    ScrollView {
        id: waypointScroll
        anchors.fill: parent
        anchors.leftMargin: root.compactEditor ? 12 : 14
        anchors.topMargin: root.compactEditor ? 12 : 14
        anchors.bottomMargin: root.compactEditor ? 12 : 14
        anchors.rightMargin: 2
        clip: true
        rightPadding: 12
        contentWidth: availableWidth

        ColumnLayout {
        width: waypointScroll.availableWidth
        spacing: root.compactEditor ? 7 : 9

        RowLayout {
            Layout.fillWidth: true
            ThemedIcon { Layout.preferredWidth: 18; Layout.preferredHeight: 18; source: "qrc:/icons/map-pin.svg"; iconColor: Theme.accentHover }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text { Layout.fillWidth: true; text: root.selectionValid ? String(root.displayedPoint.name || ("航点 " + (root.selectedIndex + 1))) : "航点"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSubtitle; font.weight: Font.Medium; elide: Text.ElideRight }
                Text { text: root.selectionValid ? "第 " + String(root.selectedIndex + 1).padStart(2, "0") + " 个航点" : "未选择"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
            }
            Rectangle {
                Layout.preferredWidth: 50; Layout.preferredHeight: 24; radius: 12
                color: root.editingLocked ? Theme.warningSoft : Theme.accentSoft
                Text { anchors.centerIn: parent; text: root.editingLocked ? "已锁定" : "编辑中"; color: root.editingLocked ? Theme.warning : Theme.accentHover; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
            }
        }

        Text {
            visible: !root.compactEditor
            Layout.fillWidth: true
            text: root.editingLocked
                  ? (App.missionPaused
                     ? "任务暂停时保持航线锁定；请继续任务，结束后再清除或修改。"
                     : "任务执行期间航线已锁定，结束后才能修改。")
                  : "调整当前航点；拖动地图标记可修改位置。"
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "航点名称"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
            TextField {
                id: waypointNameField
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                enabled: root.selectionValid && !root.editingLocked
                maximumLength: 24
                selectByMouse: true
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                onEditingFinished: {
                    var nextName = text.trim()
                    if (!nextName.length) nextName = "航点 " + (root.selectedIndex + 1)
                    text = nextName
                    root.pointValueEdited("name", nextName)
                }
                background: Rectangle { radius: Theme.radiusMedium; color: Theme.surfaceRaised; border.width: 1; border.color: waypointNameField.activeFocus ? Theme.accent : Theme.line }

                Binding {
                    target: waypointNameField
                    property: "text"
                    when: !waypointNameField.activeFocus
                    restoreMode: Binding.RestoreBindingOrValue
                    value: root.selectionValid
                           ? String(root.displayedPoint.name
                                    || ("航点 " + (root.selectedIndex + 1)))
                           : ""
                }
            }
        }

        component WaypointNumberRow: RowLayout {
            id: waypointRow
            property string label: "参数"
            property string roleName: ""
            property real currentValue: 0
            property real minimum: 0
            property real maximum: 100
            property real step: 1
            property int decimalPlaces: 1
            property string unitText: ""
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            spacing: 8
            Text { Layout.preferredWidth: 58; text: waypointRow.label; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
            NumericField {
                Layout.fillWidth: true
                value: waypointRow.currentValue
                from: waypointRow.minimum; to: waypointRow.maximum
                stepSize: waypointRow.step; decimals: waypointRow.decimalPlaces
                unit: waypointRow.unitText
                enabled: root.selectionValid && !root.editingLocked
                accessibleName: waypointRow.label
                onValueEdited: value => root.pointValueEdited(waypointRow.roleName, value)
            }
        }

        WaypointNumberRow { label: "相对高度"; roleName: "altitudeMeters"; currentValue: root.pointNumber("altitudeMeters", 8); minimum: 1; maximum: 100; step: 0.1; decimalPlaces: 1; unitText: "m" }
        WaypointNumberRow { label: "飞行速度"; roleName: "speedMetersPerSecond"; currentValue: root.pointNumber("speedMetersPerSecond", 4); minimum: 0.5; maximum: 10; step: 0.1; decimalPlaces: 1; unitText: "m/s" }
        WaypointNumberRow { label: "航向角"; roleName: "yawDegrees"; currentValue: root.pointNumber("yawDegrees", 0); minimum: -180; maximum: 180; step: 1; decimalPlaces: 0; unitText: "°" }
        Rectangle {
            visible: root.gimbalMission
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.line
        }
        Text {
            visible: root.gimbalMission
            text: "云台与拍摄"
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
        }
        WaypointNumberRow {
            visible: root.gimbalMission
            label: "水平视角"
            roleName: "gimbalYawDegrees"
            currentValue: root.pointNumber("gimbalYawDegrees", 0)
            minimum: -180; maximum: 180; step: 5; decimalPlaces: 0; unitText: "°"
        }
        WaypointNumberRow {
            visible: root.gimbalMission
            label: "俯仰视角"
            roleName: "gimbalPitchDegrees"
            currentValue: root.pointNumber("gimbalPitchDegrees", -45)
            minimum: -135; maximum: 45; step: 5; decimalPlaces: 0; unitText: "°"
        }
        Rectangle {
            visible: root.gimbalMission
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            radius: Theme.radiusMedium
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.line

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                Text {
                    Layout.fillWidth: true
                    text: "到达后截图"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                }
                Rectangle {
                    id: captureSwitch
                    readonly property bool checked: root.selectionValid
                                                    && Boolean(root.displayedPoint.capturePhoto)
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 22
                    radius: 11
                    color: checked ? Theme.accent : Theme.lineStrong
                    opacity: root.selectionValid && !root.editingLocked ? 1 : 0.5
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: "到达航点后自动截图"
                    Accessible.checked: checked

                    Rectangle {
                        width: 16
                        height: 16
                        radius: 8
                        y: 3
                        x: captureSwitch.checked ? captureSwitch.width - width - 3 : 3
                        color: Theme.accentContent
                        Behavior on x {
                            NumberAnimation { duration: Theme.animationDuration(100) }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: root.selectionValid && !root.editingLocked
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.pointValueEdited("capturePhoto",
                                                       !captureSwitch.checked)
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
        Text { text: "位置（相对起飞点）"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
        WaypointNumberRow { label: "向东"; roleName: "eastMeters"; currentValue: root.pointNumber("eastMeters", 0); minimum: -2000; maximum: 2000; step: 0.1; decimalPlaces: 1; unitText: "m" }
        WaypointNumberRow { label: "向北"; roleName: "northMeters"; currentValue: root.pointNumber("northMeters", 0); minimum: -2000; maximum: 2000; step: 0.1; decimalPlaces: 1; unitText: "m" }

        ColumnLayout {
            Layout.fillWidth: true; spacing: 6
            Text { text: "任务结束后"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                radius: Theme.radiusMedium
                color: Theme.surfaceRaised
                border.width: 1
                border.color: Theme.line
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 2
                    spacing: 2
                    Repeater {
                        model: [
                            { label: "返航", value: "rtl" },
                            { label: "降落", value: "land" },
                            { label: "悬停", value: "hold" }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Theme.radiusSmall
                            color: root.endAction === modelData.value
                                   ? Theme.surface : endActionMouse.containsMouse
                                                     ? Theme.surfaceInteractive : "transparent"
                            border.width: root.endAction === modelData.value ? 1 : 0
                            border.color: activeFocus ? Theme.accent : Theme.lineStrong
                            opacity: root.editingLocked ? 0.55 : 1
                            activeFocusOnTab: !root.editingLocked
                            Accessible.name: "任务结束后" + modelData.label
                            Accessible.role: Accessible.RadioButton
                            Accessible.checked: root.endAction === modelData.value
                            Keys.onReturnPressed: selectEndAction()
                            Keys.onEnterPressed: selectEndAction()
                            Keys.onSpacePressed: selectEndAction()

                            function selectEndAction() {
                                if (root.editingLocked) return
                                if (root.endAction !== modelData.value) {
                                    root.endActionSelected(modelData.value)
                                }
                            }
                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                color: root.endAction === modelData.value
                                       ? Theme.textPrimary : Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                                font.weight: root.endAction === modelData.value
                                             ? Font.Medium : Font.Normal
                            }
                            MouseArea {
                                id: endActionMouse
                                anchors.fill: parent
                                enabled: !root.editingLocked
                                hoverEnabled: true
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    parent.selectEndAction()
                                    root.focusRequested()
                                }
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 4 }

        ActionButton {
            Layout.fillWidth: true
            compact: root.compactEditor
            text: "删除此航点"
            iconSource: "qrc:/icons/trash-2.svg"
            kind: "danger"
            enabled: !root.editingLocked && root.pointCount > 0
            onClicked: root.removeRequested()
        }

        ActionButton {
            visible: App.missionUploaded && !App.missionRunning && !App.missionPaused
            Layout.fillWidth: true
            text: "清除飞行器任务"
            iconSource: "qrc:/icons/rotate-ccw.svg"
            kind: "secondary"
            enabled: !App.missionBusy
            onClicked: root.clearRequested()
        }
        }
    }
}
