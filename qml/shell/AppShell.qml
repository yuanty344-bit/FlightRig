import QtQuick
import QtQuick.Layouts
import FlightBench
import "../pages"

Item {
    id: root
    property int currentIndex: 0

    function openDrawer(mode) {
        utilityDrawer.openMode(mode)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
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
                onOpenHealth: utilityDrawer.openMode("health")
            }
            FlightPage {
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
            Layout.preferredHeight: 52
            currentIndex: root.currentIndex
            onNavigate: index => root.currentIndex = index
        }
    }

    UtilityDrawer {
        id: utilityDrawer
        onFlightRequested: root.currentIndex = 1
    }
}
