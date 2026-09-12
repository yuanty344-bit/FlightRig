pragma ComponentBehavior: Bound
import QtQuick
import "../../controls"
import "../inspection"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FlightBench

SurfaceCard {
    id: root
    signal inspectionTargetRequested()
    readonly property real minimumPanelHeight: root.gimbalCamera
        ? Math.max(460, cameraContent.implicitHeight + 28) : 460

    property int viewMode: 0
    // Set by FlightPage so a hidden sensor page does not keep ownership of the
    // shared QQuick3DGeometry while a history dialog renders the same map.
    property bool viewActive: true
    readonly property bool showDepth: root.depthCamera && root.viewMode === 1
    readonly property bool showPointCloud: root.depthCamera && root.viewMode === 2
    property real pointerDistance: 0
    readonly property bool depthCamera: App.displayedSensorKind === "depth-camera"
    readonly property bool gimbalCamera: App.displayedSensorKind === "gimbal-camera"
    readonly property bool monoCamera: App.displayedSensorKind === "mono-camera"
    readonly property bool surveyRecordingAllowed:
        String(((App.selectedTestPlan || {}).sensorSettings || {}).usage || "off") === "record"
    property bool surveyExpanded: false
    readonly property bool gimbalControlsReady: root.gimbalCamera
                                                 && App.sensorServiceRunning
                                                 && App.sensorServiceConnected
                                                 && !App.replayActive
    readonly property string streamName: root.showDepth ? "depth" : "rgb"
    readonly property bool frameReady: root.showDepth ? App.cameraDepthReady : App.cameraRgbReady
    readonly property string frameSource: root.showDepth ? App.cameraDepthFrame : App.cameraRgbFrame
    readonly property real streamFrameHz: root.showDepth ? App.cameraDepthFrameHz
                                                        : App.cameraRgbFrameHz
    readonly property int streamLatency: root.showDepth
                                         ? App.cameraDepthLatencyMilliseconds
                                         : App.cameraRgbLatencyMilliseconds
    readonly property bool streamInterrupted: root.showDepth
                                              ? App.cameraDepthStreamInterrupted
                                              : App.cameraRgbStreamInterrupted
    // Image.status briefly returns to Loading for every data-URL refresh. Do
    // not let that transient state replace a valid displayed frame with the
    // "decoding" placeholder or make the FPS label flash several times per
    // second. The previous frame remains visible until its replacement is
    // ready, and the latch is reset only when the selected stream disappears.
    property bool hasDecodedFrame: false
    readonly property bool frameDecoded: root.frameReady
                                         && (root.hasDecodedFrame
                                             || cameraImage.status === Image.Ready)
    readonly property bool frameDecodeFailed: root.frameReady
                                              && !root.hasDecodedFrame
                                              && cameraImage.status === Image.Error

    onStreamNameChanged: root.hasDecodedFrame = false
    onDepthCameraChanged: {
        if (!root.depthCamera)
            root.viewMode = 0
    }
    onFrameReadyChanged: {
        if (!root.frameReady)
            root.hasDecodedFrame = false
    }

    color: Theme.surface

    ColumnLayout {
        id: cameraContent
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ThemedIcon {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                source: "qrc:/icons/camera.svg"
                iconColor: root.showPointCloud && App.depthMapping.available ? Theme.accent
                           : root.frameDecoded ? Theme.success
                           : root.frameDecodeFailed ? Theme.danger : Theme.textMuted
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    text: root.showPointCloud ? "Depth 三维障碍建图"
                          : root.depthCamera ? "RGB 与深度相机"
                                           : root.gimbalCamera ? "云台相机" : "单目相机"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSubtitle
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: root.showPointCloud
                          ? App.depthMapping.statusMessage
                          : root.frameDecoded
                          ? Number(root.streamFrameHz).toFixed(1) + " FPS · "
                            + (root.streamLatency >= 0 ? "约 " + root.streamLatency + " ms"
                               : "延迟未测量")
                          : root.frameDecodeFailed ? "画面解码失败"
                          : root.frameReady ? "正在解码相机画面"
                          : App.replayActive
                            ? root.streamInterrupted ? "历史画面中断" : "等待历史画面"
                            : App.sensorServiceStatus
                    color: root.frameDecodeFailed ? Theme.danger
                           : root.streamInterrupted ? Theme.warning : Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    elide: Text.ElideRight
                }
            }

            ActionButton {
                visible: root.monoCamera && !App.replayActive
                text: App.visualSurvey.active ? "暂停巡检" : "开始视觉巡检"
                iconSource: App.visualSurvey.active ? "qrc:/icons/pause.svg" : "qrc:/icons/camera.svg"
                compact: true
                kind: App.visualSurvey.active ? "secondary" : "primary"
                enabled: App.sensorServiceRunning && App.sensorServiceConnected
                         && root.surveyRecordingAllowed
                disabledReason: !root.surveyRecordingAllowed
                                ? "在测试方案中把传感器数据设为“预览并记录”后可用"
                                : "相机数据服务连接后才能开始视觉巡检"
                onClicked: {
                    if (App.visualSurvey.active) App.stopVisualSurvey()
                    else if (App.startVisualSurvey()) root.surveyExpanded = true
                }
            }
            CompactIconButton {
                visible: root.monoCamera
                iconSource: "qrc:/icons/map-pinned.svg"
                accessibleLabel: root.surveyExpanded ? "收起影像地图" : "展开影像地图"
                toolTipText: root.surveyExpanded ? "收起影像地图" : "查看影像地图"
                normalIconColor: root.surveyExpanded ? Theme.accent : Theme.textSecondary
                onClicked: root.surveyExpanded = !root.surveyExpanded
            }

            CompactIconButton {
                visible: !root.showPointCloud
                iconSource: "qrc:/icons/camera.svg"
                accessibleLabel: "保存当前画面"
                toolTipText: "截图"
                enabled: root.frameReady
                disabledReason: "当前时刻没有可保存的画面"
                onClicked: App.saveCameraSnapshot(root.streamName)
            }
            CompactIconButton {
                visible: !root.showPointCloud
                iconSource: App.cameraRecording ? "qrc:/icons/square.svg" : "qrc:/icons/video.svg"
                accessibleLabel: App.cameraRecording ? "停止录像" : "开始录像"
                toolTipText: App.cameraRecording ? "停止并保存录像" : "开始录像"
                normalIconColor: App.cameraRecording ? Theme.danger : Theme.textSecondary
                enabled: root.frameReady && !App.replayActive
                disabledReason: App.replayActive ? "历史回放期间不能开始新录像"
                                                 : "收到画面后才能录像"
                onClicked: App.cameraRecording
                           ? App.stopCameraRecording()
                           : App.startCameraRecording(root.streamName)
            }
            CompactIconButton {
                visible: !root.showPointCloud
                iconSource: "qrc:/icons/folder-open.svg"
                accessibleLabel: "打开截图与录像位置"
                toolTipText: "打开保存位置"
                onClicked: App.openSensorCapturesFolder()
            }

            SegmentedControl {
                visible: root.depthCamera
                enabled: !App.cameraRecording
                Layout.preferredWidth: 194
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                options: ["彩色", "深度", "三维"]
                currentIndex: root.viewMode
                onSelected: index => root.viewMode = index
            }
        }

        GimbalInspectionPanel {
            id: inspectionPanel
            visible: root.gimbalCamera
            Layout.fillWidth: true
            onChooseTargetRequested: root.inspectionTargetRequested()
        }

        Rectangle {
            visible: root.gimbalCamera
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            radius: Theme.radiusMedium
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.line

            Flickable {
                id: gimbalScroller
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                clip: true
                contentWidth: Math.max(width, gimbalControls.implicitWidth)
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: InsetScrollBar {
                    id: gimbalScrollBar
                    policy: gimbalScroller.contentWidth > gimbalScroller.width + 1
                            ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }

                RowLayout {
                    id: gimbalControls
                    width: gimbalScroller.contentWidth
                    height: Math.max(0, gimbalScroller.height
                                        - (gimbalScrollBar.visible
                                           ? Theme.scrollBarExtent : 0))
                    spacing: 8

                    ColumnLayout {
                        Layout.preferredWidth: 126
                        spacing: 0
                        Text {
                            text: App.replayActive ? "云台历史" : "云台视角"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: App.replayActive
                                  ? App.gimbalFeedbackAvailable
                                    ? "实际 " + Number(App.gimbalYawDegrees).toFixed(0) + "° / "
                                      + Number(App.gimbalPitchDegrees).toFixed(0) + "°"
                                    : "此时没有角度反馈"
                                  : App.gimbalMoving ? "正在转向"
                                                   : App.gimbalFeedbackAvailable ? "角度已到位"
                                                                                 : "等待角度反馈"
                            color: App.gimbalMoving ? Theme.accent : Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                        }
                    }

                    Text {
                        text: App.replayActive ? "目标水平" : "水平"
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                    NumericField {
                        Layout.preferredWidth: 104
                        value: App.gimbalTargetYawDegrees
                        from: -180
                        to: 180
                        stepSize: 5
                        decimals: 0
                        unit: "°"
                        accessibleName: "云台水平角度"
                        enabled: root.gimbalControlsReady
                        onValueEdited: value => App.setGimbalAngles(value,
                                                                     App.gimbalTargetPitchDegrees)
                    }
                    Text {
                        text: App.replayActive ? "目标俯仰" : "俯仰"
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                    NumericField {
                        Layout.preferredWidth: 104
                        value: App.gimbalTargetPitchDegrees
                        from: -135
                        to: 45
                        stepSize: 5
                        decimals: 0
                        unit: "°"
                        accessibleName: "云台俯仰角度"
                        enabled: root.gimbalControlsReady
                        onValueEdited: value => App.setGimbalAngles(App.gimbalTargetYawDegrees,
                                                                     value)
                    }

                    Item { Layout.fillWidth: true }

                    CompactIconButton {
                        iconSource: "qrc:/icons/chevron-down.svg"
                        iconRotation: 90
                        accessibleLabel: "云台向左观察"
                        toolTipText: "向左观察"
                        enabled: root.gimbalControlsReady
                        disabledReason: "连接云台数据服务后可用"
                        onClicked: App.setGimbalPreset("left")
                    }
                    CompactIconButton {
                        iconSource: "qrc:/icons/locate-fixed.svg"
                        accessibleLabel: "云台回到正前方"
                        toolTipText: "回到正前方"
                        enabled: root.gimbalControlsReady
                        disabledReason: "连接云台数据服务后可用"
                        onClicked: App.setGimbalPreset("front")
                    }
                    CompactIconButton {
                        iconSource: "qrc:/icons/chevron-down.svg"
                        accessibleLabel: "云台垂直俯视"
                        toolTipText: "垂直俯视"
                        enabled: root.gimbalControlsReady
                        disabledReason: "连接云台数据服务后可用"
                        onClicked: App.setGimbalPreset("down")
                    }
                    CompactIconButton {
                        iconSource: "qrc:/icons/chevron-down.svg"
                        iconRotation: -90
                        accessibleLabel: "云台向右观察"
                        toolTipText: "向右观察"
                        enabled: root.gimbalControlsReady
                        disabledReason: "连接云台数据服务后可用"
                        onClicked: App.setGimbalPreset("right")
                    }
                }
            }
        }

        Rectangle {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 300
            radius: Theme.radiusLarge
            color: Theme.darkMode ? "#0E1721" : "#F4F7FA"
            border.width: 1
            border.color: Theme.line
            clip: true

            Image {
                id: cameraImage
                anchors.fill: parent
                anchors.margins: 1
                anchors.rightMargin: surveyMap.visible ? surveyMap.width + 11 : 1
                source: root.frameSource
                fillMode: Image.PreserveAspectFit
                // Live sources are refreshed several times per second. Asynchronous
                // decoding can keep cancelling the previous data URL before it is
                // displayed, which looks like a permanently blank camera even though
                // frames are being received and recorded. Decode live frames
                // synchronously; replay uses file URLs and can remain asynchronous.
                asynchronous: App.replayActive
                cache: false
                retainWhileLoading: true
                visible: !root.showPointCloud && root.frameReady
                         && (status !== Image.Error || root.hasDecodedFrame)
                onStatusChanged: {
                    if (status === Image.Ready)
                        root.hasDecodedFrame = true
                }
            }

            VisualSurveyMap {
                id: surveyMap
                visible: root.monoCamera && root.surveyExpanded
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 8
                width: Math.max(280, Math.min(390, parent.width * 0.36))
                compact: true
                z: 4
            }

            Item {
                visible: root.gimbalCamera && root.frameDecoded
                         && App.gimbalInspection.active && !App.replayActive
                anchors.centerIn: cameraImage
                width: 28
                height: 28
                Rectangle { anchors.centerIn: parent; width: 28; height: 2; color: Theme.accent }
                Rectangle { anchors.centerIn: parent; width: 2; height: 28; color: Theme.accent }
            }

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 360)
                spacing: 8
                visible: !root.showPointCloud && !root.frameDecoded
                ThemedIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 28
                    height: 28
                    source: "qrc:/icons/camera.svg"
                    iconColor: Theme.textFaint
                }
                Text {
                    width: parent.width
                    text: root.frameDecodeFailed ? "相机画面解码失败"
                          : root.frameReady ? "正在解码相机画面"
                          : App.replayActive && root.streamInterrupted ? "这个时刻相机画面已中断"
                          : App.replayActive ? "这个时刻尚无相机画面"
                                           : App.sensorServiceRunning ? "正在等待相机画面"
                                                                      : "启动仿真后显示画面"
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textBody
                    font.weight: Font.Medium
                }
                Text {
                    width: parent.width
                    text: root.frameDecodeFailed
                          ? "原始数据已收到，可在记录中查看或保存当前帧排查"
                          : App.replayActive ? "拖动回放时间轴可查看已记录画面"
                          : App.sensorServiceEnabled
                          ? "数据服务会随本次仿真启动"
                          : "请先在运行环境中启用 ROS 2 传感器数据"
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 12
                visible: root.showDepth && root.frameReady
                width: depthMetrics.implicitWidth + 22
                height: depthMetrics.implicitHeight + 14
                radius: Theme.radiusMedium
                color: Theme.darkMode ? "#D91B2632" : "#EFFFFFFF"
                border.width: 1
                border.color: Theme.line
                Text {
                    id: depthMetrics
                    anchors.centerIn: parent
                    text: "中心 " + (App.depthCenterDistanceMeters > 0
                                      ? Number(App.depthCenterDistanceMeters).toFixed(1) + " m" : "—")
                          + "   最近 " + (App.depthNearestDistanceMeters > 0
                                          ? Number(App.depthNearestDistanceMeters).toFixed(1) + " m" : "—")
                          + "   有效 " + Math.round(App.depthValidRatio * 100) + "%"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    font.weight: Font.Medium
                }
            }

            Loader {
                anchors.fill: parent
                anchors.margins: 1
                visible: root.showPointCloud
                active: root.viewActive && root.showPointCloud
                sourceComponent: Component {
                    DepthMapView3D { }
                }
            }

            MouseArea {
                id: depthPointer
                anchors.fill: cameraImage
                enabled: root.showDepth && root.frameReady
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
                onPositionChanged: mouse => {
                    // PreserveAspectFit leaves letterboxing. Convert only the
                    // actual painted image rectangle to normalized image data.
                    const paintedX = (width - cameraImage.paintedWidth) / 2
                    const paintedY = (height - cameraImage.paintedHeight) / 2
                    const nx = (mouse.x - paintedX) / Math.max(1, cameraImage.paintedWidth)
                    const ny = (mouse.y - paintedY) / Math.max(1, cameraImage.paintedHeight)
                    root.pointerDistance = nx >= 0 && nx <= 1 && ny >= 0 && ny <= 1
                                           ? App.depthDistanceAt(nx, ny) : 0
                }
                onExited: root.pointerDistance = 0
            }

            Rectangle {
                visible: depthPointer.containsMouse && root.pointerDistance > 0
                x: Math.min(viewport.width - width - 8, depthPointer.mouseX + 14)
                y: Math.max(8, Math.min(viewport.height - height - 8, depthPointer.mouseY + 14))
                width: pointerText.implicitWidth + 18
                height: 30
                radius: 15
                color: Theme.darkMode ? "#E61B2632" : "#F2FFFFFF"
                border.width: 1
                border.color: Theme.lineStrong
                Text {
                    id: pointerText
                    anchors.centerIn: parent
                    text: Number(root.pointerDistance).toFixed(2) + " m"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                visible: App.cameraRecording
                width: recordingText.implicitWidth + 28
                height: 28
                radius: 14
                color: Theme.dangerSoft
                Row {
                    anchors.centerIn: parent
                    spacing: 7
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 7; height: 7; radius: 4
                        color: Theme.danger
                    }
                    Text {
                        id: recordingText
                        text: "正在录像"
                        color: Theme.danger
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                        font.weight: Font.DemiBold
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 12
                visible: root.showDepth && root.frameReady
                width: 250
                height: 30
                radius: 15
                color: Theme.darkMode ? "#E61B2632" : "#F2FFFFFF"
                border.width: 1
                border.color: Theme.lineStrong

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "近"
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0
                        Repeater {
                            model: ["#EF4444", "#F59E0B", "#FDE68A", "#38BDF8", "#2563EB"]
                            Rectangle {
                                required property string modelData
                                width: 20
                                height: 7
                                color: modelData
                            }
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "30 m+"
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 2
                        color: "#121B26"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "无数据"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textTiny
                    }
                }
            }
        }
    }
}
