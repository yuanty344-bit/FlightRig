import QtQuick
import QtQuick.Layouts
import QtTest
import FlightBench
import "../../qml/features/settings"

TestCase {
    id: testCase
    name: "SettingsRows"
    visible: true
    when: windowShown
    width: 700; height: 650
    ColumnLayout {
        id: column
        width: 400
        SettingsPageHeader { id: header; title: "外观"; subtitle: "主题、文字与动态效果" }
        SettingsSectionLabel { text: "设置" }
        SettingsCategoryRow { id: category; title: "操作"; summary: "快捷键"; iconSource: ""; targetPage: 2 }
        SettingsSwitchRow { id: toggle; title: "显示轨迹"; summary: "继续记录" }
        SettingsChoiceRow { id: choice; title: "选项"; choices: [{label:"默认",value:"default"}]; selectedValue: "default"; controlColumnWidth: 190 }
        SettingsNumberRow { id: number; title: "速度"; value: 2; unit: "m/s"; controlColumnWidth: 190 }
        SettingsValueLine { id: line; label: "版本"; value: "0.12.0"; controlColumnWidth: 190 }
    }
    SignalSpy { id: categorySpy; target: category; signalName: "activated" }
    SignalSpy { id: toggleSpy; target: toggle; signalName: "toggled" }
    SignalSpy { id: headerSpy; target: header; signalName: "returnRequested" }
    function test_interaction() {
        categorySpy.clear(); toggleSpy.clear(); headerSpy.clear()
        mouseClick(category, category.width/2, category.height/2)
        compare(categorySpy.count, 1)
        mouseClick(toggle, toggle.width-19, toggle.height/2)
        compare(toggleSpy.count, 1)
        mouseClick(header.children[0])
        compare(headerSpy.count, 1)
    }
    function test_layout() {
        failOnWarning(/.*(recursive rearrange|Binding loop|Unable to assign|is not defined).*/)
        for (const mode of ["light", "dark"]) {
            Theme.mode=mode
            for (const width of [340,400,600]) {
                column.width=width
                wait(30)
                compare(choice.width,width); compare(number.width,width); compare(line.width,width)
                compare(choice.children[1].x+choice.children[1].width, width)
                compare(number.children[1].x+number.children[1].width, width)
                compare(line.children[1].x+line.children[1].width, width)
            }
        }
    }
}
