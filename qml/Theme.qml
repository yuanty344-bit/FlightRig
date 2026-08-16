pragma Singleton
import QtQuick
import QtCore

QtObject {
    property alias mode: preferences.mode
    property Settings preferences: Settings {
        id: preferences
        category: "Appearance"
        property string mode: "light"
    }

    property SystemPalette systemPalette: SystemPalette { }
    readonly property bool darkMode: mode === "dark"
                                     || (mode === "system"
                                         && systemPalette.window.hslLightness < 0.5)

    readonly property string fontFamily: "Microsoft YaHei UI"
    readonly property string monoFontFamily: "Cascadia Mono"

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
    readonly property color disabled: darkMode ? "#1B293B" : "#E5E9EE"
    readonly property color accentContent: "#FFFFFF"

    readonly property color instrumentSky: darkMode ? "#316CA8" : "#79AEDC"
    readonly property color instrumentGround: darkMode ? "#72543D" : "#B4957D"
    readonly property color instrumentMark: darkMode ? "#F8CC55" : "#7C6111"
    readonly property color sourceApp: darkMode ? "#6FA8FF" : "#1769D2"
    readonly property color sourceDocker: darkMode ? "#43D5A4" : "#16815F"
    readonly property color sourceMavsdk: darkMode ? "#A78BFA" : "#6D4CC2"
    readonly property color sourcePx4: darkMode ? "#F0AA45" : "#A66812"

    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 8
    readonly property int radiusLarge: 12
    readonly property int radiusXLarge: 14

    readonly property int space4: 4
    readonly property int space8: 8
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int space20: 20
    readonly property int space24: 24
    readonly property int space32: 32

    readonly property int textTiny: 11
    readonly property int textSmall: 12
    readonly property int textBody: 14
    readonly property int textSubtitle: 16
    readonly property int textTitle: 19
    readonly property int textDisplay: 52
}
