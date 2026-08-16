pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import FlightBench

Rectangle {
    id: root

    property real value: 0
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property int decimals: 1
    property string unit: ""
    property bool showSteppers: true
    property string accessibleName: "数值"
    signal valueEdited(real value)

    implicitWidth: 132
    implicitHeight: 34
    radius: Theme.radiusMedium
    color: field.activeFocus ? Theme.surface : Theme.surfaceRaised
    border.width: field.activeFocus ? 2 : 1
    border.color: field.activeFocus ? Theme.accent : Theme.line
    opacity: enabled ? 1 : 0.55

    function formatted(number) {
        return Number(number).toFixed(decimals)
    }

    function normalized(number) {
        if (!Number.isFinite(number))
            return Math.max(from, Math.min(to, value))
        const stepped = stepSize > 0
                      ? Math.round((number - from) / stepSize) * stepSize + from
                      : number
        return Math.max(from, Math.min(to, stepped))
    }

    function commit(number) {
        const result = normalized(number)
        field.text = formatted(result)
        if (Math.abs(result - value) > Math.pow(10, -decimals) / 2)
            valueEdited(result)
    }

    function currentEntry() {
        const entry = Number(field.text.replace(",", "."))
        return Number.isFinite(entry) ? entry : value
    }

    onValueChanged: {
        if (!field.activeFocus)
            field.text = formatted(value)
    }

    Component.onCompleted: field.text = formatted(value)

    TextField {
        id: field
        anchors.left: parent.left
        anchors.right: stepperColumn.visible ? stepperColumn.left : parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        leftPadding: 10
        rightPadding: unitLabel.visible ? unitLabel.width + 10 : 8
        color: Theme.textPrimary
        selectedTextColor: Theme.accentContent
        selectionColor: Theme.accent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSmall
        horizontalAlignment: Text.AlignRight
        verticalAlignment: Text.AlignVCenter
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true
        enabled: root.enabled
        Accessible.name: root.accessibleName
        Accessible.role: Accessible.EditableText
        validator: DoubleValidator {
            bottom: root.from
            top: root.to
            decimals: root.decimals
            notation: DoubleValidator.StandardNotation
        }
        background: Item { }
        onEditingFinished: root.commit(Number(text.replace(",", ".")))
        Keys.onUpPressed: root.commit(root.currentEntry() + root.stepSize)
        Keys.onDownPressed: root.commit(root.currentEntry() - root.stepSize)

        Text {
            id: unitLabel
            visible: root.unit.length > 0
            anchors.right: parent.right
            anchors.rightMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            text: root.unit
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
        }
    }

    Column {
        id: stepperColumn
        visible: root.showSteppers
        width: 24
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        Repeater {
            model: [1, -1]
            delegate: Rectangle {
                id: stepButton
                required property int modelData
                width: stepperColumn.width
                height: root.height / 2
                color: stepMouse.containsMouse ? Theme.surfaceInteractive : "transparent"

                ThemedIcon {
                    anchors.centerIn: parent
                    width: 11
                    height: 11
                    source: "qrc:/icons/chevron-down.svg"
                    rotation: stepButton.modelData > 0 ? 180 : 0
                    iconColor: root.enabled ? Theme.textMuted : Theme.textFaint
                }

                MouseArea {
                    id: stepMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: root.enabled
                    cursorShape: Qt.PointingHandCursor
                    property bool repeating: false
                    onPressed: {
                        repeating = false
                        holdTimer.restart()
                    }
                    onClicked: {
                        if (!repeating)
                            root.commit(root.currentEntry() + stepButton.modelData * root.stepSize)
                    }
                    onReleased: { holdTimer.stop(); repeatTimer.stop() }
                    onCanceled: { holdTimer.stop(); repeatTimer.stop() }
                }

                Timer {
                    id: holdTimer
                    interval: 350
                    onTriggered: {
                        stepMouse.repeating = true
                        root.commit(root.currentEntry() + stepButton.modelData * root.stepSize)
                        repeatTimer.restart()
                    }
                }

                Timer {
                    id: repeatTimer
                    interval: 80
                    repeat: true
                    onTriggered: root.commit(root.currentEntry() + stepButton.modelData * root.stepSize)
                }
            }
        }
    }

    Rectangle {
        visible: stepperColumn.visible
        anchors.left: stepperColumn.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.line
    }

    Rectangle {
        visible: stepperColumn.visible
        anchors.left: stepperColumn.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 1
        color: Theme.line
    }
}
