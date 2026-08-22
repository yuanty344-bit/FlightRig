pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

Dialog {
    id: root

    property string planId: ""
    property string planType: "free-flight"
    property string modelId: ""
    property string worldId: ""
    property string routeId: ""
    property string endAction: "hold"
    property string acceptancePreset: "standard"
    property real takeoffAltitudeMeters: 5.0
    property real horizontalSpeedMetersPerSecond: 1.0
    property real verticalSpeedMetersPerSecond: 0.5
    property real yawRateDegreesPerSecond: 30.0
    property real maxHorizontalDeviationMeters: 8.0
    property real minimumObstacleClearanceMeters: 3.0
    property real maximumTelemetryGapSeconds: 1.0
    property real maximumAltitudeMeters: 100.0
    property real maximumDurationMinutes: 20.0
    property bool advancedExpanded: false

    readonly property bool editing: planId.length > 0
    readonly property bool routeRequired: planType === "mission"
    signal saveRequested(var draft)
    signal routeEditRequested(var draft)

    function modelOptions() {
        var options = []
        for (var i = 0; i < App.simulationModels.length; ++i) {
            var model = App.simulationModels[i]
            options.push({ label: String(model.name || model.id || "飞行器"),
                           value: String(model.id || "") })
        }
        return options
    }

    function worldOptions() {
        var options = []
        for (var i = 0; i < App.simulationWorlds.length; ++i) {
            var world = App.simulationWorlds[i]
            options.push({ label: String(world.name || world.id || "仿真世界"),
                           value: String(world.id || "") })
        }
        return options
    }

    function modelName(modelId) {
        const options = modelOptions()
        for (let i = 0; i < options.length; ++i) {
            if (String(options[i].value) === String(modelId)) return String(options[i].label)
        }
        return "选择飞行器"
    }

    function worldName(worldId) {
        const options = worldOptions()
        for (let i = 0; i < options.length; ++i) {
            if (String(options[i].value) === String(worldId)) return String(options[i].label)
        }
        return "选择仿真世界"
    }

    function routeOptions() {
        var options = [{ label: "稍后选择航线", value: "" }]
        for (var i = 0; i < App.savedRoutes.length; ++i) {
            var route = App.savedRoutes[i]
            var distance = Number(route.distanceMeters || 0)
            options.push({
                label: String(route.name || "未命名航线") + " · "
                       + Number(route.waypointCount || 0) + " 个航点"
                       + (distance > 0 ? " · " + distance.toFixed(0) + " m" : ""),
                value: String(route.id || "")
            })
        }
        return options
    }

    function applyPresetDefaults(preset) {
        if (preset === "strict") {
            maxHorizontalDeviationMeters = 3.0
            minimumObstacleClearanceMeters = 5.0
            maximumTelemetryGapSeconds = 0.8
            maximumAltitudeMeters = 100.0
            maximumDurationMinutes = 15.0
        } else if (preset === "standard") {
            maxHorizontalDeviationMeters = 8.0
            minimumObstacleClearanceMeters = 3.0
            maximumTelemetryGapSeconds = 1.0
            maximumAltitudeMeters = 100.0
            maximumDurationMinutes = 20.0
        }
    }

    function prepare(plan) {
        var value = plan || ({})
        var storedType = String(value.type || "free-flight")
        var legacyCity = storedType === "city-inspection"
        planId = String(value.id || "")
        planType = legacyCity ? "mission" : storedType
        modelId = String(value.modelId || App.selectedModelId)
        worldId = String(value.worldId || App.selectedWorldId)
        routeId = String(value.routeId || "")
        endAction = String(value.endAction || (planType === "mission" ? "land" : "hold"))
        takeoffAltitudeMeters = Number(value.takeoffAltitudeMeters || App.takeoffAltitude || 5.0)
        acceptancePreset = legacyCity ? "strict" : String(value.acceptancePreset || "standard")

        var control = value.controlSettings || ({})
        horizontalSpeedMetersPerSecond = Number(control.horizontalSpeedMetersPerSecond
                                                 || App.controlSpeedMetersPerSecond || 1.0)
        verticalSpeedMetersPerSecond = Number(control.verticalSpeedMetersPerSecond
                                               || App.controlVerticalSpeedMetersPerSecond || 0.5)
        yawRateDegreesPerSecond = Number(control.yawRateDegreesPerSecond
                                         || App.controlYawRateDegreesPerSecond || 30.0)

        var rules = value.acceptanceRules || ({})
        applyPresetDefaults(acceptancePreset === "custom" ? "standard" : acceptancePreset)
        if (rules.maxHorizontalDeviationMeters !== undefined)
            maxHorizontalDeviationMeters = Number(rules.maxHorizontalDeviationMeters)
        if (rules.minimumObstacleClearanceMeters !== undefined)
            minimumObstacleClearanceMeters = Number(rules.minimumObstacleClearanceMeters)
        if (rules.maximumTelemetryGapSeconds !== undefined)
            maximumTelemetryGapSeconds = Number(rules.maximumTelemetryGapSeconds)
        if (rules.maximumAltitudeMeters !== undefined)
            maximumAltitudeMeters = Number(rules.maximumAltitudeMeters)
        if (rules.maximumDurationSeconds !== undefined)
            maximumDurationMinutes = Number(rules.maximumDurationSeconds) / 60.0

        nameField.text = value.name ? String(value.name) : "我的测试方案"
        descriptionField.text = String(value.description || "")
        advancedExpanded = false
        open()
    }

    function draft() {
        return {
            id: planId,
            name: nameField.text,
            description: descriptionField.text,
            type: planType,
            modelId: modelId,
            worldId: worldId,
            takeoffAltitudeMeters: takeoffAltitudeMeters,
            routeId: routeRequired ? routeId : "",
            endAction: routeRequired ? endAction : "hold",
            acceptancePreset: acceptancePreset,
            controlSettings: {
                horizontalSpeedMetersPerSecond: horizontalSpeedMetersPerSecond,
                verticalSpeedMetersPerSecond: verticalSpeedMetersPerSecond,
                yawRateDegreesPerSecond: yawRateDegreesPerSecond
            },
            acceptanceRules: {
                maxHorizontalDeviationMeters: maxHorizontalDeviationMeters,
                minimumObstacleClearanceMeters: minimumObstacleClearanceMeters,
                maximumTelemetryGapSeconds: maximumTelemetryGapSeconds,
                maximumAltitudeMeters: maximumAltitudeMeters,
                maximumDurationSeconds: Math.round(maximumDurationMinutes * 60)
            }
        }
    }

    anchors.centerIn: parent
    width: Math.min(570, parent ? parent.width - 40 : 570)
    height: Math.min(600, parent ? parent.height - 32 : 600)
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: nameField.forceActiveFocus(Qt.PopupFocusReason)
    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.lineStrong
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: root.editing ? "编辑测试方案" : "创建测试方案"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTitle
                font.weight: Font.Medium
            }
            CompactIconButton {
                iconSource: "qrc:/icons/x.svg"
                accessibleLabel: "关闭测试方案编辑"
                toolTipText: "关闭"
                onClicked: root.close()
            }
        }

        ScrollView {
            id: editorScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.rightMargin: -10
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: Math.max(0, editorScroll.availableWidth - 10)
                spacing: 10

                Text { text: "方案名称"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    maximumLength: 40
                    placeholderText: "例如：校园基础巡航"
                    selectByMouse: true
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textFaint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textBody
                    leftPadding: 10
                    rightPadding: 10
                    background: Rectangle {
                        radius: Theme.radiusMedium
                        color: Theme.surface
                        border.width: nameField.activeFocus ? 2 : 1
                        border.color: nameField.activeFocus ? Theme.accent : Theme.lineStrong
                    }
                }

                Text { text: "用途说明"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                TextArea {
                    id: descriptionField
                    property int characterLimit: 120
                    Layout.fillWidth: true
                    Layout.preferredHeight: 58
                    placeholderText: "简单说明这套方案适合做什么"
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textFaint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                    leftPadding: 10; rightPadding: 10; topPadding: 8; bottomPadding: 8
                    background: Rectangle {
                        radius: Theme.radiusMedium
                        color: Theme.surface
                        border.width: descriptionField.activeFocus ? 2 : 1
                        border.color: descriptionField.activeFocus ? Theme.accent : Theme.lineStrong
                    }
                    onTextChanged: if (text.length > characterLimit) text = text.slice(0, characterLimit)
                }

                Text { text: "执行方式"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                SelectField {
                    Layout.fillWidth: true
                    model: [
                        { label: "自由飞行 · 起飞后由键盘控制", value: "free-flight" },
                        { label: "航点任务 · 自动执行一条航线", value: "mission" }
                    ]
                    selectedValue: root.planType
                    onValuePicked: value => {
                        root.planType = String(value || "free-flight")
                        if (!root.routeRequired) {
                            root.routeId = ""
                            root.endAction = "hold"
                        } else if (root.endAction === "hold") {
                            root.endAction = "land"
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 5
                        Text { text: "飞行器"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                        ActionButton {
                            Layout.fillWidth: true
                            compact: true
                            kind: "secondary"
                            text: root.modelName(root.modelId)
                            iconSource: "qrc:/icons/plane.svg"
                            toolTipText: "选择飞行器和载荷"
                            onClicked: modelPicker.showPicker(root.modelId)
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 5
                        Text { text: "仿真世界"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                        ActionButton {
                            Layout.fillWidth: true
                            compact: true
                            kind: "secondary"
                            text: root.worldName(root.worldId)
                            iconSource: "qrc:/icons/map-pinned.svg"
                            toolTipText: "查看地图并选择仿真世界"
                            onClicked: worldPicker.showPicker(root.worldId)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text { Layout.fillWidth: true; text: "起飞高度"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                    NumericField {
                        Layout.preferredWidth: 138
                        value: root.takeoffAltitudeMeters
                        from: 1.0; to: 100.0; stepSize: 0.1; decimals: 1; unit: "m"
                        accessibleName: "测试方案起飞高度"
                        onValueEdited: value => root.takeoffAltitudeMeters = value
                    }
                }

                ColumnLayout {
                    visible: root.routeRequired
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "航线与结束动作"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        SelectField {
                            Layout.fillWidth: true
                            model: root.routeOptions()
                            selectedValue: root.routeId
                            onValuePicked: value => root.routeId = String(value || "")
                        }
                        CompactIconButton {
                            iconSource: "qrc:/icons/pencil.svg"
                            accessibleLabel: "编辑选中的航线"
                            toolTipText: enabled ? accessibleLabel : "请先选择一条航线"
                            enabled: root.routeId.length > 0
                            disabledReason: "请先选择一条航线"
                            onClicked: root.routeEditRequested(root.draft())
                        }
                    }
                    SelectField {
                        Layout.fillWidth: true
                        model: [
                            { label: "完成后自动降落", value: "land" },
                            { label: "完成后返航", value: "rtl" },
                            { label: "完成后原地悬停", value: "hold" }
                        ]
                        selectedValue: root.endAction
                        onValuePicked: value => root.endAction = String(value || "land")
                    }
                    Text {
                        visible: root.routeId.length === 0
                        Layout.fillWidth: true
                        text: "可以先保存，但启动测试前必须选择一条航线。"
                        color: Theme.warning
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        wrapMode: Text.WordWrap
                    }
                }

                Text { text: "安全检查"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                SelectField {
                    Layout.fillWidth: true
                    model: [
                        { label: "标准检查 · 适合日常验证", value: "standard" },
                        { label: "严格检查 · 更小偏差和更大净空", value: "strict" },
                        { label: "自定义检查 · 使用高级阈值", value: "custom" }
                    ]
                    selectedValue: root.acceptancePreset
                    onValuePicked: value => {
                        root.acceptancePreset = String(value || "standard")
                        if (root.acceptancePreset !== "custom")
                            root.applyPresetDefaults(root.acceptancePreset)
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

                Item {
                    id: advancedToggle
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    activeFocusOnTab: true
                    Accessible.name: root.advancedExpanded ? "收起高级设置" : "展开高级设置"
                    Accessible.role: Accessible.Button
                    Keys.onReturnPressed: root.advancedExpanded = !root.advancedExpanded
                    Keys.onEnterPressed: root.advancedExpanded = !root.advancedExpanded
                    Keys.onSpacePressed: root.advancedExpanded = !root.advancedExpanded

                    RowLayout {
                        anchors.fill: parent
                        spacing: 8
                        ThemedIcon {
                            Layout.preferredWidth: 17; Layout.preferredHeight: 17
                            source: "qrc:/icons/sliders-horizontal.svg"
                            iconColor: Theme.textSecondary
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "高级设置"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.Medium
                        }
                        Text {
                            text: root.advancedExpanded ? "收起" : "控制速度与验收阈值"
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                        }
                        ThemedIcon {
                            Layout.preferredWidth: 14; Layout.preferredHeight: 14
                            source: "qrc:/icons/chevron-down.svg"
                            rotation: root.advancedExpanded ? 180 : 0
                            iconColor: Theme.textMuted
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            advancedToggle.forceActiveFocus(Qt.MouseFocusReason)
                            root.advancedExpanded = !root.advancedExpanded
                        }
                    }
                }

                ColumnLayout {
                    visible: root.advancedExpanded
                    Layout.fillWidth: true
                    spacing: 10

                    Text { text: "键盘控制速度"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            Text { text: "水平"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                            NumericField {
                                Layout.fillWidth: true
                                value: root.horizontalSpeedMetersPerSecond
                                from: 0.5; to: 2.0; stepSize: 0.1; decimals: 1; unit: "m/s"
                                accessibleName: "键盘水平速度"
                                onValueEdited: value => root.horizontalSpeedMetersPerSecond = value
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            Text { text: "垂直"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                            NumericField {
                                Layout.fillWidth: true
                                value: root.verticalSpeedMetersPerSecond
                                from: 0.2; to: 1.5; stepSize: 0.1; decimals: 1; unit: "m/s"
                                accessibleName: "键盘垂直速度"
                                onValueEdited: value => root.verticalSpeedMetersPerSecond = value
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            Text { text: "转向"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                            NumericField {
                                Layout.fillWidth: true
                                value: root.yawRateDegreesPerSecond
                                from: 5; to: 90; stepSize: 5; decimals: 0; unit: "°/s"
                                accessibleName: "键盘转向速度"
                                onValueEdited: value => root.yawRateDegreesPerSecond = value
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: "验收阈值"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                        Text {
                            visible: root.acceptancePreset !== "custom"
                            text: "选择“自定义检查”后可修改"
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8

                        Text { visible: root.routeRequired; text: "最大航线偏差"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                        NumericField { visible: root.routeRequired; Layout.preferredWidth: 150; enabled: root.acceptancePreset === "custom"; value: root.maxHorizontalDeviationMeters; from: 0.5; to: 50; stepSize: 0.5; decimals: 1; unit: "m"; accessibleName: "最大航线偏差"; onValueEdited: value => root.maxHorizontalDeviationMeters = value }
                        Text { text: "最小建筑净空"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                        NumericField { Layout.preferredWidth: 150; enabled: root.acceptancePreset === "custom"; value: root.minimumObstacleClearanceMeters; from: 0.5; to: 50; stepSize: 0.5; decimals: 1; unit: "m"; accessibleName: "最小建筑净空"; onValueEdited: value => root.minimumObstacleClearanceMeters = value }
                        Text { text: "遥测最长中断"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                        NumericField { Layout.preferredWidth: 150; enabled: root.acceptancePreset === "custom"; value: root.maximumTelemetryGapSeconds; from: 0.1; to: 10; stepSize: 0.1; decimals: 1; unit: "s"; accessibleName: "遥测最长中断时间"; onValueEdited: value => root.maximumTelemetryGapSeconds = value }
                        Text { text: "最大允许高度"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                        NumericField { Layout.preferredWidth: 150; enabled: root.acceptancePreset === "custom"; value: root.maximumAltitudeMeters; from: 1; to: 100; stepSize: 1; decimals: 0; unit: "m"; accessibleName: "最大允许高度"; onValueEdited: value => root.maximumAltitudeMeters = value }
                        Text { text: "最长测试时间"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                        NumericField { Layout.preferredWidth: 150; enabled: root.acceptancePreset === "custom"; value: root.maximumDurationMinutes; from: 1; to: 120; stepSize: 1; decimals: 0; unit: "min"; accessibleName: "最长测试时间"; onValueEdited: value => root.maximumDurationMinutes = value }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 2
            Text {
                Layout.fillWidth: true
                text: root.routeRequired && root.routeId.length === 0 ? "保存后仍需补充航线" : "方案参数会在应用时生效"
                color: root.routeRequired && root.routeId.length === 0 ? Theme.warning : Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                elide: Text.ElideRight
            }
            ActionButton { Layout.preferredWidth: 82; text: "取消"; kind: "secondary"; onClicked: root.close() }
            ActionButton {
                Layout.preferredWidth: 92
                text: root.editing ? "保存" : "创建"
                enabled: nameField.text.trim().length > 0 && root.modelId.length > 0 && root.worldId.length > 0
                toolTipText: enabled ? "保存这套测试配置" : "请填写名称并选择飞行器和世界"
                onClicked: root.saveRequested(root.draft())
            }
        }
    }

    SimulationModelPicker {
        id: modelPicker
        onModelPicked: modelId => root.modelId = String(modelId || "")
    }

    SimulationWorldPicker {
        id: worldPicker
        onWorldPicked: worldId => root.worldId = String(worldId || "")
    }
}
