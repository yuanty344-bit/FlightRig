pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Dialog {
    id: root
    property var modelOptions: []
    property var worldOptions: []
    property string modelValue: ""
    property string worldValue: ""
    property string resultValue: ""
    property int dateDays: 0
    property int filteredCount: 0
    property int totalCount: 0
    readonly property bool filtersActive: modelValue.length > 0
                                          || worldValue.length > 0
                                          || resultValue.length > 0
                                          || dateDays > 0
    signal modelSelected(string value)
    signal worldSelected(string value)
    signal resultSelected(string value)
    signal dateSelected(int days)
    signal clearRequested()

    anchors.centerIn: parent
    width: Math.min(390, parent ? parent.width - 40 : 390)
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.lineStrong
    }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: "筛选会话"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTitle
                font.weight: Font.Medium
            }
            CompactIconButton {
                iconSource: "qrc:/icons/x.svg"
                accessibleLabel: "关闭会话筛选"
                toolTipText: "关闭"
                onClicked: root.close()
            }
        }
        Text { text: "机型"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
        SelectField {
            Layout.fillWidth: true
            model: root.modelOptions
            selectedValue: root.modelValue
            onValuePicked: value => root.modelSelected(String(value || ""))
        }
        Text { Layout.topMargin: 2; text: "仿真场景"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
        SelectField {
            Layout.fillWidth: true
            model: root.worldOptions
            selectedValue: root.worldValue
            onValuePicked: value => root.worldSelected(String(value || ""))
        }
        Text { Layout.topMargin: 2; text: "测试结论"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
        SelectField {
            Layout.fillWidth: true
            model: [
                { label: "全部结论", value: "" },
                { label: "通过", value: "passed" },
                { label: "有提醒", value: "warning" },
                { label: "未通过", value: "failed" },
                { label: "用户中止", value: "aborted" },
                { label: "需检查", value: "review" },
                { label: "未完成", value: "incomplete" },
                { label: "未评估", value: "not-rated" }
            ]
            selectedValue: root.resultValue
            onValuePicked: value => root.resultSelected(String(value || ""))
        }
        Text { Layout.topMargin: 2; text: "日期"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
        SelectField {
            Layout.fillWidth: true
            model: [
                { label: "全部日期", value: 0 },
                { label: "今天", value: 1 },
                { label: "近 7 天", value: 7 },
                { label: "近 30 天", value: 30 }
            ]
            selectedValue: root.dateDays
            onValuePicked: value => root.dateSelected(Number(value || 0))
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 4; color: Theme.line }
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: "显示 " + root.filteredCount + " / " + root.totalCount + " 条记录"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
            ActionButton {
                Layout.preferredWidth: 82
                text: "清除"
                kind: "secondary"
                enabled: root.filtersActive
                onClicked: root.clearRequested()
            }
            ActionButton {
                Layout.preferredWidth: 82
                text: "完成"
                onClicked: root.close()
            }
        }
    }
}
