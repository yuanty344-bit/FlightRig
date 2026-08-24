import QtQuick
import QtQuick3D
import FlightBench

Item {
    id: root
    signal replayAnalysisRequested()
    property bool followVehicle: App.defaultCameraMode !== "free"
    property real orbitYaw: -34
    property real orbitPitch: -24
    property real cameraDistance: 18
    property real pressedX: 0
    property real pressedY: 0
    property vector3d panOffset: Qt.vector3d(0, 0, 0)
    readonly property real vehicleVisualHeight: 0.32
    // MAVSDK local-position telemetry arrives at 10 Hz.  Interpolating the
    // rendered pose over a little more than one sample interval keeps the
    // model, marker and follow camera fluid without altering recorded data.
    readonly property int livePoseSmoothingMs: 125
    readonly property bool replayMode: App.replayActive
    readonly property bool positionAvailable: replayMode || App.localPositionAvailable
    readonly property vector3d scenePosition: replayMode ? App.replayPosition : App.vehicleScenePosition
    readonly property quaternion sceneRotation: replayMode ? App.replayRotation : App.vehicleSceneRotation
    readonly property real sceneHeading: replayMode ? App.replayHeadingDegrees : App.headingDegrees
    readonly property string activeWorldId: replayMode ? App.replayWorldId : App.selectedWorldId
    readonly property string activeWorldPreviewDirectory: String(replayMode
                                                                  ? App.replayWorldPreviewDirectory
                                                                  : App.selectedWorldPreviewDirectory)
    readonly property int activePathPointCount: replayMode ? App.replayPathPointCount : App.flightPathPointCount
    readonly property real activePathLengthMeters: replayMode ? App.replayPathLengthMeters : App.flightPathLengthMeters
    readonly property real groundMarkerSizeFactor:
        App.groundMarkerSizeMode === "small" ? 0.78
        : App.groundMarkerSizeMode === "large" ? 1.35
        : App.groundMarkerSizeMode === "medium" ? 1.0
        : Math.max(0.85, Math.min(1.4, cameraDistance / 18.0))
    readonly property real groundMarkerScale: groundMarkerSizeFactor
    // Generated city surfaces top out at 0.14 m (road mesh + layer offset).
    // Keeping the decal 1 cm above that stack is visually indistinguishable
    // from the ground, but prevents flicker and terrain-layer occlusion.
    readonly property real groundMarkerSurfaceY:
        activeWorldId === "default" ? 0.01 : 0.15
    readonly property string flightPathStatus: {
        if (!App.flightPathVisible)
            return "已隐藏"
        if (root.activePathPointCount > 1)
            return root.activePathLengthMeters.toFixed(1) + " m"
        if (root.replayMode)
            return "等待播放"
        if (!App.simulationRunning)
            return "未开始"
        if (!App.vehicleConnected)
            return "等待连接"
        if (!App.armed && !App.inAir)
            return "等待起飞"
        return "正在记录"
    }

    function flightPathColor() {
        return Theme.flightPathColor(App.flightPathColorPreset)
    }

    function resetView() {
        orbitYaw = -34
        orbitPitch = -24
        cameraDistance = root.activeWorldId !== "default" ? 72 : 18
        panOffset = Qt.vector3d(0, 0, 0)
        followVehicle = App.defaultCameraMode !== "free"
    }

    function setFollowVehicle(value) {
        if (!value && followVehicle)
            panOffset = cameraFocus.position
        followVehicle = value
    }

    Connections {
        target: App
        function onSelectedWorldChanged() { root.resetView() }
        function onReplayStateChanged() { root.followVehicle = true; root.resetView() }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: Theme.darkMode ? "#0B1522" : "#F5F8FB"
        border.width: 1
        border.color: Theme.line
    }

    View3D {
        id: view
        anchors.fill: parent
        anchors.margins: 1

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: Theme.darkMode ? "#0B1522" : "#F5F8FB"
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            tonemapMode: SceneEnvironment.TonemapModeLinear
        }

        DirectionalLight {
            eulerRotation.x: -48
            eulerRotation.y: -32
            brightness: 0.95
            ambientColor: Theme.darkMode ? "#52657D" : "#A9B8C8"
            castsShadow: true
            shadowFactor: 35
        }

        Node {
            id: cameraFocus
            position: root.followVehicle && root.positionAvailable
                      ? Qt.vector3d(root.scenePosition.x,
                                    App.defaultCameraMode === "follow-both"
                                    ? root.scenePosition.y * 0.5
                                    : root.scenePosition.y,
                                    root.scenePosition.z)
                      : root.panOffset
            Behavior on position {
                enabled: !root.replayMode && root.positionAvailable && root.followVehicle
                Vector3dAnimation {
                    duration: root.livePoseSmoothingMs
                    easing.type: Easing.Linear
                }
            }
            Node {
                eulerRotation.y: root.orbitYaw
                Node {
                    eulerRotation.x: root.orbitPitch
                    PerspectiveCamera {
                        id: camera
                        z: root.cameraDistance
                        // City meshes span kilometres.  A larger near plane
                        // preserves enough depth precision for roads, water and
                        // green areas without affecting the tactical view.
                        clipNear: root.activeWorldId === "default" ? 0.05 : 0.5
                        clipFar: 2500
                        fieldOfView: 43
                    }
                }
            }
        }
        camera: camera

        Model {
            visible: root.activeWorldId === "default" && App.defaultGridVisible
            source: "#Cube"
            y: -0.08
            scale: Qt.vector3d(0.4, 0.001, 0.4)
            materials: [ PrincipledMaterial {
                baseColor: Theme.darkMode ? "#101D2B" : "#EAF0F5"
                roughness: 0.9
            } ]
        }

        Repeater3D {
            visible: root.activeWorldId === "default" && App.defaultGridVisible
            model: 41
            delegate: Model {
                required property int index
                source: "#Cube"
                x: index - 20
                y: -0.025
                scale: Qt.vector3d(index === 20 ? 0.0005 : 0.00016, 0.0005, 0.4)
                materials: [ DefaultMaterial {
                    diffuseColor: index === 20
                                  ? (Theme.darkMode ? "#315C87" : "#91B3D4")
                                  : Theme.line
                    lighting: DefaultMaterial.NoLighting
                } ]
            }
        }
        Repeater3D {
            visible: root.activeWorldId === "default" && App.defaultGridVisible
            model: 41
            delegate: Model {
                required property int index
                source: "#Cube"
                z: index - 20
                y: -0.024
                scale: Qt.vector3d(0.4, 0.0005, index === 20 ? 0.0005 : 0.00016)
                materials: [ DefaultMaterial {
                    diffuseColor: index === 20
                                  ? (Theme.darkMode ? "#315C87" : "#91B3D4")
                                  : Theme.line
                    lighting: DefaultMaterial.NoLighting
                } ]
            }
        }

        Model {
            visible: root.activeWorldId === "default"
            source: "#Cylinder"
            y: 0.015
            scale: Qt.vector3d(0.0015, 0.0003, 0.0015)
            materials: [ DefaultMaterial {
                diffuseColor: Theme.accent
                lighting: DefaultMaterial.NoLighting
            } ]
        }

        WorldScene3D {
            worldId: root.activeWorldId
            meshDirectory: root.activeWorldPreviewDirectory
        }

        Node {
            id: groundMarkerAnchor
            visible: App.groundMarkerVisible && root.positionAvailable
            position: Qt.vector3d(root.scenePosition.x,
                                  root.groundMarkerSurfaceY,
                                  root.scenePosition.z)
            Behavior on position {
                enabled: !root.replayMode && root.positionAvailable
                Vector3dAnimation {
                    duration: root.livePoseSmoothingMs
                    easing.type: Easing.Linear
                }
            }
            GroundPositionMarker3D {
                headingDegrees: root.sceneHeading
                visualScale: root.groundMarkerScale
            }
        }

        Model {
            visible: App.flightPathVisible && root.activePathPointCount > 1
            y: root.vehicleVisualHeight
            geometry: root.replayMode ? App.replayPathGeometry : App.flightPathGeometry
            materials: [ DefaultMaterial {
                diffuseColor: root.flightPathColor()
                lighting: DefaultMaterial.NoLighting
                cullMode: Material.NoCulling
            } ]
        }

        Model {
            visible: root.replayMode && App.replayPlannedPathPointCount > 1
            y: root.vehicleVisualHeight
            geometry: App.replayPlannedPathGeometry
            materials: [ DefaultMaterial {
                diffuseColor: Theme.textMuted
                opacity: 0.72
                lighting: DefaultMaterial.NoLighting
                cullMode: Material.NoCulling
            } ]
        }

        Node {
            id: vehicleVisualAnchor
            visible: root.positionAvailable
            position: root.scenePosition
            Behavior on position {
                enabled: !root.replayMode && root.positionAvailable
                Vector3dAnimation {
                    duration: root.livePoseSmoothingMs
                    easing.type: Easing.Linear
                }
            }
            DroneModel3D {
                y: root.vehicleVisualHeight
                rotation: root.sceneRotation
                Behavior on rotation {
                    enabled: !root.replayMode && root.positionAvailable
                    QuaternionAnimation {
                        duration: root.livePoseSmoothingMs
                        easing.type: Easing.Linear
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property bool panning: false
        onPressed: mouse => {
            root.pressedX = mouse.x
            root.pressedY = mouse.y
            if (mouse.button === Qt.RightButton) {
                panning = true
                root.setFollowVehicle(false)
            }
            if (!App.keyboardControlActive)
                root.forceActiveFocus(Qt.MouseFocusReason)
        }
        onReleased: panning = false
        onPositionChanged: mouse => {
            if (!pressed)
                return
            if (panning) {
                const fovRadians = 43 * Math.PI / 180
                const visibleHeight = 2 * root.cameraDistance * Math.tan(fovRadians / 2)
                const metersPerPixel = visibleHeight / Math.max(1, height)
                const yawRadians = root.orbitYaw * Math.PI / 180
                const pitchRadians = root.orbitPitch * Math.PI / 180
                const rightX = Math.cos(yawRadians)
                const rightZ = -Math.sin(yawRadians)
                const upX = Math.sin(pitchRadians) * Math.sin(yawRadians)
                const upY = Math.cos(pitchRadians)
                const upZ = Math.sin(pitchRadians) * Math.cos(yawRadians)
                const deltaX = mouse.x - root.pressedX
                const deltaY = mouse.y - root.pressedY
                root.panOffset = Qt.vector3d(
                    root.panOffset.x - rightX * deltaX * metersPerPixel
                                    + upX * deltaY * metersPerPixel,
                    root.panOffset.y + upY * deltaY * metersPerPixel,
                    root.panOffset.z - rightZ * deltaX * metersPerPixel
                                    + upZ * deltaY * metersPerPixel)
                root.pressedX = mouse.x
                root.pressedY = mouse.y
                return
            }
            root.orbitYaw -= (mouse.x - root.pressedX) * 0.32
                             * App.cameraRotationSensitivity
            const verticalDirection = App.invertCameraVertical ? -1 : 1
            root.orbitPitch = Math.max(-78, Math.min(-8,
                                      root.orbitPitch
                                      - (mouse.y - root.pressedY) * 0.24
                                        * App.cameraRotationSensitivity
                                        * verticalDirection))
            root.pressedX = mouse.x
            root.pressedY = mouse.y
        }
        onWheel: wheel => {
            root.cameraDistance = Math.max(5, Math.min(
                                        root.activeWorldId !== "default" ? 700 : 70,
                                        root.cameraDistance
                                        - wheel.angleDelta.y / 80
                                          * App.cameraZoomSensitivity))
            wheel.accepted = true
        }
    }

    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 6

        Repeater {
            model: [
                { label: "高度", value: root.replayMode ? App.replayAltitudeMeters.toFixed(1) + " m" : App.telemetryAvailable ? App.altitudeMeters.toFixed(1) + " m" : "—" },
                { label: "速度", value: root.replayMode ? App.replaySpeedMetersPerSecond.toFixed(1) + " m/s" : App.telemetryAvailable ? App.speedMetersPerSecond.toFixed(1) + " m/s" : "—" },
                { label: "轨迹", value: root.flightPathStatus, path: true }
            ]
            delegate: Rectangle {
                required property var modelData
                width: metricText.implicitWidth + 18
                height: 30
                radius: Theme.radiusSmall
                color: Theme.darkMode ? "#D90E1928" : "#EFFFFFFF"
                border.width: 1
                border.color: Theme.line
                Behavior on width { NumberAnimation { duration: Theme.animationDuration(140); easing.type: Easing.OutCubic } }
                Text {
                    id: metricText
                    anchors.centerIn: parent
                    text: modelData.label + "  " + modelData.value
                    color: modelData.path && root.activePathPointCount > 1
                           ? root.flightPathColor()
                           : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                }
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 6

        Rectangle {
            width: 58
            height: 30
            radius: Theme.radiusSmall
            color: followMouse.containsMouse ? Theme.surfaceInteractive : Theme.surfaceRaised
            border.width: 1
            border.color: root.followVehicle ? Theme.accent : Theme.line
            Text { anchors.centerIn: parent; text: root.followVehicle ? "跟随" : "自由"; color: root.followVehicle ? Theme.accentHover : Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
            MouseArea { id: followMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { root.setFollowVehicle(!root.followVehicle); if (!App.keyboardControlActive) root.forceActiveFocus(Qt.MouseFocusReason) } }
        }
        Rectangle {
            width: 58
            height: 30
            radius: Theme.radiusSmall
            color: resetMouse.containsMouse ? Theme.surfaceInteractive : Theme.surfaceRaised
            border.width: 1
            border.color: Theme.line
            Text { anchors.centerIn: parent; text: "重置"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
            MouseArea { id: resetMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { root.resetView(); if (!App.keyboardControlActive) root.forceActiveFocus(Qt.MouseFocusReason) } }
        }
    }

    Rectangle {
        visible: root.replayMode && root.activePathPointCount > 1
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 12
        anchors.topMargin: 48
        width: App.replayPlannedPathPointCount > 1 ? 174 : 86
        height: 26
        radius: Theme.radiusSmall
        color: Theme.darkMode ? "#D90E1928" : "#EFFFFFFF"
        border.width: 1
        border.color: Theme.line

        Row {
            anchors.centerIn: parent
            spacing: 7
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 2
                radius: 1
                color: root.flightPathColor()
            }
            Text {
                text: "实际"
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
            Item { width: 4; height: 1; visible: App.replayPlannedPathPointCount > 1 }
            Row {
                visible: App.replayPlannedPathPointCount > 1
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Repeater {
                    model: 3
                    Rectangle { width: 4; height: 2; radius: 1; color: Theme.textMuted }
                }
            }
            Text {
                visible: App.replayPlannedPathPointCount > 1
                text: "计划"
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
        }
    }

    ReplayTelemetryChart {
        visible: root.replayMode && root.width >= 720
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        z: 4
        onExpandRequested: root.replayAnalysisRequested()
    }

    Rectangle {
        visible: !root.positionAvailable
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 300)
        height: 64
        radius: Theme.radiusMedium
        color: Theme.darkMode ? "#E6171F2A" : "#F2FFFFFF"
        border.width: 1
        border.color: Theme.line
        z: 5

        Column {
            anchors.centerIn: parent
            spacing: 3
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: App.simulationRunning ? "等待飞行器位置" : "三维场景已就绪"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSmall
                font.weight: Font.Medium
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: App.simulationRunning ? "建立本地位置后将显示无人机和实时轨迹"
                                            : "启动仿真后会在这里显示无人机"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        text: root.positionAvailable ? "X 东 / Y 上 / Z 南  ·  左键旋转  ·  右键平移  ·  滚轮缩放" : "等待本地位置遥测"
        color: Theme.textFaint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textTiny
    }
}
