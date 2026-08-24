pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import FlightBench

Rectangle {
    id: root

    property var analysis: ({})
    readonly property bool available: Boolean(analysis && analysis.available)

    implicitHeight: 96
    radius: Theme.radiusMedium
    color: Theme.surfaceRaised
    border.width: 1
    border.color: Theme.line

    function meters(value) {
        return Number(value || 0).toFixed(1) + " m"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 0

        ColumnLayout {
            Layout.preferredWidth: 150
            Layout.maximumWidth: 172
            spacing: 4

            RowLayout {
                spacing: 7
                ThemedIcon {
                    source: "qrc:/icons/route.svg"
                    iconColor: Theme.accent
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }
                Text {
                    text: "航线执行"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                    font.weight: Font.Medium
                }
                Rectangle {
                    visible: Boolean(root.analysis.estimated)
                    Layout.preferredWidth: estimateText.implicitWidth + 10
                    Layout.preferredHeight: 20
                    radius: 10
                    color: Theme.warningSoft
                    Text {
                        id: estimateText
                        anchors.centerIn: parent
                        text: "估算"
                        color: Theme.warning
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: "计划 " + root.meters(root.analysis.plannedDistanceMeters)
                      + " · 实际 " + root.meters(root.analysis.actualDistanceMeters)
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                elide: Text.ElideRight
            }
        }

        Repeater {
            model: [
                { label: "平均横向偏差", value: root.meters(root.analysis.meanHorizontalDeviationMeters) },
                { label: "95% 偏差", value: root.meters(root.analysis.p95HorizontalDeviationMeters) },
                { label: "最大偏差", value: root.meters(root.analysis.maxHorizontalDeviationMeters) },
                { label: "2 m 内时间", value: Number(root.analysis.withinTolerancePercent || 0).toFixed(0) + "%" }
            ]
            delegate: Item {
                id: metric
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 1
                    height: 42
                    color: Theme.line
                }
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Text {
                        text: metric.modelData.label
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                    Text {
                        text: metric.modelData.value
                        color: Theme.textPrimary
                        font.family: Theme.monoFontFamily
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }
}
