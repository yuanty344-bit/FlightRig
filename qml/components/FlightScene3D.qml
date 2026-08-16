import QtQuick
import QtQuick3D
import FlightBench

Item {
    id: root
    property bool followVehicle: true
    property real orbitYaw: -34
    property real orbitPitch: -24
    property real cameraDistance: 18
    property real pressedX: 0
    property real pressedY: 0
    readonly property real vehicleVisualHeight: 0.32
    readonly property string flightPathStatus: {
        if (!App.flightPathVisible)
            return "已隐藏"
        if (App.flightPathPointCount > 1)
            return App.flightPathLengthMeters.toFixed(1) + " m"
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
        cameraDistance = App.selectedWorldId !== "default" ? 72 : 18
    }

    Connections {
        target: App
        function onSelectedWorldChanged() { root.resetView() }
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
            position: root.followVehicle && App.localPositionAvailable
                      ? App.vehicleScenePosition : Qt.vector3d(0, 0, 0)
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
                        clipNear: App.selectedWorldId === "default" ? 0.05 : 0.5
                        clipFar: 2500
                        fieldOfView: 43
                    }
                }
            }
        }
        camera: camera

        Model {
            visible: App.selectedWorldId === "default"
            source: "#Cube"
            y: -0.08
            scale: Qt.vector3d(0.4, 0.001, 0.4)
            materials: [ PrincipledMaterial {
                baseColor: Theme.darkMode ? "#101D2B" : "#EAF0F5"
                roughness: 0.9
            } ]
        }

        Repeater3D {
            visible: App.selectedWorldId === "default"
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
            visible: App.selectedWorldId === "default"
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
            visible: App.selectedWorldId === "default"
            source: "#Cylinder"
            y: 0.015
            scale: Qt.vector3d(0.0015, 0.0003, 0.0015)
            materials: [ DefaultMaterial {
                diffuseColor: Theme.accent
                lighting: DefaultMaterial.NoLighting
            } ]
        }

        WorldScene3D { }

        Model {
            visible: App.flightPathVisible && App.flightPathPointCount > 1
            y: root.vehicleVisualHeight
            geometry: App.flightPathGeometry
            materials: [ DefaultMaterial {
                diffuseColor: root.flightPathColor()
                lighting: DefaultMaterial.NoLighting
                cullMode: Material.NoCulling
            } ]
        }

        GroundPositionMarker3D {
            visible: App.localPositionAvailable
            x: App.vehicleScenePosition.x
            y: 0
            z: App.vehicleScenePosition.z
        }

        DroneModel3D {
            visible: App.localPositionAvailable
            position: Qt.vector3d(App.vehicleScenePosition.x,
                                  App.vehicleScenePosition.y + root.vehicleVisualHeight,
                                  App.vehicleScenePosition.z)
            rotation: App.vehicleSceneRotation
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: mouse => {
            root.pressedX = mouse.x
            root.pressedY = mouse.y
        }
        onPositionChanged: mouse => {
            if (!pressed)
                return
            root.orbitYaw -= (mouse.x - root.pressedX) * 0.32
            root.orbitPitch = Math.max(-78, Math.min(-8,
                                      root.orbitPitch - (mouse.y - root.pressedY) * 0.24))
            root.pressedX = mouse.x
            root.pressedY = mouse.y
        }
        onWheel: wheel => {
            root.cameraDistance = Math.max(5, Math.min(
                                        App.selectedWorldId !== "default" ? 700 : 70,
                                        root.cameraDistance - wheel.angleDelta.y / 80))
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
                { label: "高度", value: App.telemetryAvailable ? App.altitudeMeters.toFixed(1) + " m" : "—" },
                { label: "速度", value: App.telemetryAvailable ? App.speedMetersPerSecond.toFixed(1) + " m/s" : "—" },
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
                Text {
                    id: metricText
                    anchors.centerIn: parent
                    text: modelData.label + "  " + modelData.value
                    color: modelData.path && App.flightPathPointCount > 1
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
            MouseArea { id: followMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.followVehicle = !root.followVehicle }
        }
        Rectangle {
            width: 58
            height: 30
            radius: Theme.radiusSmall
            color: resetMouse.containsMouse ? Theme.surfaceInteractive : Theme.surfaceRaised
            border.width: 1
            border.color: Theme.line
            Text { anchors.centerIn: parent; text: "重置"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
            MouseArea { id: resetMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.resetView() }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        text: App.localPositionAvailable ? "X 东 / Y 上 / Z 南  ·  拖动旋转  ·  滚轮缩放" : "等待本地位置遥测"
        color: Theme.textFaint
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textTiny
    }
}
