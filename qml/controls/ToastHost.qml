import QtQuick
import QtQuick.Layouts
import FlightBench

Item {
    id: root
    property string message: ""
    property bool warning: false
    property string lastMessage: ""
    property bool acceptingMessages: false
    readonly property string displayedMessage: message.endsWith("…")
                                               ? message.slice(0, -1)
                                               : message

    width: Math.min(520, parent ? parent.width - 48 : 520)
    height: toastCard.implicitHeight
    visible: opacity > 0
    opacity: 0
    Accessible.name: displayedMessage
    Accessible.role: Accessible.StaticText

    function showMessage(value) {
        var clean = String(value || "").trim()
        if (!acceptingMessages || clean.length === 0 || clean === lastMessage)
            return
        lastMessage = clean
        message = clean
        warning = App.hasIssue
        opacity = 1
        hideTimer.restart()
    }

    Connections {
        target: App
        function onStatusMessageChanged() { root.showMessage(App.statusMessage) }
    }

    Timer {
        interval: 5000
        running: true
        onTriggered: {
            root.lastMessage = App.statusMessage
            root.acceptingMessages = true
        }
    }

    Timer {
        id: hideTimer
        interval: root.warning ? 5200 : 3200
        onTriggered: root.opacity = 0
    }

    Behavior on opacity { NumberAnimation { duration: Theme.animationDuration(140) } }

    Rectangle {
        id: toastCard
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        implicitHeight: Math.max(40, toastText.implicitHeight + 18)
        radius: Theme.radiusMedium
        color: Theme.darkMode ? "#222D39" : "#202A35"
        border.width: 1
        border.color: root.warning ? Theme.warning : (Theme.darkMode ? "#3B4958" : "#303B47")

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 7
                Layout.preferredHeight: 7
                radius: 4
                color: root.warning ? Theme.warning : Theme.success
            }
            Text {
                id: toastText
                objectName: "toastText"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.displayedMessage
                color: "#F4F7FA"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }
    }
}
