pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench
import "../../controls"
import "../sensors"

Dialog {
    id: depthMapDialog
    required property Item host
    parent: host
    anchors.centerIn: parent
    width: Math.min(host.width - 36, 1180)
    height: Math.min(host.height - 36, 760)
    modal: true
    padding: 0
    closePolicy: Dialog.CloseOnEscape
    onClosed: App.closeLoadedDepthMap()
    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.lineStrong
    }
    contentItem: ColumnLayout {
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            color: "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 14
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: "会话 DepthFusion 三维地图"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTitle
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: App.depthMapping.confirmedVoxelCount + " 个稳定体素 · "
                              + App.depthMapping.chunkCount + " 个分块 · 置信度 "
                              + App.depthMapping.mapConfidencePercent.toFixed(0) + "% · 历史最小净空 "
                              + (App.depthMapping.minimumObservedCorridorMeters > 0
                                 ? App.depthMapping.minimumObservedCorridorMeters.toFixed(1) + " m"
                                 : "未记录")
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                    }
                }
                CompactIconButton {
                    iconSource: "qrc:/icons/x.svg"
                    accessibleLabel: "关闭三维地图"
                    toolTipText: "关闭"
                    onClicked: depthMapDialog.close()
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 14
            active: depthMapDialog.opened
            sourceComponent: Component {
                DepthMapView3D { }
            }
        }
    }
}
