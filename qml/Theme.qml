pragma Singleton
import QtQuick
import QtCore

QtObject {
    property alias darkMode: preferences.darkMode
    property Settings preferences: Settings {
        id: preferences
        category: "Appearance"
        property bool darkMode: true
    }

    readonly property string fontFamily: "Microsoft YaHei UI"
    readonly property string monoFontFamily: "Cascadia Mono"

    readonly property color canvas: darkMode ? "#0A111C" : "#F4F6F8"
    readonly property color canvasSoft: darkMode ? "#0C1522" : "#FFFFFF"
    readonly property color surface: darkMode ? "#0E1928" : "#FFFFFF"
    readonly property color surfaceRaised: darkMode ? "#142236" : "#EEF2F6"
    readonly property color surfaceInteractive: darkMode ? "#192A40" : "#E5ECF4"
    readonly property color line: darkMode ? "#26364B" : "#DCE2E9"
    readonly property color lineStrong: darkMode ? "#31445D" : "#C5CED8"

    readonly property color textPrimary: darkMode ? "#E7EDF7" : "#20262E"
    readonly property color textSecondary: darkMode ? "#AAB7C8" : "#4E5966"
    readonly property color textMuted: darkMode ? "#8494AA" : "#697480"
    readonly property color textFaint: darkMode ? "#74859C" : "#66727F"

    readonly property color accent: darkMode ? "#3178DE" : "#1769D2"
    readonly property color accentHover: darkMode ? "#4D8CE7" : "#0F5BBB"
    readonly property color accentSoft: darkMode ? "#183761" : "#E8F1FC"
    readonly property color success: darkMode ? "#2ECF9B" : "#16815F"
    readonly property color successSoft: darkMode ? "#103A32" : "#E2F4ED"
    readonly property color warning: darkMode ? "#F0AA45" : "#A66812"
    readonly property color warningSoft: darkMode ? "#3D2E1B" : "#FBF0DB"
    readonly property color danger: darkMode ? "#F3A29A" : "#B53A42"
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
    readonly property int radiusXLarge: 16

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
