pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench
import "../../controls"
import "../sensors"

MotionDialog {
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
    background: DialogSurface {
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
            ColumnLayout {
                anchors.left: parent.left
                anchors.leftMargin: Theme.panelInset
                anchors.right: closeButton.left
                anchors.rightMargin: Theme.space12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: "飞行记录的 DepthFusion 三维地图"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTitle
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: App.depthMapping.confirmedVoxelCount + " 个稳定体素 · "
                          + App.depthMapping.chunkCount + " 个分块 · 置信度 "
                          + App.depthMapping.mapConfidencePercent.toFixed(0) + "% · 历史最小净空 "
                          + (App.depthMapping.minimumObservedCorridorMeters > 0
                             ? App.depthMapping.minimumObservedCorridorMeters.toFixed(1) + " m"
                             : "未记录")
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                    elide: Text.ElideRight
                }
            }
            PanelCloseButton {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: Theme.edgeInset
                anchors.top: parent.top
                anchors.topMargin: Theme.edgeInset
                accessibleLabel: "关闭三维地图"
                onClicked: depthMapDialog.close()
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
