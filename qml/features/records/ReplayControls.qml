pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

// Playback strip for session replay. Replaces the flight-control strip on
// the flight page while a replay is active.
Item {
    id: root
    signal playPauseRequested()
    signal stopRequested()
    signal closeRequested()
    signal seekRequested(int timeMs)
    signal speedRequested(real multiplier)

    function formatTime(ms) {
        const total = Math.floor(ms / 1000)
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function cleanEventLabel(label) {
        return String(label || "").replace(/^\[[^\]]+\]\s*/, "")
    }

    function seekRelative(deltaMs) {
        root.seekRequested(Math.max(0, Math.min(App.replayDurationMs,
                                                App.replayTimeMs + deltaMs)))
    }

    function pauseForInspection() {
        if (App.replayPlaying) root.playPauseRequested()
    }

    function inspectTime(timeMs) {
        pauseForInspection()
        root.seekRequested(Math.max(0, Math.min(App.replayDurationMs, timeMs)))
    }

    // Keep precise inspection available without adding another row of controls.
    // Arrow keys move five seconds; comma/period step 200 ms.
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: App.replayActive
        onActivated: root.playPauseRequested()
    }

    Shortcut {
        sequence: "Left"
        context: Qt.ApplicationShortcut
        enabled: App.replayActive
        onActivated: root.seekRelative(-5000)
    }

    Shortcut {
        sequence: "Right"
        context: Qt.ApplicationShortcut
        enabled: App.replayActive
        onActivated: root.seekRelative(5000)
    }

    Shortcut {
        sequence: ","
        context: Qt.ApplicationShortcut
        enabled: App.replayActive
        onActivated: root.seekRelative(-200)
    }

    Shortcut {
        sequence: "."
        context: Qt.ApplicationShortcut
        enabled: App.replayActive
        onActivated: root.seekRelative(200)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 10

        CompactIconButton {
            implicitWidth: 34
            implicitHeight: 34
            iconSource: App.replayPlaying ? "qrc:/icons/pause.svg" : "qrc:/icons/play.svg"
            accessibleLabel: App.replayPlaying ? "暂停回放" : "继续回放"
            toolTipText: accessibleLabel
            onClicked: root.playPauseRequested()
        }

        CompactIconButton {
            implicitWidth: 34
            implicitHeight: 34
            iconSource: "qrc:/icons/rotate-ccw.svg"
            accessibleLabel: "回到回放开头"
            toolTipText: "回到开头"
            onClicked: root.stopRequested()
        }

        CompactIconButton {
            implicitWidth: 34
            implicitHeight: 34
            iconSource: "qrc:/icons/chevron-down.svg"
            iconRotation: 90
            accessibleLabel: "上一个事件"
            toolTipText: "上一个事件"
            enabled: App.replayEvents.length > 0
                     && (App.replayCurrentEventIndex > 0
                         || App.replayTimeMs > Number(App.replayEvents[0].timeMs || 0))
            onClicked: {
                root.pauseForInspection()
                App.seekPreviousReplayEvent()
            }
        }

        CompactIconButton {
            implicitWidth: 34
            implicitHeight: 34
            iconSource: "qrc:/icons/chevron-down.svg"
            iconRotation: -90
            accessibleLabel: "下一个事件"
            toolTipText: "下一个事件"
            enabled: App.replayEvents.length > 0
                     && (App.replayCurrentEventIndex < App.replayEvents.length - 1
                         || App.replayTimeMs < Number(App.replayEvents[App.replayEvents.length - 1].timeMs || 0))
            onClicked: {
                root.pauseForInspection()
                App.seekNextReplayEvent()
            }
        }

        Text {
            Layout.preferredWidth: 98
            text: root.formatTime(App.replayTimeMs) + " / " + root.formatTime(App.replayDurationMs)
            color: Theme.textSecondary
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.textSmall
            horizontalAlignment: Text.AlignHCenter
        }

        Slider {
            id: progressSlider
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            from: 0
            to: Math.max(1, App.replayDurationMs)
            value: App.replayTimeMs
            onMoved: root.seekRequested(Math.round(value))
            Accessible.name: "回放进度"
            background: Rectangle {
                x: progressSlider.leftPadding
                y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                width: progressSlider.availableWidth
                height: 3
                radius: 2
                color: Theme.lineStrong
                Rectangle {
                    width: progressSlider.visualPosition * parent.width
                    height: parent.height
                    radius: parent.radius
                    color: Theme.accent
                }
                Repeater {
                    model: App.replayEvents
                    delegate: Rectangle {
                        id: eventMarker
                        required property var modelData
                        x: Math.max(0, Math.min(parent.width - width,
                            Number(modelData.timeMs || 0) / Math.max(1, App.replayDurationMs) * parent.width))
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2
                        height: 9
                        radius: 1
                        color: Theme.warning
                        MouseArea {
                            id: eventMouse
                            anchors.centerIn: parent
                            width: 12
                            height: 20
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.inspectTime(Number(eventMarker.modelData.timeMs || 0))
                        }
                        AppToolTip {
                            visible: eventMouse.containsMouse
                            text: root.formatTime(Number(eventMarker.modelData.timeMs || 0))
                                  + "  " + root.cleanEventLabel(eventMarker.modelData.label)
                        }
                    }
                }
            }
            handle: Rectangle {
                x: progressSlider.leftPadding + progressSlider.visualPosition * (progressSlider.availableWidth - width)
                y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                width: 15
                height: 15
                radius: 8
                color: Theme.surface
                border.width: 2
                border.color: Theme.accent
            }
        }

        SegmentedControl {
            Layout.preferredWidth: 174
            options: ["0.5x", "1x", "2x", "4x"]
            currentIndex: App.replaySpeedMultiplier >= 4 ? 3
                          : App.replaySpeedMultiplier >= 2 ? 2
                          : App.replaySpeedMultiplier <= 0.5 ? 0 : 1
            onSelected: index => root.speedRequested(index === 0 ? 0.5
                                                     : index === 1 ? 1.0
                                                     : index === 2 ? 2.0
                                                     : 4.0)
        }

        CompactIconButton {
            outlined: true
            iconSource: "qrc:/icons/x.svg"
            accessibleLabel: "退出回放"
            toolTipText: "退出回放"
            onClicked: root.closeRequested()
        }
    }

    Connections {
        target: App
        function onReplayTimeChanged() {
            if (!progressSlider.pressed)
                progressSlider.value = App.replayTimeMs
        }
    }
}
