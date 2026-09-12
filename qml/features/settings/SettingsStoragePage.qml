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
    signal clearTrashRequested()
    clip: true; leftPadding: 20; rightPadding: 20; topPadding: 12; bottomPadding: 18; contentWidth: availableWidth
    ScrollBar.horizontal: InsetScrollBar { policy: ScrollBar.AlwaysOff }
    ScrollBar.vertical: InsetScrollBar { policy: ScrollBar.AsNeeded }
    ColumnLayout {
        width: root.availableWidth; spacing: 3
        SettingsPageHeader { onReturnRequested: root.returnRequested(); title: "记录与存储"; subtitle: "遥测采样、报告和飞行记录回收站" }
        SettingsChoiceRow { controlColumnWidth: root.controlColumnWidth; title: "遥测记录频率"; choices: [{label:"2 Hz · 节省空间",value:2},{label:"5 Hz · 推荐",value:5},{label:"10 Hz · 更精细",value:10}]; selectedValue: App.telemetryRecordingHz; onValuePicked: value => App.telemetryRecordingHz = Number(value) }
        SettingsSwitchRow { title: "结束后生成 HTML 报告"; summary: "报告保存在文档目录，不会自动弹出"; checked: App.autoGenerateReports; onToggled: App.autoGenerateReports = !App.autoGenerateReports }
        SettingsChoiceRow { controlColumnWidth: root.controlColumnWidth; title: "回收站自动清理"; choices: [{label:"7 天",value:7},{label:"30 天",value:30},{label:"90 天",value:90},{label:"不自动清理",value:0}]; selectedValue: App.trashRetentionDays; onValuePicked: value => App.trashRetentionDays = Number(value) }
        SettingsSectionLabel { text: "本机存储" }
        SettingsValueLine { controlColumnWidth: root.controlColumnWidth; label: "飞行记录与回收站"; value: App.sessionStorageSizeLabel }
        SettingsValueLine { controlColumnWidth: root.controlColumnWidth; label: "回收站"; value: App.trashSessionCount + " 条" }
        RowLayout {
            Layout.fillWidth: true; Layout.topMargin: 8; spacing: 8
            ActionButton { Layout.fillWidth: true; compact: true; kind: "secondary"; text: "打开记录文件夹"; iconSource: "qrc:/icons/folder-open.svg"; onClicked: App.openSessionsFolder() }
            ActionButton { Layout.fillWidth: true; compact: true; kind: "danger"; text: "清空回收站"; iconSource: "qrc:/icons/archive-x.svg"; enabled: App.trashSessionCount > 0; disabledReason: "飞行记录回收站已经是空的"; onClicked: root.clearTrashRequested() }
        }
        Text { Layout.fillWidth: true; Layout.topMargin: 8; text: "采样频率越高，轨迹和图表越细致，也会占用更多空间。"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
        ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; Layout.topMargin: 8; compact: true; kind: "secondary"; text: "恢复记录默认值"; iconSource: "qrc:/icons/refresh-cw.svg"; onClicked: App.resetRecordingPreferences() }
    }
}
