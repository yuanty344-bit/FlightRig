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
        SettingsPageHeader { onReturnRequested: root.returnRequested(); title: "三维视图"; subtitle: "相机、位置标记与城市图层" }
        SettingsChoiceRow { controlColumnWidth: root.controlColumnWidth; title: "默认相机"; choices: [{label:"兼顾无人机与地面",value:"follow-both"},{label:"跟随无人机",value:"follow"},{label:"自由视角",value:"free"}]; selectedValue: App.defaultCameraMode; onValuePicked: value => App.defaultCameraMode = value }
        SettingsSwitchRow { title: "显示地面位置标记"; checked: App.groundMarkerVisible; onToggled: App.groundMarkerVisible = !App.groundMarkerVisible }
        SettingsChoiceRow { controlColumnWidth: root.controlColumnWidth; title: "标记大小"; choices: [{label:"自动",value:"auto"},{label:"小",value:"small"},{label:"中",value:"medium"},{label:"大",value:"large"}]; selectedValue: App.groundMarkerSizeMode; onValuePicked: value => App.groundMarkerSizeMode = value }
        SettingsNumberRow { controlColumnWidth: root.controlColumnWidth; title: "建筑透明度"; value: App.cityBuildingOpacity * 100; from: 25; to: 100; stepSize: 5; decimals: 0; unit: "%"; onValueEdited: value => App.cityBuildingOpacity = value / 100 }
        SettingsSectionLabel { text: "地图图层" }
        SettingsSwitchRow { title: "道路"; checked: App.worldRoadsVisible; onToggled: App.worldRoadsVisible = !App.worldRoadsVisible }
        SettingsSwitchRow { title: "绿地"; checked: App.worldGreenAreasVisible; onToggled: App.worldGreenAreasVisible = !App.worldGreenAreasVisible }
        SettingsSwitchRow { title: "水面"; checked: App.worldWaterVisible; onToggled: App.worldWaterVisible = !App.worldWaterVisible }
        SettingsSwitchRow { title: "默认场地网格"; checked: App.defaultGridVisible; onToggled: App.defaultGridVisible = !App.defaultGridVisible }
        ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; Layout.topMargin: 8; compact: true; kind: "secondary"; text: "恢复视图默认值"; iconSource: "qrc:/icons/refresh-cw.svg"; onClicked: App.resetViewPreferences() }
    }
}
