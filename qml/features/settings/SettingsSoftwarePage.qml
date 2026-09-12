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
    signal importRequested()
    signal resetAllRequested()
    clip: true; leftPadding: 20; rightPadding: 20; topPadding: 12; bottomPadding: 18; contentWidth: availableWidth
    ScrollBar.horizontal: InsetScrollBar { policy: ScrollBar.AlwaysOff }
    ScrollBar.vertical: InsetScrollBar { policy: ScrollBar.AsNeeded }
    ColumnLayout {
        width: root.availableWidth; spacing: 2
        SettingsPageHeader { onReturnRequested: root.returnRequested(); title: "软件与环境"; subtitle: "版本、数据位置与设置备份" }
        SettingsSectionLabel { text: "版本" }
        SettingsValueLine { controlColumnWidth: root.controlColumnWidth; label: "FlightRig"; value: App.applicationVersion }
        SettingsValueLine { controlColumnWidth: root.controlColumnWidth; label: "Qt"; value: App.qtRuntimeVersion }
        SettingsValueLine { controlColumnWidth: root.controlColumnWidth; label: "MAVSDK"; value: App.mavsdkVersion }
        SettingsValueLine { controlColumnWidth: root.controlColumnWidth; label: "构建"; value: App.buildDescription; multiline: true }
        SettingsValueLine { controlColumnWidth: root.controlColumnWidth; label: "系统"; value: App.platformDescription; multiline: true }
        ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; Layout.topMargin: 6; compact: true; kind: "secondary"; text: "复制环境信息"; iconSource: "qrc:/icons/copy.svg"; onClicked: App.copyEnvironmentInformation() }
        SettingsSectionLabel { text: "本机数据位置" }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            ActionButton { Layout.fillWidth: true; Layout.minimumWidth: 80; compact: true; kind: "secondary"; text: "记录"; iconSource: "qrc:/icons/database.svg"; onClicked: App.openSessionsFolder() }
            ActionButton { Layout.fillWidth: true; Layout.minimumWidth: 80; compact: true; kind: "secondary"; text: "航线"; iconSource: "qrc:/icons/route.svg"; onClicked: App.openRoutesFolder() }
            ActionButton { Layout.fillWidth: true; Layout.minimumWidth: 80; compact: true; kind: "secondary"; text: "方案"; iconSource: "qrc:/icons/clipboard-check.svg"; onClicked: App.openPlansFolder() }
        }
        SettingsSectionLabel { text: "设置备份" }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            ActionButton { Layout.fillWidth: true; compact: true; kind: "secondary"; text: "导出设置"; iconSource: "qrc:/icons/file-down.svg"; onClicked: App.exportApplicationSettings() }
            ActionButton { Layout.fillWidth: true; compact: true; kind: "secondary"; text: "导入设置"; iconSource: "qrc:/icons/upload.svg"; onClicked: root.importRequested() }
        }
        Text { Layout.fillWidth: true; text: "设置文件不包含飞行记录、航线、测试方案或任何仿真数据。"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
        ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; compact: true; kind: "danger"; text: "恢复全部设置"; iconSource: "qrc:/icons/rotate-ccw.svg"; onClicked: root.resetAllRequested() }
    }
}
