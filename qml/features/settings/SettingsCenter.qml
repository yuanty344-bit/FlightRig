pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
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

    StackLayout {
        anchors.fill: parent
        currentIndex: root.pageIndex

        ScrollView {
            id: settingsHomeScroll
            clip: true
            leftPadding: 20; rightPadding: 20; topPadding: 16; bottomPadding: 18
            contentWidth: availableWidth
            ScrollBar.horizontal: InsetScrollBar { policy: ScrollBar.AlwaysOff }
            ScrollBar.vertical: InsetScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                width: settingsHomeScroll.availableWidth
                spacing: 8
                Text { Layout.fillWidth: true; text: "设置会自动保存，只影响以后使用时的偏好。"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
                SettingsCategoryRow { onActivated: root.openPage(targetPage); title: "外观"; summary: root.themeLabel() + " · 文字 " + Theme.textScalePercent + "%"; iconSource: "qrc:/icons/palette.svg"; targetPage: 1 }
                SettingsCategoryRow { onActivated: root.openPage(targetPage); title: "操作与快捷键"; summary: "精确转向 " + App.preciseTurnDefaultDegrees.toFixed(0) + "° · 相机灵敏度"; iconSource: "qrc:/icons/mouse-pointer-2.svg"; targetPage: 2 }
                SettingsCategoryRow { onActivated: root.openPage(targetPage); title: "三维视图"; summary: root.cameraLabel() + " · 场景图层"; iconSource: "qrc:/icons/box.svg"; targetPage: 3 }
                SettingsCategoryRow { onActivated: root.openPage(targetPage); title: "飞行轨迹"; summary: App.flightPathVisible ? "显示 · " + root.pathLengthLabel() : "已隐藏"; iconSource: "qrc:/icons/route.svg"; targetPage: 4 }
                SettingsCategoryRow { onActivated: root.openPage(targetPage); title: "记录与存储"; summary: App.telemetryRecordingHz + " Hz · " + App.sessionStorageSizeLabel; iconSource: "qrc:/icons/database.svg"; targetPage: 5 }
                SettingsCategoryRow { onActivated: root.openPage(targetPage); title: "软件与环境"; summary: "FlightRig " + App.applicationVersion; iconSource: "qrc:/icons/info.svg"; targetPage: 6 }
                Text { Layout.fillWidth: true; Layout.topMargin: 8; text: "飞行器、仿真场景、航线和安全规则属于测试方案，不在这里重复设置。"; color: Theme.textFaint; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny; wrapMode: Text.WordWrap }
            }
        }

        SettingsAppearancePage {
            controlColumnWidth: root.controlColumnWidth
            onReturnRequested: root.returnHome()
        }

        SettingsInteractionPage {
            controlColumnWidth: root.controlColumnWidth
            onReturnRequested: root.returnHome()
        }

        SettingsScenePage {
            controlColumnWidth: root.controlColumnWidth
            onReturnRequested: root.returnHome()
        }

        SettingsPathPage {
            controlColumnWidth: root.controlColumnWidth
            onReturnRequested: root.returnHome()
            onColorRequested: {
                root.loadPendingColor(Theme.flightPathColor(App.flightPathColorPreset))
                pathColorDialog.open()
            }
        }

        SettingsStoragePage {
            controlColumnWidth: root.controlColumnWidth
            onReturnRequested: root.returnHome()
            onClearTrashRequested: clearTrashDialog.open()
        }

        SettingsSoftwarePage {
            controlColumnWidth: root.controlColumnWidth
            onReturnRequested: root.returnHome()
            onImportRequested: importDialog.open()
            onResetAllRequested: resetAllDialog.open()
        }
    }

    FileDialog {
        id: importDialog
        title: "导入 FlightRig 设置"
        fileMode: FileDialog.OpenFile
        nameFilters: ["FlightRig 设置 (*.json)"]
        onAccepted: {
            const result = App.importApplicationSettings(selectedFile)
            if (result.success) {
                if (result.themeMode !== undefined) Theme.mode = result.themeMode
                if (result.textScalePercent !== undefined) Theme.textScalePercent = Number(result.textScalePercent)
                if (result.reducedMotion !== undefined) Theme.reducedMotion = Boolean(result.reducedMotion)
            }
        }
    }

    MotionDialog {
        id: pathColorDialog
        anchors.centerIn: parent
        width: Math.min(410, root.width - 28)
        modal: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: DialogSurface {
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
                PanelCloseButton {
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

    MotionDialog {
        id: clearTrashDialog
        anchors.centerIn: parent
        width: Math.min(340, root.width - 24)
        modal: true
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: DialogSurface { radius: Theme.radiusLarge; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.lineStrong }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "清空飞行记录回收站？"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            Text { Layout.fillWidth: true; text: "回收站中的 " + App.trashSessionCount + " 条飞行记录将永久删除，无法恢复。"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { Layout.preferredWidth: 82; text: "取消"; kind: "secondary"; onClicked: clearTrashDialog.close() }
                ActionButton { Layout.preferredWidth: 108; text: "永久清空"; kind: "danger"; onClicked: { if (App.clearSessionTrash()) clearTrashDialog.close() } }
            }
        }
    }

    MotionDialog {
        id: resetAllDialog
        anchors.centerIn: parent
        width: Math.min(340, root.width - 24)
        modal: true
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: DialogSurface { radius: Theme.radiusLarge; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.lineStrong }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "恢复全部设置？"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            Text { Layout.fillWidth: true; text: "外观、操作、三维视图、轨迹和记录偏好会恢复默认值。测试方案与飞行记录不会删除。"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; wrapMode: Text.WordWrap }
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
