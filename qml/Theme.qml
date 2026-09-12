pragma Singleton
import QtQuick
import QtCore

QtObject {
    property alias mode: preferences.mode
    property alias textScalePercent: preferences.textScalePercent
    property alias reducedMotion: preferences.reducedMotion
    property Settings preferences: Settings {
        id: preferences
        category: "Appearance"
        property string mode: "light"
        property int textScalePercent: 100
        property bool reducedMotion: false
    }

    property SystemPalette systemPalette: SystemPalette { }
    readonly property bool darkMode: mode === "dark"
                                     || (mode === "system"
                                         && systemPalette.window.hslLightness < 0.5)

    readonly property string fontFamily: "Microsoft YaHei UI"
    readonly property string monoFontFamily: "Cascadia Mono"
    readonly property real textScale: Math.max(0.9, Math.min(1.1, textScalePercent / 100.0))

    function scaledFontSize(baseSize) {
        return Math.round(baseSize * textScale)
    }

    function animationDuration(normalDuration) {
        return reducedMotion ? 0 : normalDuration
    }

    readonly property int motionFast: animationDuration(100)
    readonly property int motionNormal: animationDuration(180)
    readonly property int motionSlow: animationDuration(240)
    readonly property real pageTravel: reducedMotion ? 0 : 14

    readonly property color canvas: darkMode ? "#0C1118" : "#EEF2F6"
    readonly property color canvasSoft: darkMode ? "#111720" : "#FBFCFE"
    readonly property color surface: darkMode ? "#171F2A" : "#FFFFFF"
    readonly property color surfaceRaised: darkMode ? "#1B2531" : "#F3F6F9"
    readonly property color surfaceInteractive: darkMode ? "#202C39" : "#EAF0F5"
    readonly property color line: darkMode ? "#283543" : "#DCE3EA"
    readonly property color lineStrong: darkMode ? "#3A4959" : "#C9D3DD"

    readonly property color textPrimary: darkMode ? "#EDF2F7" : "#17202A"
    readonly property color textSecondary: darkMode ? "#B2BDC9" : "#495563"
    readonly property color textMuted: darkMode ? "#9EABB9" : "#5C6876"
    readonly property color textFaint: darkMode ? "#7F8C9A" : "#7A8694"

    readonly property color accent: darkMode ? "#5595F5" : "#1769E0"
    readonly property color accentHover: darkMode ? "#70A7F7" : "#125EC8"
    readonly property color accentSoft: darkMode ? "#182C48" : "#E9F1FD"
    readonly property color success: darkMode ? "#4DC9A8" : "#16836D"
    readonly property color successSoft: darkMode ? "#17332F" : "#E8F5F1"
    readonly property color warning: darkMode ? "#EFAD58" : "#B86812"
    readonly property color warningSoft: darkMode ? "#3D2E1B" : "#FBF0DB"
    readonly property color danger: darkMode ? "#EF7777" : "#C44242"
    readonly property color dangerSoft: darkMode ? "#28191D" : "#FBE9EA"
    readonly property color accentContent: "#FFFFFF"
    readonly property color overlay: darkMode ? "#99070B10" : "#700C1724"
    readonly property color shadow: darkMode ? "#90000000" : "#38091828"
    readonly property color heroStart: darkMode ? "#172638" : "#18304C"
    readonly property color heroEnd: darkMode ? "#0D1723" : "#0D1B2D"
    readonly property color heroText: "#F4F8FC"
    readonly property color heroMuted: "#A9BCD0"
    readonly property color heroIcon: "#DCE8F3"
    readonly property color heroLine: "#36506B"
    readonly property color heroSurface: "#243A52"

    readonly property color instrumentSky: darkMode ? "#316CA8" : "#79AEDC"
    readonly property color instrumentGround: darkMode ? "#72543D" : "#B4957D"
    readonly property color instrumentMark: darkMode ? "#F8CC55" : "#7C6111"

    // Single source of truth for flight path preset colors, shared by the 3D
    // scene, the mission map and the settings swatches.
    function flightPathColor(preset) {
        if (typeof preset === "string" && /^#[0-9a-fA-F]{6}$/.test(preset))
            return preset
        if (preset === "blue") return darkMode ? "#72A7F5" : "#1769D2"
        if (preset === "teal") return darkMode ? "#43D5A4" : "#16815F"
        if (preset === "violet") return darkMode ? "#A78BFA" : "#6D4CC2"
        return darkMode ? "#FFB454" : "#C85D00"
    }

    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 8
    readonly property int radiusLarge: 12
    readonly property int radiusXLarge: 16

    readonly property int space8: 8
    readonly property int space12: 12
    readonly property int edgeInset: 12
    readonly property int panelInset: 20
    readonly property int accentRailInset: 10
    readonly property int scrollBarExtent: 12
    readonly property int scrollBarEdgeGap: 4
    readonly property int closeButtonExtent: 36

    readonly property int textTiny: scaledFontSize(11)
    readonly property int textSmall: scaledFontSize(12)
    readonly property int textBody: scaledFontSize(14)
    readonly property int textSubtitle: scaledFontSize(16)
    readonly property int textTitle: scaledFontSize(19)
}
