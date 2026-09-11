import QtQuick
import "../../qml/features/plans"
import QtQuick.Layouts
import QtTest
import FlightBench

TestCase {
    id: testCase
    name: "AcceptanceResultRow"
    when: windowShown
    width: 960
    height: 800
    ColumnLayout {
        id: column
        width: 700
        AcceptanceResultRow {
            id: row
            Layout.fillWidth: true
            result: ({ status: "warning", label: "传感器最长连续中断时间",
                       detail: "这是包含较长中文说明的验收详情，需要在窄窗口、高 DPI 和较大字号下完整显示，不能只保留前三行，也不能在调整窗口大小时引发布局循环。最大中断时间为 0.493 秒，验收上限为 2 秒。" })
        }
    }
    function test_resizing() {
        failOnWarning(/.*(recursive rearrange|Binding loop).*/)
        for (const mode of ["light", "dark"]) {
            Theme.mode = mode
            for (const scale of [90, 100, 110]) {
                Theme.textScalePercent = scale
                for (const w of [700, 420, 419, 280, 200, 700]) {
                    column.width = w
                    wait(30)
                    compare(row.width, w)
                    const stableHeight = row.height
                    wait(30)
                    compare(row.height, stableHeight)
                    for (const child of row.children) {
                        verify(child.x >= 0 && child.x + child.width <= row.width + 1)
                        verify(child.y >= 0 && child.y + child.height <= row.height + 1)
                        if (child.truncated !== undefined) compare(child.truncated, false)
                    }
                }
            }
        }
    }
}
