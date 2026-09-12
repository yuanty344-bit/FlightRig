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
        SettingsPageHeader { onReturnRequested: root.returnRequested(); title: "外观"; subtitle: "主题、文字与动态效果" }
        SettingsChoiceRow { controlColumnWidth: root.controlColumnWidth;
            title: "界面主题"
            choices: [{label:"浅色", value:"light"}, {label:"深色", value:"dark"}, {label:"跟随系统", value:"system"}]
            selectedValue: Theme.mode
            onValuePicked: value => Theme.mode = value
        }
        SettingsChoiceRow { controlColumnWidth: root.controlColumnWidth;
            title: "文字大小"
            choices: [{label:"紧凑 90%", value:90}, {label:"标准 100%", value:100}, {label:"较大 110%", value:110}]
            selectedValue: Theme.textScalePercent
            onValuePicked: value => Theme.textScalePercent = Number(value)
        }
        SettingsSwitchRow { title: "减少动态效果"; summary: "关闭不必要的移动动画"; checked: Theme.reducedMotion; onToggled: Theme.reducedMotion = !Theme.reducedMotion }
        ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; compact: true; kind: "secondary"; text: "恢复外观默认值"; iconSource: "qrc:/icons/refresh-cw.svg"; onClicked: { Theme.mode = "light"; Theme.textScalePercent = 100; Theme.reducedMotion = false } }
    }
}
