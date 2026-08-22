pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import FlightBench

Item {
    id: root
    property int pageIndex: 0
    readonly property int controlColumnWidth: 190
    property int pendingColorRed: 37
    property int pendingColorGreen: 99
    property int pendingColorBlue: 235
    property real pendingColorHue: 0.62
    property real pendingColorSaturation: 0.84
    property real pendingColorValue: 0.92

    function openPage(index) { pageIndex = index }
    function returnHome() { pageIndex = 0 }
    function themeLabel() {
        return Theme.mode === "dark" ? "深色" : Theme.mode === "system" ? "跟随系统" : "浅色"
    }
    function cameraLabel() {
        return App.defaultCameraMode === "free" ? "自由视角"
             : App.defaultCameraMode === "follow" ? "跟随无人机" : "兼顾无人机与地面"
    }
    function pathLengthLabel() {
        return App.flightPathMaxVisibleLengthMeters === 0
               ? "全部" : App.flightPathMaxVisibleLengthMeters + " m"
    }
    function twoDigitHex(value) {
        const text = Math.max(0, Math.min(255, Math.round(value))).toString(16).toUpperCase()
        return text.length < 2 ? "0" + text : text
    }
    function pendingColorHex() {
        return "#" + twoDigitHex(pendingColorRed)
                + twoDigitHex(pendingColorGreen)
                + twoDigitHex(pendingColorBlue)
    }
    function loadPendingColor(value) {
        let text = String(value).toUpperCase()
        if (text.length === 9 && text.charAt(0) === "#")
            text = "#" + text.slice(3)
        if (!/^#[0-9A-F]{6}$/.test(text))
            return
        pendingColorRed = parseInt(text.slice(1, 3), 16)
        pendingColorGreen = parseInt(text.slice(3, 5), 16)
        pendingColorBlue = parseInt(text.slice(5, 7), 16)
        updatePendingHsvFromRgb()
    }
    function updatePendingRgbFromHsv() {
        const color = Qt.hsva(pendingColorHue,
                              pendingColorSaturation,
                              pendingColorValue, 1)
        pendingColorRed = Math.round(color.r * 255)
        pendingColorGreen = Math.round(color.g * 255)
        pendingColorBlue = Math.round(color.b * 255)
    }
    function updatePendingHsvFromRgb() {
        const red = pendingColorRed / 255
        const green = pendingColorGreen / 255
        const blue = pendingColorBlue / 255
        const maximum = Math.max(red, green, blue)
        const minimum = Math.min(red, green, blue)
        const difference = maximum - minimum
        let hue = 0
        if (difference > 0) {
            if (maximum === red)
                hue = ((green - blue) / difference) % 6
            else if (maximum === green)
                hue = (blue - red) / difference + 2
            else
                hue = (red - green) / difference + 4
            hue /= 6
            if (hue < 0)
                hue += 1
        }
        pendingColorHue = hue
        pendingColorSaturation = maximum === 0 ? 0 : difference / maximum
        pendingColorValue = maximum
    }
    function setPendingRgb(channel, value) {
        const normalized = Math.max(0, Math.min(255, Math.round(value)))
        if (channel === "r")
            pendingColorRed = normalized
        else if (channel === "g")
            pendingColorGreen = normalized
        else
            pendingColorBlue = normalized
        updatePendingHsvFromRgb()
    }

    component PageHeader: RowLayout {
        required property string title
        property string subtitle: ""
        Layout.fillWidth: true
        Layout.preferredHeight: subtitle.length > 0 ? 58 : 44
        spacing: 8
        CompactIconButton {
            iconSource: "qrc:/icons/chevron-down.svg"
            iconRotation: 90
            accessibleLabel: "返回设置"
            toolTipText: "返回"
            onClicked: root.returnHome()
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 2
            Text {
                text: title
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSubtitle
                font.weight: Font.DemiBold
            }
            Text {
                visible: subtitle.length > 0
                Layout.fillWidth: true
                text: subtitle
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                elide: Text.ElideRight
            }
        }
    }

    component SectionLabel: Item {
        required property string text
        Layout.fillWidth: true
        Layout.preferredHeight: 34
        Text {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 7
            text: parent.text
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textTiny
            font.weight: Font.Medium
        }
    }

    component CategoryRow: Rectangle {
        id: category
        required property string title
        required property string summary
        required property url iconSource
        property int targetPage: 0
        signal activated()
        Layout.fillWidth: true
        Layout.preferredHeight: 62
        radius: Theme.radiusMedium
        color: categoryMouse.containsMouse ? Theme.surfaceInteractive : Theme.surfaceRaised
        border.width: 1
        border.color: categoryMouse.containsMouse ? Theme.lineStrong : Theme.line
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 10
            spacing: 11
            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                radius: Theme.radiusMedium
                color: Theme.accentSoft
                ThemedIcon {
                    anchors.centerIn: parent
                    width: 17; height: 17
                    source: category.iconSource
                    iconColor: Theme.accent
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: category.title; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium }
                Text { Layout.fillWidth: true; text: category.summary; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideRight }
            }
            ThemedIcon { Layout.preferredWidth: 14; Layout.preferredHeight: 14; source: "qrc:/icons/chevron-down.svg"; rotation: -90; iconColor: Theme.textFaint }
        }
        MouseArea {
            id: categoryMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { root.openPage(category.targetPage); category.activated() }
        }
    }

    component SettingSwitch: Rectangle {
        id: control
        property bool checked: false
        signal toggled()
        implicitWidth: 38
        implicitHeight: 22
        radius: 11
        color: checked ? Theme.accent : Theme.lineStrong
        Rectangle {
            width: 16; height: 16; radius: 8
            y: 3
            x: control.checked ? control.width - width - 3 : 3
            color: "white"
            Behavior on x { NumberAnimation { duration: Theme.animationDuration(100); easing.type: Easing.OutCubic } }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: control.toggled() }
    }

    component SwitchRow: Item {
        id: switchRow
        required property string title
        property string summary: ""
        property bool checked: false
        signal toggled()
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: summary.length > 0
                                ? Math.max(48, switchTextColumn.implicitHeight + 10)
                                : 44
        Column {
            id: switchTextColumn
            anchors.left: parent.left
            anchors.right: switchSlot.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text { width: parent.width; text: switchRow.title; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium; elide: Text.ElideRight }
            Text {
                visible: switchRow.summary.length > 0
                width: parent.width
                text: switchRow.summary
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
        Item {
            id: switchSlot
            width: 38
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            SettingSwitch {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                checked: switchRow.checked
                onToggled: switchRow.toggled()
            }
        }
    }

    component ChoiceRow: Item {
        id: choiceRow
        required property string title
        required property var choices
        property var selectedValue: ""
        signal valuePicked(var value)
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: 48
        Text {
            anchors.left: parent.left
            anchors.right: choiceSlot.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: choiceRow.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
        Item {
            id: choiceSlot
            width: root.controlColumnWidth
            height: 38
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            SelectField {
                anchors.fill: parent
                model: choiceRow.choices
                selectedValue: choiceRow.selectedValue
                onValuePicked: value => choiceRow.valuePicked(value)
            }
        }
    }

    component NumberRow: Item {
        id: numberRow
        required property string title
        property string summary: ""
        property real value: 0
        property real from: 0
        property real to: 100
        property real stepSize: 1
        property int decimals: 0
        property string unit: ""
        signal valueEdited(real value)
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: summary.length > 0 ? 58 : 48
        Column {
            anchors.left: parent.left
            anchors.right: numberSlot.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text { width: parent.width; text: numberRow.title; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; font.weight: Font.Medium; elide: Text.ElideRight }
            Text { visible: numberRow.summary.length > 0; width: parent.width; text: numberRow.summary; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; elide: Text.ElideRight }
        }
        Item {
            id: numberSlot
            width: root.controlColumnWidth
            height: 38
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            NumericField {
                anchors.fill: parent
                value: numberRow.value
                from: numberRow.from; to: numberRow.to; stepSize: numberRow.stepSize
                decimals: numberRow.decimals; unit: numberRow.unit
                accessibleName: numberRow.title
                onValueEdited: value => numberRow.valueEdited(value)
            }
        }
    }

    component ValueLine: Item {
        id: valueLine
        required property string label
        required property string value
        property bool multiline: false
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: multiline
                                ? Math.max(34, valueText.implicitHeight + 8)
                                : 34
        Text {
            anchors.left: parent.left
            anchors.right: valueSlot.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: valueLine.label
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSmall
            elide: Text.ElideRight
        }
        Item {
            id: valueSlot
            width: valueLine.multiline
                   ? Math.max(root.controlColumnWidth, parent.width - 112)
                   : root.controlColumnWidth
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            Text {
                id: valueText
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignRight
                text: valueLine.value
                color: Theme.textPrimary
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.textSmall
                wrapMode: valueLine.multiline ? Text.WrapAnywhere : Text.NoWrap
                maximumLineCount: valueLine.multiline ? 2 : 1
                elide: valueLine.multiline ? Text.ElideRight : Text.ElideMiddle
            }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: root.pageIndex

        ScrollView {
            id: settingsHomeScroll
            clip: true
            leftPadding: 20; rightPadding: 20; topPadding: 16; bottomPadding: 18
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: settingsHomeScroll.availableWidth
                spacing: 8
                Text { Layout.fillWidth: true; text: "设置会自动保存，只影响以后使用时的偏好。"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
                CategoryRow { title: "外观"; summary: root.themeLabel() + " · 文字 " + Theme.textScalePercent + "%"; iconSource: "qrc:/icons/palette.svg"; targetPage: 1 }
                CategoryRow { title: "操作与快捷键"; summary: "精确转向 " + App.preciseTurnDefaultDegrees.toFixed(0) + "° · 相机灵敏度"; iconSource: "qrc:/icons/mouse-pointer-2.svg"; targetPage: 2 }
                CategoryRow { title: "三维视图"; summary: root.cameraLabel() + " · 世界图层"; iconSource: "qrc:/icons/box.svg"; targetPage: 3 }
                CategoryRow { title: "飞行轨迹"; summary: App.flightPathVisible ? "显示 · " + root.pathLengthLabel() : "已隐藏"; iconSource: "qrc:/icons/route.svg"; targetPage: 4 }
                CategoryRow { title: "记录与存储"; summary: App.telemetryRecordingHz + " Hz · " + App.sessionStorageSizeLabel; iconSource: "qrc:/icons/database.svg"; targetPage: 5 }
                CategoryRow { title: "软件与环境"; summary: "FlightBench " + App.applicationVersion; iconSource: "qrc:/icons/info.svg"; targetPage: 6 }
                Text { Layout.fillWidth: true; Layout.topMargin: 8; text: "机型、世界、航线和安全规则属于测试方案，不在这里重复设置。"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
            }
        }

        ScrollView {
            id: appearanceScroll
            clip: true; leftPadding: 20; rightPadding: 20; topPadding: 12; bottomPadding: 18; contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: appearanceScroll.availableWidth; spacing: 3
                PageHeader { title: "外观"; subtitle: "主题、文字与动态效果" }
                ChoiceRow {
                    title: "界面主题"
                    choices: [{label:"浅色", value:"light"}, {label:"深色", value:"dark"}, {label:"跟随系统", value:"system"}]
                    selectedValue: Theme.mode
                    onValuePicked: value => Theme.mode = value
                }
                ChoiceRow {
                    title: "文字大小"
                    choices: [{label:"紧凑 90%", value:90}, {label:"标准 100%", value:100}, {label:"较大 110%", value:110}]
                    selectedValue: Theme.textScalePercent
                    onValuePicked: value => Theme.textScalePercent = Number(value)
                }
                SwitchRow { title: "减少动态效果"; summary: "关闭不必要的移动动画"; checked: Theme.reducedMotion; onToggled: Theme.reducedMotion = !Theme.reducedMotion }
                ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; compact: true; kind: "secondary"; text: "恢复外观默认值"; iconSource: "qrc:/icons/refresh-cw.svg"; onClicked: { Theme.mode = "light"; Theme.textScalePercent = 100; Theme.reducedMotion = false } }
            }
        }

        ScrollView {
            id: interactionScroll
            clip: true; leftPadding: 20; rightPadding: 20; topPadding: 12; bottomPadding: 18; contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: interactionScroll.availableWidth; spacing: 3
                PageHeader { title: "操作与快捷键"; subtitle: "只调整交互习惯，不改变安全规则" }
                NumberRow { title: "默认精确转向"; value: App.preciseTurnDefaultDegrees; from: 5; to: 180; stepSize: 5; decimals: 0; unit: "°"; onValueEdited: value => App.preciseTurnDefaultDegrees = value }
                NumberRow { title: "旋转灵敏度"; value: App.cameraRotationSensitivity; from: 0.5; to: 2.0; stepSize: 0.1; decimals: 1; onValueEdited: value => App.cameraRotationSensitivity = value }
                NumberRow { title: "缩放灵敏度"; value: App.cameraZoomSensitivity; from: 0.5; to: 2.0; stepSize: 0.1; decimals: 1; onValueEdited: value => App.cameraZoomSensitivity = value }
                SwitchRow { title: "反转垂直旋转"; checked: App.invertCameraVertical; onToggled: App.invertCameraVertical = !App.invertCameraVertical }
                SectionLabel { text: "飞行快捷键（固定）" }
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 104; radius: Theme.radiusMedium; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.line
                    GridLayout {
                        anchors.fill: parent; anchors.margins: 12; columns: 2; columnSpacing: 16; rowSpacing: 7
                        Text { text: "W A S D"; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall }
                        Text { text: "水平移动"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                        Text { text: "Space / C"; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall }
                        Text { text: "上升 / 下降"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                        Text { text: "Q / E"; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall }
                        Text { text: "左转 / 右转"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                        Text { text: "Esc / L"; color: Theme.textPrimary; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textSmall }
                        Text { text: "退出控制 / 安全降落"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                    }
                }
                ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; compact: true; kind: "secondary"; text: "恢复操作默认值"; iconSource: "qrc:/icons/refresh-cw.svg"; onClicked: App.resetInteractionPreferences() }
            }
        }

        ScrollView {
            id: viewScroll
            clip: true; leftPadding: 20; rightPadding: 20; topPadding: 12; bottomPadding: 18; contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: viewScroll.availableWidth; spacing: 3
                PageHeader { title: "三维视图"; subtitle: "相机、位置标记与城市图层" }
                ChoiceRow { title: "默认相机"; choices: [{label:"兼顾无人机与地面",value:"follow-both"},{label:"跟随无人机",value:"follow"},{label:"自由视角",value:"free"}]; selectedValue: App.defaultCameraMode; onValuePicked: value => App.defaultCameraMode = value }
                SwitchRow { title: "显示地面位置标记"; checked: App.groundMarkerVisible; onToggled: App.groundMarkerVisible = !App.groundMarkerVisible }
                ChoiceRow { title: "标记大小"; choices: [{label:"自动",value:"auto"},{label:"小",value:"small"},{label:"中",value:"medium"},{label:"大",value:"large"}]; selectedValue: App.groundMarkerSizeMode; onValuePicked: value => App.groundMarkerSizeMode = value }
                NumberRow { title: "建筑透明度"; value: App.cityBuildingOpacity * 100; from: 25; to: 100; stepSize: 5; decimals: 0; unit: "%"; onValueEdited: value => App.cityBuildingOpacity = value / 100 }
                SectionLabel { text: "地图图层" }
                SwitchRow { title: "道路"; checked: App.worldRoadsVisible; onToggled: App.worldRoadsVisible = !App.worldRoadsVisible }
                SwitchRow { title: "绿地"; checked: App.worldGreenAreasVisible; onToggled: App.worldGreenAreasVisible = !App.worldGreenAreasVisible }
                SwitchRow { title: "水面"; checked: App.worldWaterVisible; onToggled: App.worldWaterVisible = !App.worldWaterVisible }
                SwitchRow { title: "默认场地网格"; checked: App.defaultGridVisible; onToggled: App.defaultGridVisible = !App.defaultGridVisible }
                ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; Layout.topMargin: 8; compact: true; kind: "secondary"; text: "恢复视图默认值"; iconSource: "qrc:/icons/refresh-cw.svg"; onClicked: App.resetViewPreferences() }
            }
        }

        ScrollView {
            id: pathScroll
            clip: true; leftPadding: 20; rightPadding: 20; topPadding: 12; bottomPadding: 18; contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: pathScroll.availableWidth; spacing: 3
                PageHeader { title: "飞行轨迹"; subtitle: "同样作用于三维视图和任务地图" }
                SwitchRow { title: "显示飞行轨迹"; summary: "隐藏后仍继续写入会话记录"; checked: App.flightPathVisible; onToggled: App.flightPathVisible = !App.flightPathVisible }
                NumberRow { title: "轨迹粗细"; value: App.flightPathWidthMeters * 100; from: 2; to: 12; stepSize: 1; decimals: 0; unit: "cm"; onValueEdited: value => App.flightPathWidthMeters = value / 100 }
                ChoiceRow { title: "可见长度"; choices: [{label:"25 m",value:25},{label:"50 m",value:50},{label:"100 m",value:100},{label:"全部",value:0}]; selectedValue: App.flightPathMaxVisibleLengthMeters; onValuePicked: value => App.flightPathMaxVisibleLengthMeters = Number(value) }
                Item {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 48
                    Text {
                        anchors.left: parent.left
                        anchors.right: colorPickerSlot.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: "轨迹颜色"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Item {
                        id: colorPickerSlot
                        width: root.controlColumnWidth
                        height: 38
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        Rectangle {
                            id: colorPickerButton
                            anchors.fill: parent
                            radius: Theme.radiusMedium
                            color: colorPickerMouse.containsMouse ? Theme.surfaceInteractive : Theme.surfaceRaised
                            border.width: 1
                            border.color: colorPickerMouse.containsMouse ? Theme.accent : Theme.line
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                    radius: 5
                                    color: Theme.flightPathColor(App.flightPathColorPreset)
                                    border.width: 1
                                    border.color: Qt.rgba(Theme.textPrimary.r, Theme.textPrimary.g, Theme.textPrimary.b, 0.25)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: String(Theme.flightPathColor(App.flightPathColorPreset)).toUpperCase()
                                    color: Theme.textSecondary
                                    font.family: Theme.monoFontFamily
                                    font.pixelSize: Theme.textTiny
                                    elide: Text.ElideRight
                                }
                                ThemedIcon {
                                    Layout.preferredWidth: 15
                                    Layout.preferredHeight: 15
                                    source: "qrc:/icons/palette.svg"
                                    iconColor: Theme.textMuted
                                }
                            }
                            MouseArea {
                                id: colorPickerMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.loadPendingColor(Theme.flightPathColor(App.flightPathColorPreset))
                                    pathColorDialog.open()
                                }
                            }
                        }
                    }
                }
                SwitchRow { title: "开始新仿真时清空轨迹"; summary: "关闭后可继续显示上一段临时轨迹"; checked: App.flightPathAutoClear; onToggled: App.flightPathAutoClear = !App.flightPathAutoClear }
                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 10; spacing: 8
                    ActionButton { Layout.fillWidth: true; compact: true; kind: "secondary"; text: "立即清空"; iconSource: "qrc:/icons/trash-2.svg"; onClicked: App.clearFlightPath() }
                    ActionButton { Layout.fillWidth: true; compact: true; kind: "secondary"; text: "恢复默认"; iconSource: "qrc:/icons/refresh-cw.svg"; onClicked: App.resetFlightPathDisplaySettings() }
                }
            }
        }

        ScrollView {
            id: storageScroll
            clip: true; leftPadding: 20; rightPadding: 20; topPadding: 12; bottomPadding: 18; contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: storageScroll.availableWidth; spacing: 3
                PageHeader { title: "记录与存储"; subtitle: "遥测采样、报告和会话回收站" }
                ChoiceRow { title: "遥测记录频率"; choices: [{label:"2 Hz · 节省空间",value:2},{label:"5 Hz · 推荐",value:5},{label:"10 Hz · 更精细",value:10}]; selectedValue: App.telemetryRecordingHz; onValuePicked: value => App.telemetryRecordingHz = Number(value) }
                SwitchRow { title: "结束后生成 HTML 报告"; summary: "报告保存在文档目录，不会自动弹出"; checked: App.autoGenerateReports; onToggled: App.autoGenerateReports = !App.autoGenerateReports }
                ChoiceRow { title: "回收站自动清理"; choices: [{label:"7 天",value:7},{label:"30 天",value:30},{label:"90 天",value:90},{label:"不自动清理",value:0}]; selectedValue: App.trashRetentionDays; onValuePicked: value => App.trashRetentionDays = Number(value) }
                SectionLabel { text: "本机存储" }
                ValueLine { label: "会话与回收站"; value: App.sessionStorageSizeLabel }
                ValueLine { label: "回收站"; value: App.trashSessionCount + " 条" }
                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 8; spacing: 8
                    ActionButton { Layout.fillWidth: true; compact: true; kind: "secondary"; text: "打开位置"; iconSource: "qrc:/icons/folder-open.svg"; onClicked: App.openSessionsFolder() }
                    ActionButton { Layout.fillWidth: true; compact: true; kind: "danger"; text: "清空回收站"; iconSource: "qrc:/icons/archive-x.svg"; enabled: App.trashSessionCount > 0; disabledReason: "会话回收站已经是空的"; onClicked: clearTrashDialog.open() }
                }
                Text { Layout.fillWidth: true; Layout.topMargin: 8; text: "采样频率越高，轨迹和图表越细致，也会占用更多空间。"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
                ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; Layout.topMargin: 8; compact: true; kind: "secondary"; text: "恢复记录默认值"; iconSource: "qrc:/icons/refresh-cw.svg"; onClicked: App.resetRecordingPreferences() }
            }
        }

        ScrollView {
            id: environmentScroll
            clip: true; leftPadding: 20; rightPadding: 20; topPadding: 12; bottomPadding: 18; contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: environmentScroll.availableWidth; spacing: 2
                PageHeader { title: "软件与环境"; subtitle: "版本、数据位置与设置备份" }
                SectionLabel { text: "版本" }
                ValueLine { label: "FlightBench"; value: App.applicationVersion }
                ValueLine { label: "Qt"; value: App.qtRuntimeVersion }
                ValueLine { label: "MAVSDK"; value: App.mavsdkVersion }
                ValueLine { label: "构建"; value: App.buildDescription; multiline: true }
                ValueLine { label: "系统"; value: App.platformDescription; multiline: true }
                ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; Layout.topMargin: 6; compact: true; kind: "secondary"; text: "复制环境信息"; iconSource: "qrc:/icons/copy.svg"; onClicked: App.copyEnvironmentInformation() }
                SectionLabel { text: "本机数据位置" }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    ActionButton { Layout.fillWidth: true; Layout.minimumWidth: 80; compact: true; kind: "secondary"; text: "会话"; iconSource: "qrc:/icons/database.svg"; onClicked: App.openSessionsFolder() }
                    ActionButton { Layout.fillWidth: true; Layout.minimumWidth: 80; compact: true; kind: "secondary"; text: "航线"; iconSource: "qrc:/icons/route.svg"; onClicked: App.openRoutesFolder() }
                    ActionButton { Layout.fillWidth: true; Layout.minimumWidth: 80; compact: true; kind: "secondary"; text: "方案"; iconSource: "qrc:/icons/clipboard-check.svg"; onClicked: App.openPlansFolder() }
                }
                SectionLabel { text: "设置备份" }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    ActionButton { Layout.fillWidth: true; compact: true; kind: "secondary"; text: "导出设置"; iconSource: "qrc:/icons/file-down.svg"; onClicked: App.exportApplicationSettings() }
                    ActionButton { Layout.fillWidth: true; compact: true; kind: "secondary"; text: "导入设置"; iconSource: "qrc:/icons/upload.svg"; onClicked: importDialog.open() }
                }
                Text { Layout.fillWidth: true; text: "设置文件不包含会话、航线、测试方案或任何仿真数据。"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
                ActionButton { Layout.alignment: Qt.AlignRight; Layout.preferredWidth: root.controlColumnWidth; Layout.minimumWidth: root.controlColumnWidth; Layout.maximumWidth: root.controlColumnWidth; Layout.preferredHeight: 38; compact: true; kind: "danger"; text: "恢复全部设置"; iconSource: "qrc:/icons/rotate-ccw.svg"; onClicked: resetAllDialog.open() }
            }
        }
    }

    FileDialog {
        id: importDialog
        title: "导入 FlightBench 设置"
        fileMode: FileDialog.OpenFile
        nameFilters: ["FlightBench 设置 (*.json)"]
        onAccepted: {
            const result = App.importApplicationSettings(selectedFile)
            if (result.success) {
                if (result.themeMode !== undefined) Theme.mode = result.themeMode
                if (result.textScalePercent !== undefined) Theme.textScalePercent = Number(result.textScalePercent)
                if (result.reducedMotion !== undefined) Theme.reducedMotion = Boolean(result.reducedMotion)
            }
        }
    }

    Dialog {
        id: pathColorDialog
        anchors.centerIn: parent
        width: Math.min(410, root.width - 28)
        modal: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: Theme.radiusLarge
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.lineStrong
        }
        contentItem: ColumnLayout {
            width: pathColorDialog.availableWidth
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: 68
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.right: colorDialogCloseButton.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: "选择轨迹颜色"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTitle
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        text: "拖动调色板选择，也可以精确输入 RGB"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        elide: Text.ElideRight
                    }
                }
                CompactIconButton {
                    id: colorDialogCloseButton
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.top: parent.top
                    anchors.topMargin: 12
                    iconSource: "qrc:/icons/x.svg"
                    accessibleLabel: "关闭颜色选择"
                    toolTipText: "关闭"
                    onClicked: pathColorDialog.close()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.preferredHeight: 1
                color: Theme.line
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.topMargin: 16
                Layout.bottomMargin: 18
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: "颜色"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        font.weight: Font.Medium
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 138
                        radius: Theme.radiusMedium
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.lineStrong
                        clip: true
                        Canvas {
                            id: saturationValueCanvas
                            anchors.fill: parent
                            anchors.margins: 1
                            onPaint: {
                                const context = getContext("2d")
                                context.clearRect(0, 0, width, height)
                                const colorGradient = context.createLinearGradient(0, 0, width, 0)
                                colorGradient.addColorStop(0, "#FFFFFF")
                                colorGradient.addColorStop(1, String(Qt.hsva(root.pendingColorHue, 1, 1, 1)))
                                context.fillStyle = colorGradient
                                context.fillRect(0, 0, width, height)
                                const shadeGradient = context.createLinearGradient(0, 0, 0, height)
                                shadeGradient.addColorStop(0, "rgba(0, 0, 0, 0)")
                                shadeGradient.addColorStop(1, "rgba(0, 0, 0, 1)")
                                context.fillStyle = shadeGradient
                                context.fillRect(0, 0, width, height)
                            }
                            Connections {
                                target: root
                                function onPendingColorHueChanged() {
                                    saturationValueCanvas.requestPaint()
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.CrossCursor
                                function updateColor(mouseX, mouseY) {
                                    root.pendingColorSaturation = Math.max(0, Math.min(1, mouseX / width))
                                    root.pendingColorValue = Math.max(0, Math.min(1, 1 - mouseY / height))
                                    root.updatePendingRgbFromHsv()
                                }
                                onPressed: mouse => updateColor(mouse.x, mouse.y)
                                onPositionChanged: mouse => {
                                    if (pressed)
                                        updateColor(mouse.x, mouse.y)
                                }
                            }
                            Rectangle {
                                width: 14
                                height: 14
                                radius: 7
                                x: Math.max(0, Math.min(parent.width - width,
                                                        root.pendingColorSaturation * parent.width - width / 2))
                                y: Math.max(0, Math.min(parent.height - height,
                                                        (1 - root.pendingColorValue) * parent.height - height / 2))
                                color: "transparent"
                                border.width: 2
                                border.color: "white"
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: -1
                                    radius: width / 2
                                    color: "transparent"
                                    border.width: 1
                                    border.color: Qt.rgba(0, 0, 0, 0.45)
                                }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 16
                        radius: 8
                        color: "transparent"
                        clip: true
                        Canvas {
                            id: hueCanvas
                            anchors.fill: parent
                            onPaint: {
                                const context = getContext("2d")
                                const gradient = context.createLinearGradient(0, 0, width, 0)
                                gradient.addColorStop(0.0, "#FF0000")
                                gradient.addColorStop(1 / 6, "#FFFF00")
                                gradient.addColorStop(2 / 6, "#00FF00")
                                gradient.addColorStop(3 / 6, "#00FFFF")
                                gradient.addColorStop(4 / 6, "#0000FF")
                                gradient.addColorStop(5 / 6, "#FF00FF")
                                gradient.addColorStop(1.0, "#FF0000")
                                context.fillStyle = gradient
                                context.fillRect(0, 0, width, height)
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                function updateHue(mouseX) {
                                    root.pendingColorHue = Math.max(0, Math.min(1, mouseX / width))
                                    root.updatePendingRgbFromHsv()
                                }
                                onPressed: mouse => updateHue(mouse.x)
                                onPositionChanged: mouse => {
                                    if (pressed)
                                        updateHue(mouse.x)
                                }
                            }
                            Rectangle {
                                width: 8
                                height: parent.height + 4
                                radius: 4
                                x: Math.max(0, Math.min(parent.width - width,
                                                        root.pendingColorHue * parent.width - width / 2))
                                y: -2
                                color: "transparent"
                                border.width: 2
                                border.color: "white"
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            color: "transparent"
                            border.width: 1
                            border.color: Qt.rgba(0, 0, 0, 0.18)
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 62
                    radius: Theme.radiusMedium
                    color: Theme.surfaceInteractive
                    border.width: 1
                    border.color: Theme.line
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 42
                            Layout.fillHeight: true
                            radius: Theme.radiusSmall
                            color: root.pendingColorHex()
                            border.width: 1
                            border.color: Qt.rgba(0, 0, 0, 0.16)
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Text {
                                text: root.pendingColorHex()
                                color: Theme.textPrimary
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.textSmall
                                font.weight: Font.Medium
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Text { text: "R"; color: Theme.textMuted; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textTiny }
                                NumericField {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 52
                                    Layout.preferredWidth: 64
                                    Layout.preferredHeight: 28
                                    showSteppers: false
                                    value: root.pendingColorRed
                                    from: 0; to: 255; stepSize: 1; decimals: 0
                                    accessibleName: "红色通道"
                                    onValueEdited: value => root.setPendingRgb("r", value)
                                }
                                Text { text: "G"; color: Theme.textMuted; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textTiny }
                                NumericField {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 52
                                    Layout.preferredWidth: 64
                                    Layout.preferredHeight: 28
                                    showSteppers: false
                                    value: root.pendingColorGreen
                                    from: 0; to: 255; stepSize: 1; decimals: 0
                                    accessibleName: "绿色通道"
                                    onValueEdited: value => root.setPendingRgb("g", value)
                                }
                                Text { text: "B"; color: Theme.textMuted; font.family: Theme.monoFontFamily; font.pixelSize: Theme.textTiny }
                                NumericField {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 52
                                    Layout.preferredWidth: 64
                                    Layout.preferredHeight: 28
                                    showSteppers: false
                                    value: root.pendingColorBlue
                                    from: 0; to: 255; stepSize: 1; decimals: 0
                                    accessibleName: "蓝色通道"
                                    onValueEdited: value => root.setPendingRgb("b", value)
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        Layout.preferredWidth: 88
                        compact: true
                        kind: "secondary"
                        text: "取消"
                        onClicked: pathColorDialog.close()
                    }
                    ActionButton {
                        Layout.preferredWidth: 104
                        compact: true
                        kind: "primary"
                        text: "应用颜色"
                        onClicked: {
                            App.flightPathColorPreset = root.pendingColorHex()
                            pathColorDialog.close()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: clearTrashDialog
        anchors.centerIn: parent
        width: Math.min(340, root.width - 24)
        modal: true
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { radius: Theme.radiusLarge; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.lineStrong }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "清空会话回收站？"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            Text { Layout.fillWidth: true; text: "回收站中的 " + App.trashSessionCount + " 条会话将永久删除，无法恢复。"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { Layout.preferredWidth: 82; text: "取消"; kind: "secondary"; onClicked: clearTrashDialog.close() }
                ActionButton { Layout.preferredWidth: 108; text: "永久清空"; kind: "danger"; onClicked: { if (App.clearSessionTrash()) clearTrashDialog.close() } }
            }
        }
    }

    Dialog {
        id: resetAllDialog
        anchors.centerIn: parent
        width: Math.min(340, root.width - 24)
        modal: true
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { radius: Theme.radiusLarge; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.lineStrong }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "恢复全部设置？"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            Text { Layout.fillWidth: true; text: "外观、操作、三维视图、轨迹和记录偏好会恢复默认值。测试方案与会话不会删除。"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { Layout.preferredWidth: 82; text: "取消"; kind: "secondary"; onClicked: resetAllDialog.close() }
                ActionButton {
                    Layout.preferredWidth: 108; text: "恢复默认"; kind: "danger"
                    onClicked: {
                        App.resetAllApplicationPreferences()
                        Theme.mode = "light"
                        Theme.textScalePercent = 100
                        Theme.reducedMotion = false
                        resetAllDialog.close()
                    }
                }
            }
        }
    }
}
