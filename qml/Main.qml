import QtQuick
import QtQuick.Controls.Basic
import FlightBench
import "shell"

ApplicationWindow {
    id: window
    property alias currentPage: shell.currentIndex
    property alias flightViewIndex: shell.flightViewIndex
    property string drawerRequest: ""
    property string themeRequest: ""
    onDrawerRequestChanged: {
        if (drawerRequest.length > 0)
            shell.openDrawer(drawerRequest)
    }
    onThemeRequestChanged: {
        if (themeRequest === "light")
            Theme.mode = "light"
        else if (themeRequest === "dark")
            Theme.mode = "dark"
    }
    width: 900
    height: 650
    minimumWidth: 760
    minimumHeight: 590
    visible: true
    title: "FlightBench · PX4 本地仿真"
    color: Theme.canvas

    onActiveChanged: {
        if (!active)
            App.releaseKeyboardControl()
    }

    background: Rectangle { color: Theme.canvasSoft }

    AppShell {
        id: shell
        anchors.fill: parent
    }
}
