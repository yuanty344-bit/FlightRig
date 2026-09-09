import QtQuick
import QtQuick.Layouts
import QtTest
import FlightBench
import "../../qml/components"

TestCase {
    id: testCase
    name: "GimbalInspectionPanel"
    when: windowShown
    visible: true
    width: 1100
    height: 1000
    ColumnLayout {
        id: column
        width: 1000
        GimbalInspectionPanel { id: panel; Layout.fillWidth: true }
    }
    function checkBounds(item) {
        if (!item.visible) return
        if (item.truncated !== undefined) compare(item.truncated, false)
        for (const child of item.children) {
            if (!child.visible || child.width <= 0 || child.height <= 0) continue
            // Internal button labels use their own padded content coordinates.
            if (String(item).indexOf("Flow") >= 0 || String(item).indexOf("Layout") >= 0) {
                verify(child.x >= -1 && child.x + child.width <= item.width + 1,
                       String(child) + " horizontally outside " + String(item))
                verify(child.y >= -1 && child.y + child.height <= item.height + 1,
                       String(child) + " vertically outside " + String(item))
            }
            checkBounds(child)
        }
    }
    function test_sizesAndHistory() {
        failOnWarning(/.*(recursive rearrange|Binding loop|Unable to assign|Cannot assign|is not defined).*/)
        for (const mode of ["light", "dark"]) {
            Theme.mode = mode
            for (const textScale of [90, 100, 110]) {
                Theme.textScalePercent = textScale
                for (const historical of [false, true]) {
                    App.gimbalInspection.historical = historical
                    for (const w of [1000, 600, 599, 400, 340]) {
                        column.width = w
                        wait(35)
                        compare(panel.width, w)
                        const stableHeight = panel.height
                        wait(35)
                        compare(panel.height, stableHeight)
                        checkBounds(panel)
                    }
                }
            }
        }
        panel.expanded = false
        wait(35)
        verify(panel.height < 80, "Collapsed height=" + panel.height + ", implicit=" + panel.implicitHeight)
    }
}
