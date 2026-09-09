import QtQuick
import QtQuick.Layouts
import FlightBench
import "../pages"
import "../components"

Item {
    id: root
    property int currentIndex: 0
    property alias flightViewIndex: flightPage.viewIndex
    property bool awaitingTestResult: false

    function openDrawer(mode) {
        utilityDrawer.openMode(mode)
    }

    Connections {
        target: App
        function onActiveFlightSessionChanged() {
            if (App.flightSessionActive) root.awaitingTestResult = true
        }
        function onFlightSessionsChanged() {
            if (!root.awaitingTestResult || App.flightSessionActive
                    || !App.flightSessions || App.flightSessions.length === 0) return
            root.awaitingTestResult = false
            const session = App.flightSessions[0]
            if (!session || String(session.id || "") === resultSummary.lastSessionId) return
            resultSummary.lastSessionId = String(session.id || "")
            resultSummary.session = App.sessionDetails(resultSummary.lastSessionId)
            resultSummary.open()
        }
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
                id: workbenchPage
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
                onReplayClosed: {
                    App.stopSessionReplay()
                    root.currentIndex = 2
                }
            }
            RecordsPage {
                id: recordsPage
                anchors.fill: parent
                visible: root.currentIndex === 2
                onOpenHealth: utilityDrawer.openMode("health")
                onReplayRequested: sessionId => {
                    if (App.startSessionReplay(sessionId)) {
                        root.currentIndex = 1
                        flightPage.openReplay()
                    }
                }
                onReplayAtRequested: (sessionId, elapsedMs) => {
                    if (App.startSessionReplay(sessionId)) {
                        App.seekReplay(elapsedMs)
                        root.currentIndex = 1
                        flightPage.openReplay()
                    }
                }
                onReusePlanRequested: sessionId => {
                    const response = App.reuseSessionTestPlan(sessionId)
                    if (response.success)
                        root.currentIndex = 0
                }
            }
        }

        BottomNavigation {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            currentIndex: root.currentIndex
            onNavigate: index => {
                if (App.replayActive && index !== 1)
                    App.stopSessionReplay()
                root.currentIndex = index
            }
        }
    }

    UtilityDrawer {
        id: utilityDrawer
        onFlightRequested: root.currentIndex = 1
        onPlanEditRequested: {
            utilityDrawer.close()
            root.currentIndex = 0
            workbenchPage.editCurrentPlan()
        }
    }

    ToastHost {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 80
        z: 20
    }

    TestResultSummary {
        id: resultSummary
        property string lastSessionId: ""
        onRecordsRequested: root.currentIndex = 2
        onReplayRequested: sessionId => {
            if (App.startSessionReplay(sessionId)) {
                root.currentIndex = 1
                flightPage.openReplay()
            }
        }
    }
}
