import QtQuick
import QtQuick.Controls.Basic
import FlightBench
import "shell"

ApplicationWindow {
    id: window
    property alias currentPage: shell.currentIndex
    property string drawerRequest: ""
    property string themeRequest: ""
    onDrawerRequestChanged: {
        if (drawerRequest.length > 0)
            shell.openDrawer(drawerRequest)
    }
    onThemeRequestChanged: {
        if (themeRequest === "light")
            Theme.darkMode = false
        else if (themeRequest === "dark")
            Theme.darkMode = true
    }
    width: 840
    height: 700
    minimumWidth: 760
    minimumHeight: 620
    visible: true
    title: "FlightBench · PX4 本地仿真"
    color: Theme.canvas

    onActiveChanged: {
        if (!active)
            App.releaseKeyboardControl()
    }

    background: Rectangle {
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.canvasSoft }
            GradientStop { position: 1.0; color: Theme.canvas }
        }
    }

    AppShell {
        id: shell
        anchors.fill: parent
    }
}
