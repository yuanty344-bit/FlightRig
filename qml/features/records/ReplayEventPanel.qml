pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Rectangle {
    id: root
    signal closeRequested()
    signal eventRequested(int eventIndex)

    radius: Theme.radiusLarge
    color: Theme.surface
    border.width: 1
    border.color: Theme.line
    clip: true

    function formatTime(ms) {
        const total = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return (minutes < 10 ? "0" : "") + minutes + ":"
                + (seconds < 10 ? "0" : "") + seconds
    }

    function cleanLabel(label) {
        return String(label || "").replace(/^\[[^\]]+\]\s*/, "")
    }

    function eventColor(label) {
        const value = String(label || "")
        if (value.indexOf("失败") >= 0 || value.indexOf("异常") >= 0
                || value.indexOf("超时") >= 0 || value.indexOf("中断") >= 0)
            return Theme.danger
        if (value.indexOf("成功") >= 0 || value.indexOf("通过") >= 0
                || value.indexOf("完成") >= 0)
            return Theme.success
        if (value.indexOf("请求") >= 0 || value.indexOf("上传") >= 0
                || value.indexOf("执行") >= 0)
            return Theme.warning
        return Theme.accent
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            Layout.leftMargin: 16
            Layout.rightMargin: 10
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: "飞行事件"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSubtitle
                font.weight: Font.Medium
            }
            Text {
                text: App.replayEvents.length + " 项"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
            CompactIconButton {
                iconSource: "qrc:/icons/x.svg"
                accessibleLabel: "关闭事件列表"
                toolTipText: "关闭"
                onClicked: root.closeRequested()
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

        ListView {
            id: eventList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: App.replayEvents.length > 0
            model: App.replayEvents
            currentIndex: App.replayCurrentEventIndex
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            onCurrentIndexChanged: {
                if (visible && currentIndex >= 0)
                    positionViewAtIndex(currentIndex, ListView.Contain)
            }

            delegate: Item {
                id: eventRow
                required property int index
                required property var modelData
                width: eventList.width
                height: Math.max(52, eventText.implicitHeight + 22)
                activeFocusOnTab: true
                Accessible.name: root.formatTime(eventRow.modelData.timeMs) + " "
                                 + root.cleanLabel(eventRow.modelData.label)
                Accessible.role: Accessible.Button

                Rectangle {
                    anchors.fill: parent
                    color: eventRow.index === App.replayCurrentEventIndex
                           ? Theme.accentSoft : rowMouse.containsMouse
                                                ? Theme.surfaceInteractive : "transparent"
                }
                Rectangle {
                    visible: eventRow.activeFocus
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radiusSmall
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.accent
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 12
                    spacing: 10
                    Text {
                        Layout.preferredWidth: 42
                        text: root.formatTime(eventRow.modelData.timeMs)
                        color: eventRow.index === App.replayCurrentEventIndex
                               ? Theme.accent : Theme.textMuted
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.textTiny
                    }
                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 4
                        color: root.eventColor(eventRow.modelData.label)
                    }
                    Text {
                        id: eventText
                        Layout.fillWidth: true
                        text: root.cleanLabel(eventRow.modelData.label)
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        wrapMode: Text.WordWrap
                    }
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.line
                }
                Keys.onReturnPressed: root.eventRequested(eventRow.index)
                Keys.onEnterPressed: root.eventRequested(eventRow.index)
                Keys.onSpacePressed: root.eventRequested(eventRow.index)
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        eventRow.forceActiveFocus(Qt.MouseFocusReason)
                        root.eventRequested(eventRow.index)
                    }
                }
            }
        }

        Text {
            visible: App.replayEvents.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: "本次会话没有事件标记"
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
