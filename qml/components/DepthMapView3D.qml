pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick3D
import FlightBench

Item {
    id: root

    property real cameraDistance: 24
    property vector3d focusPoint: App.depthMapping.mapCenter
    property real panX: 0
    property real panY: 0
    property bool hasAutoFit: false

    function resetView() {
        const extent = Math.max(App.depthMapping.widthMeters,
                                App.depthMapping.depthMeters,
                                App.depthMapping.heightMeters)
        root.cameraDistance = Math.max(10, Math.min(80, extent * 1.45 + 8))
        root.panX = 0
        root.panY = 0
        orbitNode.eulerRotation = Qt.vector3d(-32, 42, 0)
        App.depthMapping.refreshVisualization()
    }

    Component.onCompleted: {
        App.depthMapping.setVisualizationActive(root.visible)
        resetView()
    }
    onVisibleChanged: App.depthMapping.setVisualizationActive(root.visible)
    Component.onDestruction: App.depthMapping.setVisualizationActive(false)

    Connections {
        target: App.depthMapping
        function onMapChanged() {
            if (App.depthMapping.available && !root.hasAutoFit) {
                root.hasAutoFit = true
                root.resetView()
            } else if (!App.depthMapping.available) {
                root.hasAutoFit = false
            }
        }
    }

    View3D {
        anchors.fill: parent
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: Theme.darkMode ? "#101922" : "#EEF3F8"
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.Medium
        }

        Node {
            id: focusNode
            position: Qt.vector3d(root.focusPoint.x + root.panX,
                                  root.focusPoint.y + root.panY,
                                  root.focusPoint.z)
            Node {
                id: orbitNode
                eulerRotation: Qt.vector3d(-32, 42, 0)
                PerspectiveCamera {
                    id: mapCamera
                    z: root.cameraDistance
                    clipNear: 0.1
                    clipFar: 400
                    fieldOfView: 48
                }
            }
        }

        DirectionalLight {
            eulerRotation.x: -48
            eulerRotation.y: -28
            brightness: 1.0
            ambientColor: Theme.darkMode ? "#405064" : "#B8C5D2"
        }

        Model {
            visible: App.depthMapping.available
            geometry: App.depthMapping.obstacleGeometry
            materials: DefaultMaterial {
                diffuseColor: Theme.darkMode ? "#4FC3E8" : "#2388B8"
                lighting: DefaultMaterial.FragmentLighting
                specularAmount: 0.15
            }
        }
        Model {
            geometry: App.depthMapping.dangerGeometry
            materials: DefaultMaterial {
                diffuseColor: "#EF5350"
                lighting: DefaultMaterial.NoLighting
            }
        }

        Model {
            source: "#Cube"
            visible: App.depthMapping.available
            position: Qt.vector3d(App.depthMapping.mapCenter.x,
                                  Math.max(0, App.depthMapping.mapCenter.y
                                           - App.depthMapping.heightMeters * 0.5 - 0.18),
                                  App.depthMapping.mapCenter.z)
            scale: Qt.vector3d(Math.max(0.08, App.depthMapping.widthMeters / 100),
                               0.001,
                               Math.max(0.08, App.depthMapping.depthMeters / 100))
            materials: DefaultMaterial {
                diffuseColor: Theme.darkMode ? "#263545" : "#D7E0E8"
                opacity: 0.58
                lighting: DefaultMaterial.NoLighting
            }
        }

        Model {
            source: "#Sphere"
            position: App.replayActive ? App.replayPosition : App.vehicleScenePosition
            scale: Qt.vector3d(0.006, 0.006, 0.006)
            materials: DefaultMaterial {
                diffuseColor: Theme.accent
                lighting: DefaultMaterial.NoLighting
            }
        }
    }

    MouseArea {
        id: navigation
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property real lastX: 0
        property real lastY: 0
        onPressed: mouse => { lastX = mouse.x; lastY = mouse.y }
        onPositionChanged: mouse => {
            if (!pressed) return
            const dx = mouse.x - lastX
            const dy = mouse.y - lastY
            if (pressedButtons & Qt.RightButton) {
                const scale = root.cameraDistance / Math.max(400, width)
                root.panX -= dx * scale
                root.panY += dy * scale
            } else {
                orbitNode.eulerRotation.y += dx * 0.35
                orbitNode.eulerRotation.x = Math.max(-85, Math.min(-5,
                                                orbitNode.eulerRotation.x + dy * 0.28))
            }
            lastX = mouse.x
            lastY = mouse.y
        }
        onWheel: wheel => {
            root.cameraDistance = Math.max(4, Math.min(120,
                root.cameraDistance * (wheel.angleDelta.y > 0 ? 0.88 : 1.14)))
            wheel.accepted = true
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        width: statusColumn.implicitWidth + 24
        height: statusColumn.implicitHeight + 18
        radius: Theme.radiusMedium
        color: Theme.darkMode ? "#E61B2632" : "#F2FFFFFF"
        border.width: 1
        border.color: Theme.line
        Column {
            id: statusColumn
            anchors.centerIn: parent
            spacing: 2
            Text {
                text: App.depthMapping.safetyLabel
                color: App.depthMapping.safetyLevel === "danger" ? Theme.danger
                       : App.depthMapping.safetyLevel === "caution" ? Theme.warning
                                                                    : Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                font.weight: Font.DemiBold
            }
            Text {
                text: App.depthMapping.corridorClearanceMeters > 0
                      ? "前向走廊 " + App.depthMapping.corridorClearanceMeters.toFixed(1) + " m"
                      : "前向走廊内未发现障碍"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        width: 218
        height: metrics.implicitHeight + 20
        radius: Theme.radiusMedium
        color: Theme.darkMode ? "#E61B2632" : "#F2FFFFFF"
        border.width: 1
        border.color: Theme.line
        Column {
            id: metrics
            anchors.fill: parent
            anchors.margins: 10
            spacing: 5
            Text {
                text: App.depthMapping.loadedFromDisk ? "历史 DepthFusion 地图"
                                                     : "DepthFusion 三维地图"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                font.weight: Font.DemiBold
            }
            Text {
                text: "稳定体素 " + App.depthMapping.confirmedVoxelCount
                      + " · 分块 " + App.depthMapping.chunkCount
                color: Theme.textSecondary; font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
            Text {
                text: "范围  " + App.depthMapping.widthMeters.toFixed(1) + " × "
                      + App.depthMapping.depthMeters.toFixed(1) + " × "
                      + App.depthMapping.heightMeters.toFixed(1) + " m"
                color: Theme.textSecondary; font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
            Text {
                text: "融合帧 " + App.depthMapping.processedFrameCount
                      + " · 地面过滤 " + App.depthMapping.filteredGroundPointCount
                color: Theme.textSecondary; font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
            Text {
                text: "置信度 " + App.depthMapping.mapConfidencePercent.toFixed(0)
                      + "% · 噪声 " + App.depthMapping.noisePercent.toFixed(1)
                      + "% · 已清除 " + App.depthMapping.clearedVoxelCount
                color: Theme.textSecondary; font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
            Text {
                text: App.depthMapping.groundPlaneAvailable
                      ? "地面坡度 " + App.depthMapping.groundSlopeDegrees.toFixed(1)
                        + "° · 网格 " + App.depthMapping.meshTriangleCount + " 面"
                      : "正在估计地面 · 网格 " + App.depthMapping.meshTriangleCount + " 面"
                color: Theme.textSecondary; font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        spacing: 8
        CompactIconButton {
            iconSource: "qrc:/icons/locate-fixed.svg"
            accessibleLabel: "重置三维地图视角"
            toolTipText: "重置视角"
            onClicked: root.resetView()
        }
        CompactIconButton {
            iconSource: "qrc:/icons/file-down.svg"
            accessibleLabel: "保存三维障碍地图"
            toolTipText: "保存点云、CSV、OBJ 网格和预览图"
            enabled: App.depthMapping.available
            disabledReason: "形成经过重复确认的障碍后才能保存"
            onClicked: App.saveDepthMap()
        }
        CompactIconButton {
            iconSource: "qrc:/icons/folder-open.svg"
            accessibleLabel: "打开三维障碍地图文件夹"
            toolTipText: "打开地图文件夹"
            onClicked: App.openDepthMapsFolder()
        }
        CompactIconButton {
            iconSource: "qrc:/icons/trash-2.svg"
            accessibleLabel: "清空三维障碍地图"
            toolTipText: "清空地图"
            enabled: App.depthMapping.available
            disabledReason: "当前地图为空"
            onClicked: clearDialog.open()
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: Math.max(180, parent.width - 280)
        text: "左键旋转 · 右键平移 · 滚轮缩放 · TSDF 融合表面 · 射线自动清除幽灵障碍"
        elide: Text.ElideRight
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textTiny
    }

    Dialog {
        id: clearDialog
        anchors.centerIn: parent
        modal: true
        padding: 22
        title: "清空三维障碍地图？"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: App.depthMapping.clear()
    }
}
