import QtQuick
import QtQuick.Layouts
import FlightBench
import "../pages"
import "../components"

Item {
    id: root
    property int currentIndex: 0
    property alias flightViewIndex: flightPage.viewIndex

    function openDrawer(mode) {
        utilityDrawer.openMode(mode)
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: 0

        TopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            onEnvironmentRequested: utilityDrawer.openMode("environment")
            onSettingsRequested: utilityDrawer.openMode("settings")
            onRefreshRequested: App.refresh()
        }

        // Plain Item + anchored pages instead of StackLayout: Qt 6.11 sizes a
        // nested StackLayout from its children's implicit widths, which made
        // the pages collapse instead of filling the window.
        Item {
            id: pageHost
            Layout.fillWidth: true
            Layout.fillHeight: true

            WorkbenchPage {
                anchors.fill: parent
                visible: root.currentIndex === 0
                onOpenEnvironment: utilityDrawer.openMode("environment")
                onOpenFlight: root.currentIndex = 1
                onOpenMission: showLibrary => {
                    root.currentIndex = 1
                    flightPage.openMissionPlanner(showLibrary)
                }
                onOpenHealth: utilityDrawer.openMode("health")
                onLoadRoute: routeId => {
                    root.currentIndex = 1
                    flightPage.loadSavedRoute(routeId)
                }
            }
            FlightPage {
                id: flightPage
                anchors.fill: parent
                visible: root.currentIndex === 1
                onOpenSettings: utilityDrawer.openMode("settings")
                onOpenHealth: utilityDrawer.openMode("health")
                onOpenTelemetry: utilityDrawer.openMode("telemetry")
            }
            RecordsPage {
                anchors.fill: parent
                visible: root.currentIndex === 2
                onOpenHealth: utilityDrawer.openMode("health")
            }
        }

        BottomNavigation {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            currentIndex: root.currentIndex
            onNavigate: index => root.currentIndex = index
        }
    }

    UtilityDrawer {
        id: utilityDrawer
        onFlightRequested: root.currentIndex = 1
    }

    ToastHost {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 80
        z: 20
    }
}
