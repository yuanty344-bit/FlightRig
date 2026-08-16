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
        anchors.fill: parent
        spacing: 0

        TopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            onEnvironmentRequested: utilityDrawer.openMode("environment")
            onSettingsRequested: utilityDrawer.openMode("settings")
            onRefreshRequested: App.refresh()
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentIndex

            WorkbenchPage {
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
                onOpenSettings: utilityDrawer.openMode("settings")
                onOpenHealth: utilityDrawer.openMode("health")
                onOpenTelemetry: utilityDrawer.openMode("telemetry")
            }
            RecordsPage {
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
