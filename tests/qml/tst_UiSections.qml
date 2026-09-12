import QtQuick
import QtQuick.Layouts
import QtTest
import FlightBench
import "../../qml/controls"
import "../../qml/features/settings"
import "../../qml/features/records"

TestCase {
    id: testCase
    name: "UiSections"
    visible: true
    when: windowShown
    width: 700
    height: 700

    SettingsAppearancePage {
        id: appearance
        width: 400
        height: 420
    }
    ColumnLayout {
        id: results
        y: 430
        width: 400
        SessionAcceptanceSection { id: acceptance; session: ({}) }
    }
    SignalSpy { id: returnSpy; target: appearance; signalName: "returnRequested" }
    ToastHost {
        id: toast
        acceptingMessages: true
    }

    function init() {
        failOnWarning(/.*(recursive rearrange|Binding loop|Unable to assign|is not defined).*/)
    }
    function test_navigation() {
        returnSpy.clear()
        const column = appearance.contentChildren[0]
        const header = column.children[0]
        mouseClick(header.children[0])
        compare(returnSpy.count, 1)
    }
    function test_responsiveSections() {
        for (const mode of ["light", "dark"]) {
            Theme.mode = mode
            for (const panelWidth of [340, 400, 600]) {
                appearance.width = panelWidth
                results.width = panelWidth
                acceptance.session = ({})
                wait(20)
                compare(acceptance.visible, false)
                acceptance.session = {
                    acceptancePassedCount: 1,
                    acceptanceWarningCount: 0,
                    acceptanceFailedCount: 0,
                    acceptanceChecks: [{label: "测试结果", status: "pass", detail: "历史飞行记录验收说明"}]
                }
                wait(30)
                verify(acceptance.visible)
                compare(acceptance.width, panelWidth)
                verify(acceptance.implicitHeight > 0)
                verify(appearance.contentWidth <= appearance.availableWidth)
            }
        }
    }
    function test_toastShowsCompleteMessage() {
        toast.width = 240
        toast.message = "PX4 起飞检查通过，正在确认状态稳定…"
        toast.opacity = 1
        wait(20)
        const textItem = findChild(toast, "toastText")
        verify(textItem !== null)
        compare(textItem.text, "PX4 起飞检查通过，正在确认状态稳定")
        compare(textItem.elide, Text.ElideNone)
        verify(!textItem.truncated)
        verify(textItem.lineCount > 1)
        verify(toast.height >= textItem.contentHeight + 18)
    }
}
