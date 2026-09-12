pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

MotionDialog {
    id: root
    property string sessionId: ""
    signal replayRequested(string sessionId, int elapsedMs)
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 36 : 1120, 1240)
    height: Math.min(parent ? parent.height - 36 : 760, 820)
    modal: true; padding: 0
    closePolicy: Dialog.CloseOnEscape
    onClosed: App.closeLoadedVisualSurvey()
    background: DialogSurface { }
    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 74
            Layout.leftMargin: 22; Layout.rightMargin: 14
            ColumnLayout {
                Layout.fillWidth: true; spacing: 2
                Text { text: "视觉巡检影像地图"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    text: App.visualSurvey.keyframeCount + " 张关键帧 · "
                          + App.visualSurvey.goodKeyframeCount + " 张清晰 · "
                          + App.visualSurvey.warningKeyframeCount + " 张需复核 · 自动跳过 "
                          + App.visualSurvey.rejectedCount + " 张 · 采集覆盖 "
                          + Number(App.visualSurvey.spatialCoveragePercent).toFixed(0) + "%"
                    color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; elide: Text.ElideRight
                }
            }
            PanelCloseButton { accessibleLabel: "关闭影像地图"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
        Item {
            id: surveyViewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 14

            readonly property real minimumContentWidth: 980 * Theme.textScale
            readonly property real minimumContentHeight: 620 * Theme.textScale
            // Account for the other bar taking up space without introducing a
            // visibility binding loop at the responsive breakpoint.
            readonly property bool needsHorizontalScroll:
                width - (height < minimumContentHeight ? Theme.scrollBarExtent : 0)
                < minimumContentWidth
            readonly property bool needsVerticalScroll:
                height - (width < minimumContentWidth ? Theme.scrollBarExtent : 0)
                < minimumContentHeight

            Flickable {
                id: surveyScroller
                objectName: "visualSurveyScroller"
                anchors.fill: parent
                anchors.rightMargin: surveyViewport.needsVerticalScroll
                                     ? Theme.scrollBarExtent : 0
                anchors.bottomMargin: surveyViewport.needsHorizontalScroll
                                      ? Theme.scrollBarExtent : 0
                clip: true
                contentWidth: Math.max(width, surveyViewport.minimumContentWidth)
                contentHeight: Math.max(height, surveyViewport.minimumContentHeight)
                flickableDirection: Flickable.HorizontalAndVerticalFlick
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: InsetScrollBar {
                    objectName: "visualSurveyVerticalScrollBar"
                    parent: surveyViewport
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: surveyViewport.needsHorizontalScroll
                                          ? Theme.scrollBarExtent : 0
                    policy: surveyViewport.needsVerticalScroll
                            ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }
                ScrollBar.horizontal: InsetScrollBar {
                    objectName: "visualSurveyHorizontalScrollBar"
                    parent: surveyViewport
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: surveyViewport.needsVerticalScroll
                                         ? Theme.scrollBarExtent : 0
                    policy: surveyViewport.needsHorizontalScroll
                            ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }

                VisualSurveyMap {
                    width: surveyScroller.contentWidth
                    height: surveyScroller.contentHeight
                    compact: false
                    onReplayRequested: elapsedMs => {
                        root.close()
                        root.replayRequested(root.sessionId, elapsedMs)
                    }
                }
            }
        }
    }
}
