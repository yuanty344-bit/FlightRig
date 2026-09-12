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
    clip: true; leftPadding: 20; rightPadding: 20; topPadding: 12; bottomPadding: 18; contentWidth: availableWidth
    ScrollBar.horizontal: InsetScrollBar { policy: ScrollBar.AlwaysOff }
    ScrollBar.vertical: InsetScrollBar { policy: ScrollBar.AsNeeded }
    ColumnLayout {
        width: root.availableWidth; spacing: 3
        SettingsPageHeader { onReturnRequested: root.returnRequested(); title: "操作与快捷键"; subtitle: "只调整交互习惯，不改变安全规则" }
        SettingsNumberRow { controlColumnWidth: root.controlColumnWidth; title: "默认精确转向"; value: App.preciseTurnDefaultDegrees; from: 5; to: 180; stepSize: 5; decimals: 0; unit: "°"; onValueEdited: value => App.preciseTurnDefaultDegrees = value }
        SettingsNumberRow { controlColumnWidth: root.controlColumnWidth; title: "旋转灵敏度"; value: App.cameraRotationSensitivity; from: 0.5; to: 2.0; stepSize: 0.1; decimals: 1; onValueEdited: value => App.cameraRotationSensitivity = value }
        SettingsNumberRow { controlColumnWidth: root.controlColumnWidth; title: "缩放灵敏度"; value: App.cameraZoomSensitivity; from: 0.5; to: 2.0; stepSize: 0.1; decimals: 1; onValueEdited: value => App.cameraZoomSensitivity = value }
        SettingsSwitchRow { title: "反转垂直旋转"; checked: App.invertCameraVertical; onToggled: App.invertCameraVertical = !App.invertCameraVertical }
        SettingsSectionLabel { text: "飞行快捷键（固定）" }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 104; radius: Theme.radiusMedium; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.line
            GridLayout {
                anchors.fill: parent; anchors.margins: 12; columns: 2; columnSpacing: 16; rowSpacing: 7
                Text { text: "W A S D"; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall }
                Text { text: "水平移动"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                Text { text: "Space / C"; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall }
                Text { text: "上升 / 下降"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                Text { text: "Q / E"; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall }
                Text { text: "左转 / 右转"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                Text { text: "Esc / L"; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall }
                Text { text: "退出控制 / 安全降落"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
            }
        }
        ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; compact: true; kind: "secondary"; text: "恢复操作默认值"; iconSource: "qrc:/icons/refresh-cw.svg"; onClicked: App.resetInteractionPreferences() }
    }
}
