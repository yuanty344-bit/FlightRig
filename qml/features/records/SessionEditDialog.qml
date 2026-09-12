import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

MotionDialog {
    id: root
    property string fallbackTitle: "飞行记录"
    signal saveRequested(string displayName, string notes)

    function prepare(displayName, notes, fallbackTitle) {
        root.fallbackTitle = fallbackTitle
        nameField.text = displayName
        notesField.text = notes
        open()
    }

    anchors.centerIn: parent
    width: Math.min(430, parent ? parent.width - 40 : 430)
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: nameField.forceActiveFocus(Qt.PopupFocusReason)
    background: DialogSurface {
        radius: Theme.radiusLarge
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.lineStrong
    }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: "编辑飞行记录"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTitle
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            PanelCloseButton {
                iconSource: "qrc:/icons/x.svg"
                accessibleLabel: "关闭飞行记录编辑"
                toolTipText: "关闭"
                onClicked: root.close()
            }
        }
        Text { text: "自定义名称"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
        TextField {
            id: nameField
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            maximumLength: 60
            placeholderText: root.fallbackTitle
            selectByMouse: true
            color: Theme.textPrimary
            placeholderTextColor: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textBody
            background: Rectangle {
                radius: Theme.radiusMedium
                color: Theme.surface
                border.width: nameField.activeFocus ? 2 : 1
                border.color: nameField.activeFocus ? Theme.accent : Theme.lineStrong
            }
            onAccepted: notesField.forceActiveFocus(Qt.TabFocusReason)
        }
        Text { text: "备注"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
        TextArea {
            id: notesField
            property int characterLimit: 500
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            placeholderText: "记录测试条件、异常原因或下一次需要关注的事项"
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            color: Theme.textPrimary
            placeholderTextColor: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
            leftPadding: 10
            rightPadding: 10
            topPadding: 8
            bottomPadding: 8
            background: Rectangle {
                radius: Theme.radiusMedium
                color: Theme.surface
                border.width: notesField.activeFocus ? 2 : 1
                border.color: notesField.activeFocus ? Theme.accent : Theme.lineStrong
            }
            onTextChanged: if (text.length > characterLimit) text = text.slice(0, characterLimit)
        }
        Text {
            Layout.fillWidth: true
            text: "名称留空时仍显示原航线名称或飞行器名称。"
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton {
                Layout.preferredWidth: 82
                text: "取消"
                kind: "secondary"
                onClicked: root.close()
            }
            ActionButton {
                Layout.preferredWidth: 92
                text: "保存"
                onClicked: root.saveRequested(nameField.text, notesField.text)
            }
        }
    }
}
