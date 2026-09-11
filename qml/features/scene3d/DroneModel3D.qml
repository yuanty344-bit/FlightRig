import QtQuick
import QtQuick3D
import FlightBench

// A deliberately light x500-like airframe: central deck, carbon X arms,
// four motors and propellers, landing skids and an unambiguous front marker.
Node {
    id: root
    property color frameColor: Theme.darkMode ? "#273442" : "#3E4853"
    property color shellColor: Theme.darkMode ? "#397FDF" : "#236BC7"
    property color metalColor: Theme.darkMode ? "#7E8FA4" : "#667482"

    // Lower frame and battery tray.
    Model {
        y: -0.015
        source: "#Cube"
        scale: Qt.vector3d(0.0042, 0.0010, 0.0052)
        materials: [ PrincipledMaterial {
            baseColor: root.frameColor
            metalness: 0.18
            roughness: 0.58
        } ]
    }
    Model {
        y: -0.105
        z: 0.025
        source: "#Cube"
        scale: Qt.vector3d(0.0032, 0.00065, 0.0041)
        materials: [ PrincipledMaterial {
            baseColor: Theme.darkMode ? "#111820" : "#20262D"
            roughness: 0.72
        } ]
    }

    // A modest top shell keeps the silhouette closer to a real x500 than an icon.
    Model {
        y: 0.085
        z: -0.015
        source: "#Sphere"
        scale: Qt.vector3d(0.0032, 0.00135, 0.0039)
        materials: [ PrincipledMaterial {
            baseColor: root.shellColor
            roughness: 0.46
        } ]
    }

    // Carbon arms cross through the center and reach each motor directly.
    Repeater3D {
        model: [45, -45]
        delegate: Model {
            required property real modelData
            source: "#Cube"
            scale: Qt.vector3d(0.00072, 0.00042, 0.0152)
            eulerRotation.y: modelData
            materials: [ PrincipledMaterial {
                baseColor: root.frameColor
                metalness: 0.22
                roughness: 0.52
            } ]
        }
    }

    // Four motor pods with crossed blades. Rotors animate only while armed.
    Repeater3D {
        model: [
            Qt.vector3d(-0.54, 0.055, -0.54),
            Qt.vector3d(0.54, 0.055, -0.54),
            Qt.vector3d(-0.54, 0.055, 0.54),
            Qt.vector3d(0.54, 0.055, 0.54)
        ]
        delegate: Node {
            id: motor
            required property vector3d modelData
            property real rotorAngle: 0
            position: modelData

            Model {
                source: "#Cylinder"
                scale: Qt.vector3d(0.00175, 0.00058, 0.00175)
                materials: [ PrincipledMaterial {
                    baseColor: motor.modelData.z < 0 ? root.shellColor : root.metalColor
                    metalness: 0.34
                    roughness: 0.38
                } ]
            }
            Model {
                y: 0.062
                source: "#Cylinder"
                scale: Qt.vector3d(0.00105, 0.00024, 0.00105)
                materials: [ PrincipledMaterial {
                    baseColor: root.metalColor
                    metalness: 0.58
                    roughness: 0.28
                } ]
            }

            NumberAnimation on rotorAngle {
                from: 0
                to: 360
                duration: 150
                loops: Animation.Infinite
                running: App.armed || App.inAir
            }
            Node {
                y: 0.103
                eulerRotation.y: motor.rotorAngle
                Model {
                    source: "#Cube"
                    scale: Qt.vector3d(0.0072, 0.00012, 0.00042)
                    materials: [ DefaultMaterial {
                        diffuseColor: Theme.darkMode ? "#A8B6C6" : "#53606C"
                        opacity: 0.76
                        lighting: DefaultMaterial.NoLighting
                    } ]
                }
                Model {
                    source: "#Cube"
                    scale: Qt.vector3d(0.00042, 0.00012, 0.0072)
                    materials: [ DefaultMaterial {
                        diffuseColor: Theme.darkMode ? "#A8B6C6" : "#53606C"
                        opacity: 0.76
                        lighting: DefaultMaterial.NoLighting
                    } ]
                }
            }
        }
    }

    // Short landing struts and two skids.
    Repeater3D {
        model: [
            Qt.vector3d(-0.22, -0.145, -0.24),
            Qt.vector3d(0.22, -0.145, -0.24),
            Qt.vector3d(-0.22, -0.145, 0.24),
            Qt.vector3d(0.22, -0.145, 0.24)
        ]
        delegate: Model {
            required property vector3d modelData
            position: modelData
            source: "#Cube"
            scale: Qt.vector3d(0.00036, 0.00145, 0.00036)
            materials: [ PrincipledMaterial {
                baseColor: root.metalColor
                metalness: 0.28
                roughness: 0.46
            } ]
        }
    }
    Repeater3D {
        model: [-0.22, 0.22]
        delegate: Model {
            required property real modelData
            x: modelData
            y: -0.29
            source: "#Cube"
            scale: Qt.vector3d(0.00048, 0.00034, 0.0065)
            materials: [ PrincipledMaterial {
                baseColor: root.frameColor
                metalness: 0.20
                roughness: 0.54
            } ]
        }
    }

    // White nose and red tail lamps make orientation readable at a glance.
    Model {
        y: 0.075
        z: -0.31
        source: "#Cube"
        scale: Qt.vector3d(0.0018, 0.00034, 0.00042)
        materials: [ DefaultMaterial {
            diffuseColor: "#F3F7FC"
            lighting: DefaultMaterial.NoLighting
        } ]
    }
    Model {
        y: 0.055
        z: 0.29
        source: "#Cube"
        scale: Qt.vector3d(0.00125, 0.00028, 0.00038)
        materials: [ DefaultMaterial {
            diffuseColor: "#E45D64"
            lighting: DefaultMaterial.NoLighting
        } ]
    }
}
