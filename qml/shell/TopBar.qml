pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import FlightBench
import "../components"

Rectangle {
    id: root
    z: 10
    signal environmentRequested()
    signal settingsRequested()
    signal refreshRequested()

    color: Theme.canvasSoft
    implicitHeight: 58

    component IconButton: Rectangle {
        id: iconButton
        property url iconSource: ""
        property string accessibleLabel: "操作"
        property bool actionEnabled: true
        property bool tipVisible: false
        signal clicked()
        Layout.preferredWidth: 34
        Layout.preferredHeight: 34
        radius: Theme.radiusMedium
        color: iconMouse.containsMouse && actionEnabled ? Theme.surfaceRaised : "transparent"
        opacity: actionEnabled ? 1.0 : 0.42
        border.width: activeFocus ? 1 : 0
        border.color: Theme.accent
        activeFocusOnTab: actionEnabled
        Accessible.name: accessibleLabel
        Accessible.role: Accessible.Button
        Keys.onReturnPressed: if (actionEnabled) iconButton.clicked()
        Keys.onEnterPressed: if (actionEnabled) iconButton.clicked()
        Keys.onSpacePressed: if (actionEnabled) iconButton.clicked()
        onActionEnabledChanged: {
            if (!actionEnabled) {
                tipDelay.stop()
                tipVisible = false
            }
        }

        ThemedIcon { anchors.centerIn: parent; width: 17; height: 17; source: iconButton.iconSource; iconColor: Theme.textSecondary; opacity: 0.90 }

        Timer {
            id: tipDelay
            interval: 450
            onTriggered: iconButton.tipVisible = iconMouse.containsMouse && iconButton.actionEnabled
        }

        Rectangle {
            anchors.top: parent.bottom
            anchors.topMargin: 7
            anchors.horizontalCenter: parent.horizontalCenter
            width: tipText.implicitWidth + 16
            height: 26
            radius: Theme.radiusSmall
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.lineStrong
            visible: iconButton.tipVisible
            z: 100

            Text {
                id: tipText
                anchors.centerIn: parent
                text: iconButton.accessibleLabel
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
        }

        MouseArea {
            id: iconMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: iconButton.actionEnabled
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onContainsMouseChanged: {
                if (containsMouse && iconButton.actionEnabled)
                    tipDelay.restart()
                else {
                    tipDelay.stop()
                    iconButton.tipVisible = false
                }
            }
            onClicked: iconButton.clicked()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 14
        spacing: 9

        Rectangle {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            radius: 8
            color: Theme.accent
            Image { anchors.centerIn: parent; width: 21; height: 21; source: "qrc:/icons/flightbench-app.svg" }
        }

        ColumnLayout {
            spacing: 0
            Text { text: "FlightBench"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 16; font.weight: Font.Medium }
            Text { text: "PX4 本地仿真"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            Layout.preferredWidth: environmentText.implicitWidth + 30
            Layout.preferredHeight: 34
            radius: Theme.radiusMedium
            color: environmentMouse.containsMouse ? Theme.surfaceRaised : "transparent"
            border.width: 1
            border.color: activeFocus ? Theme.accent : Theme.line
            activeFocusOnTab: true
            Accessible.name: environmentText.text
            Accessible.role: Accessible.Button
            Keys.onReturnPressed: root.environmentRequested()
            Keys.onEnterPressed: root.environmentRequested()
            Keys.onSpacePressed: root.environmentRequested()
            Row {
                anchors.centerIn: parent
                spacing: 7
                Rectangle { width: 7; height: 7; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: App.dockerAvailable ? Theme.success : Theme.textFaint }
                Text { id: environmentText; text: App.dockerAvailable ? "Docker 可用" : "检查 Docker"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
            }
            MouseArea { id: environmentMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.environmentRequested() }
        }

        IconButton { iconSource: "qrc:/icons/refresh-cw.svg"; accessibleLabel: "重新检测"; actionEnabled: !App.busy; onClicked: root.refreshRequested() }
        IconButton { iconSource: "qrc:/icons/sliders-horizontal.svg"; accessibleLabel: "设置"; onClicked: root.settingsRequested() }
    }

    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
}
