pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench
import "../../controls"

ScrollView {
    id: root
    property int controlColumnWidth: 190
    signal returnRequested()
    signal colorRequested()
    clip: true; leftPadding: 20; rightPadding: 20; topPadding: 12; bottomPadding: 18; contentWidth: availableWidth
    ScrollBar.horizontal: InsetScrollBar { policy: ScrollBar.AlwaysOff }
    ScrollBar.vertical: InsetScrollBar { policy: ScrollBar.AsNeeded }
    ColumnLayout {
        width: root.availableWidth; spacing: 3
        SettingsPageHeader { onReturnRequested: root.returnRequested(); title: "飞行轨迹"; subtitle: "同样作用于三维视图和任务地图" }
        SettingsSwitchRow { title: "显示飞行轨迹"; summary: "隐藏后仍继续写入会话记录"; checked: App.flightPathVisible; onToggled: App.flightPathVisible = !App.flightPathVisible }
        SettingsNumberRow { controlColumnWidth: root.controlColumnWidth; title: "轨迹粗细"; value: App.flightPathWidthMeters * 100; from: 2; to: 12; stepSize: 1; decimals: 0; unit: "cm"; onValueEdited: value => App.flightPathWidthMeters = value / 100 }
        SettingsChoiceRow { controlColumnWidth: root.controlColumnWidth; title: "可见长度"; choices: [{label:"25 m",value:25},{label:"50 m",value:50},{label:"100 m",value:100},{label:"全部",value:0}]; selectedValue: App.flightPathMaxVisibleLengthMeters; onValuePicked: value => App.flightPathMaxVisibleLengthMeters = Number(value) }
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: 48
            Text {
                anchors.left: parent.left
                anchors.right: colorPickerSlot.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "轨迹颜色"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Item {
                id: colorPickerSlot
                width: root.controlColumnWidth
                height: 38
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    id: colorPickerButton
                    anchors.fill: parent
                    radius: Theme.radiusMedium
                    color: colorPickerMouse.containsMouse ? Theme.surfaceInteractive : Theme.surfaceRaised
                    border.width: 1
                    border.color: colorPickerMouse.containsMouse ? Theme.accent : Theme.line
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8
                        Rectangle {
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            radius: 5
                            color: Theme.flightPathColor(App.flightPathColorPreset)
                            border.width: 1
                            border.color: Qt.rgba(Theme.textPrimary.r, Theme.textPrimary.g, Theme.textPrimary.b, 0.25)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: String(Theme.flightPathColor(App.flightPathColorPreset)).toUpperCase()
                            color: Theme.textSecondary
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.textTiny
                            elide: Text.ElideRight
                        }
                        ThemedIcon {
                            Layout.preferredWidth: 15
                            Layout.preferredHeight: 15
                            source: "qrc:/icons/palette.svg"
                            iconColor: Theme.textMuted
                        }
                    }
                    MouseArea {
                        id: colorPickerMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.colorRequested()
                        }
                    }
                }
            }
        }
        SettingsSwitchRow { title: "开始新仿真时清空轨迹"; summary: "关闭后可继续显示上一段临时轨迹"; checked: App.flightPathAutoClear; onToggled: App.flightPathAutoClear = !App.flightPathAutoClear }
        RowLayout {
            Layout.fillWidth: true; Layout.topMargin: 10; spacing: 8
            ActionButton { Layout.fillWidth: true; compact: true; kind: "secondary"; text: "立即清空"; iconSource: "qrc:/icons/trash-2.svg"; onClicked: App.clearFlightPath() }
            ActionButton { Layout.fillWidth: true; compact: true; kind: "secondary"; text: "恢复默认"; iconSource: "qrc:/icons/refresh-cw.svg"; onClicked: App.resetFlightPathDisplaySettings() }
        }
    }
}
