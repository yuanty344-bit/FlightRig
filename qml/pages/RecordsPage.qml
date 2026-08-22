pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench
import "../components"

Item {
    id: root
    readonly property bool compactLayout: width < 860 || height < 500
    property string selectedSessionId: ""
    property var selectedSession: ({})
    property string exportPath: ""
    property string modelFilter: ""
    property string worldFilter: ""
    property string resultFilter: ""
    property int dateFilterDays: 0
    property var filteredSessions: []
    property var modelFilterOptions: [{ label: "全部机型", value: "" }]
    property var worldFilterOptions: [{ label: "全部场景", value: "" }]
    readonly property bool filtersActive: modelFilter.length > 0
                                          || worldFilter.length > 0
                                          || resultFilter.length > 0
                                          || dateFilterDays > 0
    signal openHealth()
    signal replayRequested(string sessionId)
    signal reusePlanRequested(string sessionId)

    function numberText(value, suffix) {
        return Number(value || 0).toFixed(1) + suffix
    }

    function statusColor(status) {
        if (status === "记录中") return Theme.accent
        if (status === "有异常" || status === "未完成") return Theme.warning
        return Theme.success
    }

    function resultColor(result) {
        if (result === "passed") return Theme.success
        if (result === "warning" || result === "review" || result === "incomplete") return Theme.warning
        if (result === "failed" || result === "aborted") return Theme.danger
        if (result === "running") return Theme.accent
        return Theme.textMuted
    }

    function selectedWorldText() {
        const name = String(selectedSession.worldName || "")
        return name.length > 0 ? name : "旧记录未保存"
    }

    function sessionTitle(session) {
        const customName = String(session.displayName || "").trim()
        if (customName.length > 0) return customName
        const planName = String(session.testPlanName || "").trim()
        if (planName.length > 0) return planName
        const routeName = String(session.missionRouteName || "").trim()
        if (routeName.length > 0) return routeName
        return String(session.modelName || "飞行会话")
    }

    function modelKey(session) {
        return String(session.modelId || session.modelName || "")
    }

    function worldKey(session) {
        return String(session.worldId || session.worldName || "__unknown__")
    }

    function dateMatches(session) {
        if (dateFilterDays <= 0) return true
        const started = new Date(String(session.startedAt || ""))
        if (isNaN(started.getTime())) return false
        const now = new Date()
        if (dateFilterDays === 1) {
            return started.getFullYear() === now.getFullYear()
                    && started.getMonth() === now.getMonth()
                    && started.getDate() === now.getDate()
        }
        return started.getTime() >= now.getTime() - dateFilterDays * 86400000
    }

    function rebuildFilters() {
        const sessions = App.flightSessions || []
        const modelOptions = [{ label: "全部机型", value: "" }]
        const worldOptions = [{ label: "全部场景", value: "" }]
        const seenModels = ({})
        const seenWorlds = ({})
        for (let index = 0; index < sessions.length; ++index) {
            const session = sessions[index]
            const modelValue = modelKey(session)
            if (modelValue.length > 0 && !seenModels[modelValue]) {
                seenModels[modelValue] = true
                modelOptions.push({ label: String(session.modelName || modelValue),
                                    value: modelValue })
            }
            const worldValue = worldKey(session)
            if (worldValue.length > 0 && !seenWorlds[worldValue]) {
                seenWorlds[worldValue] = true
                worldOptions.push({ label: worldValue === "__unknown__"
                                           ? "旧记录未保存"
                                           : String(session.worldName || worldValue),
                                    value: worldValue })
            }
        }
        modelFilterOptions = modelOptions
        worldFilterOptions = worldOptions

        const result = []
        for (let index = 0; index < sessions.length; ++index) {
            const session = sessions[index]
            if (modelFilter.length > 0 && modelKey(session) !== modelFilter) continue
            if (worldFilter.length > 0 && worldKey(session) !== worldFilter) continue
            if (resultFilter.length > 0
                    && String(session.testResult || "not-rated") !== resultFilter) continue
            if (!dateMatches(session)) continue
            result.push(session)
        }
        filteredSessions = result
        Qt.callLater(ensureSelection)
    }

    function clearFilters() {
        modelFilter = ""
        worldFilter = ""
        resultFilter = ""
        dateFilterDays = 0
    }

    function selectSession(sessionId) {
        if (!sessionId || sessionId.length === 0) return
        selectedSessionId = sessionId
        selectedSession = App.sessionDetails(sessionId)
        exportPath = ""
    }

    function ensureSelection() {
        if (App.flightSessionActive) {
            selectSession(String(App.activeFlightSession.id || ""))
            return
        }
        if (filteredSessions.length > 0) {
            let exists = false
            for (let i = 0; i < filteredSessions.length; ++i) {
                if (String(filteredSessions[i].id || "") === selectedSessionId) {
                    exists = true
                    break
                }
            }
            if (!exists) selectSession(String(filteredSessions[0].id || ""))
        } else {
            selectedSessionId = ""
            selectedSession = ({})
        }
    }

    onModelFilterChanged: rebuildFilters()
    onWorldFilterChanged: rebuildFilters()
    onResultFilterChanged: rebuildFilters()
    onDateFilterDaysChanged: rebuildFilters()
    Component.onCompleted: rebuildFilters()

    Connections {
        target: App
        function onActiveFlightSessionChanged() { root.ensureSelection() }
        function onFlightSessionsChanged() {
            root.rebuildFilters()
            if (root.selectedSessionId.length > 0)
                root.selectedSession = App.sessionDetails(root.selectedSessionId)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compactLayout ? 20 : 28
        anchors.rightMargin: root.compactLayout ? 20 : 28
        anchors.topMargin: root.compactLayout ? 12 : 20
        anchors.bottomMargin: root.compactLayout ? 10 : 18
        spacing: root.compactLayout ? 10 : 14

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compactLayout ? 50 : 58
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: "记录"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: 24
                    font.weight: Font.Medium
                }
                Text {
                    Layout.fillWidth: true
                    text: "每次仿真自动保存为一个会话"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                }
            }
            CompactIconButton {
                iconSource: "qrc:/icons/archive-restore.svg"
                accessibleLabel: "打开会话回收站"
                toolTipText: App.trashSessionCount > 0 ? "会话回收站（" + App.trashSessionCount + "）" : "会话回收站"
                outlined: App.trashSessionCount > 0
                onClicked: trashDialog.open()
            }
            CompactIconButton {
                iconSource: "qrc:/icons/sliders-horizontal.svg"
                accessibleLabel: "筛选飞行会话"
                toolTipText: root.filtersActive ? "已筛选会话" : "筛选会话"
                outlined: root.filtersActive
                normalIconColor: root.filtersActive ? Theme.accent : Theme.textSecondary
                onClicked: filterDialog.open()
            }
            ActionButton {
                Layout.preferredWidth: 114
                compact: true
                text: "保存位置"
                iconSource: "qrc:/icons/folder-open.svg"
                kind: "secondary"
                onClicked: App.openSessionsFolder()
            }
        }

        SurfaceCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    Layout.preferredWidth: Math.max(220, Math.min(300, parent.width * 0.32))
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 42
                            Layout.leftMargin: 16
                            Layout.rightMargin: 14
                            Text {
                                Layout.fillWidth: true
                                text: root.filtersActive ? "筛选结果" : "最近会话"
                                color: Theme.textMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                            }
                            Text {
                                text: root.filteredSessions.length + " / " + App.flightSessions.length
                                color: root.filtersActive ? Theme.accent : Theme.textFaint
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.textTiny
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

                        Item {
                            id: activeSessionRow
                            visible: App.flightSessionActive
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? 72 : 0
                            activeFocusOnTab: true
                            Accessible.name: "当前会话"
                            Accessible.role: Accessible.Button
                            Keys.onReturnPressed: root.selectSession(String(App.activeFlightSession.id || ""))
                            Keys.onEnterPressed: root.selectSession(String(App.activeFlightSession.id || ""))
                            Keys.onSpacePressed: root.selectSession(String(App.activeFlightSession.id || ""))
                            Rectangle {
                                anchors.fill: parent
                                color: root.selectedSessionId === String(App.activeFlightSession.id || "")
                                       ? Theme.accentSoft : activeMouse.containsMouse ? Theme.surfaceRaised : "transparent"
                            }
                            Rectangle {
                                visible: activeSessionRow.activeFocus
                                anchors.fill: parent
                                radius: Theme.radiusSmall
                                color: "transparent"
                                border.width: 1
                                border.color: Theme.accent
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 14
                                spacing: 10
                                Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: Theme.accent }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text { text: "当前会话"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                                    Text { text: String(App.activeFlightSession.durationLabel || "00:00") + " · 正在记录"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                                }
                            }
                            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
                            MouseArea {
                                id: activeMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.selectSession(String(App.activeFlightSession.id || ""))
                                    root.forceActiveFocus(Qt.MouseFocusReason)
                                }
                            }
                        }

                        ListView {
                            id: sessionList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: root.filteredSessions.length > 0
                            clip: true
                            model: root.filteredSessions
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Item {
                                id: sessionRow
                                required property int index
                                required property var modelData
                                width: sessionList.width
                                height: 74
                                activeFocusOnTab: true
                                Accessible.name: root.sessionTitle(modelData)
                                Accessible.role: Accessible.Button
                                Keys.onReturnPressed: root.selectSession(String(modelData.id || ""))
                                Keys.onEnterPressed: root.selectSession(String(modelData.id || ""))
                                Keys.onSpacePressed: root.selectSession(String(modelData.id || ""))

                                Rectangle {
                                    anchors.fill: parent
                                    color: root.selectedSessionId === String(sessionRow.modelData.id || "")
                                           ? Theme.accentSoft : rowMouse.containsMouse ? Theme.surfaceRaised : "transparent"
                                }
                                Rectangle {
                                    visible: sessionRow.activeFocus
                                    anchors.fill: parent
                                    radius: Theme.radiusSmall
                                    color: "transparent"
                                    border.width: 1
                                    border.color: Theme.accent
                                }
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 16
                                    anchors.rightMargin: 14
                                    spacing: 3
                                    Item { Layout.fillHeight: true }
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.sessionTitle(sessionRow.modelData)
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textSmall
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: String(sessionRow.modelData.startedLabel || "") + " · "
                                              + String(sessionRow.modelData.durationLabel || "00:00") + " · "
                                              + String(sessionRow.modelData.testResultLabel || "未评估")
                                        color: root.resultColor(String(sessionRow.modelData.testResult || "not-rated"))
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textTiny
                                        elide: Text.ElideRight
                                    }
                                    Item { Layout.fillHeight: true }
                                }
                                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
                                MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { root.selectSession(String(sessionRow.modelData.id || "")); root.forceActiveFocus(Qt.MouseFocusReason) } }
                            }
                        }

                        Column {
                            visible: !App.flightSessionActive && root.filteredSessions.length === 0
                            Layout.alignment: Qt.AlignCenter
                            spacing: 6
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: root.filtersActive ? "没有符合条件的会话" : "还没有飞行记录"
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSmall
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: root.filtersActive ? "可在筛选中清除条件" : "启动一次仿真后会自动创建"
                                color: Theme.textFaint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                            }
                        }
                    }

                    Rectangle { anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right; width: 1; color: Theme.line }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Flickable {
                        id: sessionDetailsFlickable
                        visible: root.selectedSessionId.length > 0
                        anchors.fill: parent
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: width
                        contentHeight: sessionDetailsColumn.y
                                       + sessionDetailsColumn.implicitHeight
                                       + (root.compactLayout ? 12 : 20)
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: sessionDetailsColumn
                        width: Math.max(0, sessionDetailsFlickable.width - (root.compactLayout ? 32 : 48))
                        x: root.compactLayout ? 16 : 24
                        y: root.compactLayout ? 12 : 20
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 56
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    text: root.sessionTitle(root.selectedSession)
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textTitle
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: String(root.selectedSession.modelName || "") + " · "
                                          + String(root.selectedSession.testResultLabel || root.selectedSession.status || "未评估")
                                    color: root.resultColor(String(root.selectedSession.testResult || "not-rated"))
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textSmall
                                    elide: Text.ElideRight
                                }
                            }
                            CompactIconButton {
                                iconSource: "qrc:/icons/pencil.svg"
                                accessibleLabel: "编辑会话名称和备注"
                                toolTipText: "编辑名称和备注"
                                onClicked: {
                                    editSessionDialog.prepare(
                                        String(root.selectedSession.displayName || ""),
                                        String(root.selectedSession.notes || ""),
                                        root.sessionTitle(root.selectedSession))
                                }
                            }
                            CompactIconButton {
                                visible: !root.compactLayout
                                         && root.selectedSessionId !== String(App.activeFlightSession.id || "")
                                iconSource: "qrc:/icons/history.svg"
                                accessibleLabel: "复用本次测试方案"
                                toolTipText: "复用本次方案"
                                enabled: !App.simulationRunning && !App.flightSessionActive
                                disabledReason: "停止当前仿真后才能复用方案"
                                onClicked: root.reusePlanRequested(root.selectedSessionId)
                            }
                            CompactIconButton {
                                iconSource: "qrc:/icons/play.svg"
                                accessibleLabel: "回放当前飞行会话"
                                toolTipText: root.selectedSession.replayAvailable
                                             ? "回放飞行" : "该会话缺少位置数据"
                                enabled: Boolean(root.selectedSession.replayAvailable)
                                         && !App.simulationRunning && !App.flightSessionActive
                                disabledReason: !Boolean(root.selectedSession.replayAvailable)
                                                ? "该会话缺少可回放的位置数据"
                                                : "停止当前仿真后才能回放"
                                onClicked: root.replayRequested(root.selectedSessionId)
                            }
                            CompactIconButton {
                                iconSource: "qrc:/icons/file-chart-column.svg"
                                accessibleLabel: "生成测试报告"
                                toolTipText: "生成并打开 HTML 报告"
                                onClicked: root.exportPath = App.generateSessionReport(root.selectedSessionId)
                            }
                            CompactIconButton {
                                id: moreSessionActionsButton
                                visible: root.compactLayout
                                iconSource: "qrc:/icons/ellipsis.svg"
                                accessibleLabel: "更多会话操作"
                                toolTipText: "更多操作"
                                onClicked: sessionActionsPopup.open()
                            }
                            CompactIconButton {
                                visible: !root.compactLayout
                                iconSource: "qrc:/icons/package.svg"
                                accessibleLabel: "导出完整会话包"
                                toolTipText: "导出报告和原始数据包"
                                onClicked: root.exportPath = App.exportSessionBundle(root.selectedSessionId)
                            }
                            CompactIconButton {
                                visible: !root.compactLayout
                                iconSource: "qrc:/icons/folder-open.svg"
                                accessibleLabel: "打开当前会话目录"
                                toolTipText: "打开目录"
                                onClicked: App.openSessionFolder(root.selectedSessionId)
                            }
                            CompactIconButton {
                                visible: !root.compactLayout
                                iconSource: "qrc:/icons/file-down.svg"
                                accessibleLabel: "导出当前会话 CSV"
                                toolTipText: "导出 CSV"
                                onClicked: root.exportPath = App.exportSessionCsv(root.selectedSessionId)
                            }
                            CompactIconButton {
                                visible: !root.compactLayout
                                         && root.selectedSessionId !== String(App.activeFlightSession.id || "")
                                iconSource: "qrc:/icons/trash-2.svg"
                                accessibleLabel: "删除当前飞行会话"
                                toolTipText: "移到会话回收站"
                                danger: true
                                onClicked: deleteSessionDialog.open()
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 78
                            Layout.minimumHeight: 78
                            Layout.maximumHeight: 78
                            spacing: 0

                            component SummaryValue: Item {
                                id: summaryValue
                                property string label: "摘要"
                                property string value: "—"
                                property bool divider: false
                                property bool monospace: true
                                property color valueColor: Theme.textPrimary
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Column {
                                    anchors.left: parent.left
                                    anchors.leftMargin: summaryValue.divider ? 18 : 0
                                    anchors.right: parent.right
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 7
                                    Text { text: summaryValue.label; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                                    Text {
                                        width: Math.max(0, parent.width - (summaryValue.divider ? 18 : 0))
                                        text: summaryValue.value
                                        color: summaryValue.valueColor
                                        font.family: summaryValue.monospace ? Theme.monoFontFamily : Theme.fontFamily
                                        font.pixelSize: root.compactLayout ? 16 : 18
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                }
                                Rectangle { visible: summaryValue.divider; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: Theme.line }
                            }

                            SummaryValue {
                                label: "测试结论"
                                value: String(root.selectedSession.testResultLabel || "未评估")
                                monospace: false
                                valueColor: root.resultColor(String(root.selectedSession.testResult || "not-rated"))
                            }
                            SummaryValue { label: "持续时间"; value: String(root.selectedSession.durationLabel || "00:00"); divider: true }
                            SummaryValue { label: "最大高度"; value: root.numberText(root.selectedSession.maxAltitude, " m"); divider: true }
                            SummaryValue { label: "最大速度"; value: root.numberText(root.selectedSession.maxHorizontalSpeed, " m/s"); divider: true }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 54
                            spacing: 18

                            component DetailValue: ColumnLayout {
                                id: detailValue
                                property string label: "详情"
                                property string value: "—"
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    text: detailValue.label
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textTiny
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: detailValue.value
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textSmall
                                    elide: Text.ElideRight
                                }
                            }

                            DetailValue { label: "测试方案"; value: String(root.selectedSession.testPlanName || "旧会话未保存") }
                            DetailValue { label: "仿真场景"; value: root.selectedWorldText() }
                            DetailValue { label: "健康检查"; value: String(root.selectedSession.healthResult || "未记录") }
                            DetailValue { label: "异常事件"; value: Number(root.selectedSession.issueCount || 0) + " 项" }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
                        RouteDeviationSummary {
                            visible: Boolean(root.selectedSession.routeAnalysis
                                             && root.selectedSession.routeAnalysis.available)
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? implicitHeight : 0
                            Layout.topMargin: visible ? 12 : 0
                            analysis: root.selectedSession.routeAnalysis || ({})
                        }
                        Rectangle {
                            visible: String(root.selectedSession.testResultReason || "").length > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? resultReasonText.implicitHeight + 18 : 0
                            Layout.topMargin: visible ? 12 : 0
                            radius: Theme.radiusMedium
                            color: Theme.surfaceRaised
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: 7
                                    Layout.preferredHeight: 7
                                    radius: 4
                                    color: root.resultColor(String(root.selectedSession.testResult || "not-rated"))
                                }
                                Text {
                                    id: resultReasonText
                                    Layout.fillWidth: true
                                    text: String(root.selectedSession.testResultReason || "")
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textSmall
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                        Rectangle {
                            visible: String(root.selectedSession.notes || "").length > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? Math.min(70, notesText.implicitHeight + 20) : 0
                            Layout.topMargin: visible ? 12 : 0
                            radius: Theme.radiusMedium
                            color: Theme.surfaceRaised
                            Text {
                                id: notesText
                                anchors.fill: parent
                                anchors.margins: 10
                                text: String(root.selectedSession.notes || "")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textSmall
                                wrapMode: Text.WordWrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        ColumnLayout {
                            visible: Boolean(root.selectedSession.acceptanceChecks
                                             && root.selectedSession.acceptanceChecks.length > 0)
                            Layout.fillWidth: true
                            Layout.topMargin: visible ? 12 : 0
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Text { Layout.fillWidth: true; text: "验收明细"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                                Text {
                                    text: Number(root.selectedSession.acceptancePassedCount || 0) + " 通过 · "
                                          + Number(root.selectedSession.acceptanceWarningCount || 0) + " 提醒 · "
                                          + Number(root.selectedSession.acceptanceFailedCount || 0) + " 未通过"
                                    color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny
                                }
                            }
                            Repeater {
                                model: root.selectedSession.acceptanceChecks || []
                                delegate: AcceptanceResultRow { required property var modelData; Layout.fillWidth: true; result: modelData }
                            }
                        }
                        Item { Layout.preferredHeight: 18 }

                        Text {
                            text: "会话事件"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.Medium
                        }
                        Item { Layout.preferredHeight: 10 }

                        ListView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(276, Math.max(46, contentHeight))
                            clip: true
                            model: root.selectedSession.events || []
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Item {
                                id: eventRow

                                required property int index
                                required property string modelData
                                width: ListView.view.width
                                height: 46
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.rightMargin: 12
                                    spacing: 12
                                    Rectangle {
                                        Layout.preferredWidth: 7
                                        Layout.preferredHeight: 7
                                        radius: 4
                                        color: eventRow.index === 0 ? Theme.accent : Theme.success
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: eventRow.modelData
                                        color: Theme.textSecondary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textSmall
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Text {
                            visible: !root.selectedSession.events || root.selectedSession.events.length === 0
                            Layout.fillWidth: true
                            Layout.topMargin: 18
                            text: "暂无会话事件"
                            color: Theme.textFaint
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                        }

                        Text {
                            visible: root.exportPath.length > 0
                            Layout.fillWidth: true
                            text: "已导出到 " + root.exportPath
                            color: Theme.textFaint
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            elide: Text.ElideMiddle
                        }
                    }
                    }

                    Text {
                        visible: root.selectedSessionId.length === 0
                        anchors.centerIn: parent
                        text: "选择一个会话查看详情"
                        color: Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                    }
                }
            }
        }
    }

    SessionFilterDialog {
        id: filterDialog
        modelOptions: root.modelFilterOptions
        worldOptions: root.worldFilterOptions
        modelValue: root.modelFilter
        worldValue: root.worldFilter
        resultValue: root.resultFilter
        dateDays: root.dateFilterDays
        filteredCount: root.filteredSessions.length
        totalCount: App.flightSessions.length
        onModelSelected: value => root.modelFilter = value
        onWorldSelected: value => root.worldFilter = value
        onResultSelected: value => root.resultFilter = value
        onDateSelected: days => root.dateFilterDays = days
        onClearRequested: root.clearFilters()
    }

    SessionEditDialog {
        id: editSessionDialog
        onSaveRequested: (displayName, notes) => {
            if (App.updateSessionDetails(root.selectedSessionId, displayName, notes)) {
                root.selectedSession = App.sessionDetails(root.selectedSessionId)
                editSessionDialog.close()
            }
        }
    }

    SessionTrashDialog { id: trashDialog }

    Popup {
        id: sessionActionsPopup
        parent: root
        x: Math.max(12, moreSessionActionsButton.mapToItem(root, moreSessionActionsButton.width, 0).x
                        - width)
        y: Math.min(root.height - height - 12,
                    moreSessionActionsButton.mapToItem(root, 0, moreSessionActionsButton.height).y + 6)
        width: Math.min(220, root.width - 24)
        padding: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: Theme.radiusMedium
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.lineStrong
        }
        contentItem: ColumnLayout {
            spacing: 4
            ActionButton {
                visible: root.selectedSessionId !== String(App.activeFlightSession.id || "")
                Layout.fillWidth: true
                text: "复用本次方案"
                iconSource: "qrc:/icons/history.svg"
                kind: "secondary"
                enabled: !App.simulationRunning && !App.flightSessionActive
                disabledReason: "停止当前仿真后才能复用方案"
                onClicked: {
                    sessionActionsPopup.close()
                    root.reusePlanRequested(root.selectedSessionId)
                }
            }
            ActionButton {
                Layout.fillWidth: true
                text: "导出完整会话包"
                iconSource: "qrc:/icons/package.svg"
                kind: "secondary"
                onClicked: {
                    sessionActionsPopup.close()
                    root.exportPath = App.exportSessionBundle(root.selectedSessionId)
                }
            }
            ActionButton {
                Layout.fillWidth: true
                text: "打开会话目录"
                iconSource: "qrc:/icons/folder-open.svg"
                kind: "secondary"
                onClicked: {
                    sessionActionsPopup.close()
                    App.openSessionFolder(root.selectedSessionId)
                }
            }
            ActionButton {
                Layout.fillWidth: true
                text: "导出遥测 CSV"
                iconSource: "qrc:/icons/file-down.svg"
                kind: "secondary"
                onClicked: {
                    sessionActionsPopup.close()
                    root.exportPath = App.exportSessionCsv(root.selectedSessionId)
                }
            }
            ActionButton {
                visible: root.selectedSessionId !== String(App.activeFlightSession.id || "")
                Layout.fillWidth: true
                text: "移到回收站"
                iconSource: "qrc:/icons/trash-2.svg"
                kind: "danger"
                onClicked: {
                    sessionActionsPopup.close()
                    deleteSessionDialog.open()
                }
            }
        }
    }

    Dialog {
        id: deleteSessionDialog
        anchors.centerIn: parent
        width: Math.min(410, root.width - 40)
        modal: true
        padding: 20
        background: Rectangle {
            radius: Theme.radiusLarge
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.lineStrong
        }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: "移到会话回收站？"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTitle
                font.weight: Font.Medium
            }
            Text {
                Layout.fillWidth: true
                text: "会话将保留 30 天。期间可以从记录页的回收站恢复。"
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textBody
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: root.sessionTitle(root.selectedSession)
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                elide: Text.ElideMiddle
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    Layout.preferredWidth: 82
                    text: "取消"
                    kind: "secondary"
                    onClicked: deleteSessionDialog.close()
                }
                ActionButton {
                    Layout.preferredWidth: 92
                    text: "移到回收站"
                    kind: "danger"
                    onClicked: {
                        const sessionId = root.selectedSessionId
                        root.selectedSessionId = ""
                        root.selectedSession = ({})
                        if (App.deleteSession(sessionId)) {
                            root.exportPath = ""
                            deleteSessionDialog.close()
                            Qt.callLater(root.ensureSelection)
                        } else {
                            root.selectSession(sessionId)
                        }
                    }
                }
            }
        }
    }
}
