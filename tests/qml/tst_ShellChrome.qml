import QtQuick
import QtQuick.Controls.Basic
import QtTest
import FlightBench
import "../../qml/controls"
import "../../qml/shell"

TestCase {
    id: testCase
    name: "ShellChrome"
    visible: true
    when: windowShown
    width: 900
    height: 650

    BottomNavigation {
        id: navigation
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 68
    }

    MotionDialog {
        id: motionDialog
        parent: testCase
        anchors.centerIn: parent
        width: 320
        height: 180
        modal: true
        contentItem: Item { }
    }

    Flickable {
        id: scrollSample
        width: 180
        height: 120
        contentWidth: width
        contentHeight: 420
        visible: false
        ScrollBar.vertical: InsetScrollBar {
            id: insetBar
            policy: ScrollBar.AlwaysOn
        }
    }

    Flickable {
        id: asNeededScrollSample
        x: -400
        width: 180
        height: 120
        contentWidth: width
        contentHeight: height
        opacity: 0
        ScrollBar.vertical: InsetScrollBar {
            id: asNeededBar
            policy: ScrollBar.AsNeeded
        }
    }

    PanelCloseButton {
        id: closeButton
        visible: false
    }

    SignalSpy {
        id: navigationSpy
        target: navigation
        signalName: "navigate"
    }

    function init() {
        failOnWarning(/.*(recursive rearrange|Binding loop|Unable to assign|is not defined).*/)
        navigation.currentIndex = 0
        navigationSpy.clear()
        Theme.reducedMotion = false
    }

    function cleanup() {
        motionDialog.close()
        tryCompare(motionDialog, "visible", false)
        Theme.reducedMotion = false
    }

    function test_navigationDockFitsAndMoves() {
        const dock = findChild(navigation, "navigationDock")
        const pill = findChild(navigation, "navigationSelectionPill")
        const records = findChild(navigation, "recordsNavigation")
        verify(dock !== null)
        verify(pill !== null)
        verify(records !== null)

        for (const availableWidth of [760, 900, 1280]) {
            testCase.width = availableWidth
            wait(20)
            verify(dock.width <= availableWidth - 32)
            verify(dock.x >= 0)
            verify(dock.x + dock.width <= availableWidth)
        }

        mouseClick(records)
        compare(navigationSpy.count, 1)
        compare(navigationSpy.signalArguments[0][0], 2)
        navigation.currentIndex = 2
        wait(Theme.motionSlow + 30)
        verify(Math.abs(pill.x - (4 + 2 * (dock.cellWidth + 4))) < 0.5)
    }

    function test_dialogMotionAndReducedMotion() {
        motionDialog.open()
        tryCompare(motionDialog, "opened", true)
        tryCompare(motionDialog, "opacity", 1)
        motionDialog.close()
        tryCompare(motionDialog, "visible", false)

        Theme.reducedMotion = true
        motionDialog.open()
        tryCompare(motionDialog, "opened", true)
        compare(motionDialog.opacity, 1)
        compare(motionDialog.scale, 1)
    }

    function test_edgeControlStandards() {
        compare(insetBar.width, Theme.scrollBarExtent)
        verify(insetBar.contentItem.x >= Theme.scrollBarEdgeGap)
        verify(insetBar.width - insetBar.contentItem.x - insetBar.contentItem.width
               >= Theme.scrollBarEdgeGap)
        compare(closeButton.implicitWidth, Theme.closeButtonExtent)
        compare(closeButton.implicitHeight, Theme.closeButtonExtent)
        verify(closeButton.outlined)
        verify(closeButton.iconSource.toString().indexOf("x.svg") >= 0)
    }

    function test_asNeededScrollbarOnlyPaintsForOverflow() {
        asNeededScrollSample.contentHeight = asNeededScrollSample.height
        wait(0)
        compare(asNeededBar.visible, false)

        asNeededScrollSample.contentHeight = asNeededScrollSample.height + 120
        tryCompare(asNeededBar, "visible", true)

        asNeededScrollSample.contentHeight = asNeededScrollSample.height
        tryCompare(asNeededBar, "visible", false)
    }
}
