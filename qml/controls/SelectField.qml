pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import FlightBench

ComboBox {
    id: root
    property var selectedValue: ""
    signal valuePicked(var value)

    implicitHeight: 38
    textRole: "label"
    valueRole: "value"
    leftPadding: 12
    rightPadding: 34
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textSmall
    Accessible.name: displayText

    function syncSelection() {
        for (let index = 0; index < count; ++index) {
            if (String(valueAt(index)) === String(selectedValue)) {
                currentIndex = index
                return
            }
        }
        currentIndex = count > 0 ? 0 : -1
    }

    Component.onCompleted: syncSelection()
    onSelectedValueChanged: syncSelection()
    onModelChanged: Qt.callLater(syncSelection)
    onActivated: index => valuePicked(valueAt(index))

    contentItem: Text {
        id: selectLabel
        leftPadding: root.leftPadding
        rightPadding: root.rightPadding
        text: root.displayText
        color: Theme.textPrimary
        font: root.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    AppToolTip {
        visible: root.hovered && selectLabel.truncated && !root.popup.visible
        text: root.displayText
        maximumWidth: 280
    }
    indicator: ThemedIcon {
        x: root.width - width - 11
        y: (root.height - height) / 2
        width: 14
        height: 14
        source: "qrc:/icons/chevron-down.svg"
        iconColor: Theme.textMuted
    }
    background: Rectangle {
        radius: Theme.radiusMedium
        color: root.activeFocus ? Theme.surface : Theme.surfaceRaised
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.accent : Theme.line
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: root.width
        height: 38
        highlighted: root.highlightedIndex === index
        contentItem: Text {
            text: String(option.modelData.label || "")
            color: option.index === root.currentIndex ? Theme.accent : Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: option.highlighted ? Theme.surfaceInteractive : "transparent"
        }
    }
    popup: Popup {
        y: root.height + 4
        width: root.width
        padding: 4
        implicitHeight: Math.min(230, contentItem.implicitHeight + 8)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        }
        background: Rectangle {
            radius: Theme.radiusMedium
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.lineStrong
        }
    }
}
