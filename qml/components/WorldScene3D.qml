import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils
import FlightBench

Node {
    id: root

    // Every packaged non-default city world follows the same lightweight
    // preview contract: buildings.obj, roads.obj, green_areas.obj and water.obj.
    // This keeps future white-model cities data-driven instead of adding a new
    // QML scene for every world.
    readonly property bool isCityWorld: App.selectedWorldId !== "default"
                                        && meshDirectory.length > 0
    readonly property string meshDirectory: String(App.selectedWorldPreviewDirectory)

    visible: isCityWorld

    Model {
        source: "#Cube"
        y: -0.11
        scale: Qt.vector3d(14, 0.002, 17)
        materials: [ PrincipledMaterial {
            baseColor: Theme.darkMode ? "#28312D" : "#D8DDD5"
            roughness: 1
        } ]
    }

    // Source meshes use Gazebo's ENU axes (X east, Y north, Z up). Rotating the
    // loader group maps them to the view's X east, Y up, Z south coordinates.
    Node {
        eulerRotation.x: -90

        RuntimeLoader {
            source: root.meshDirectory.length > 0
                    ? root.meshDirectory + "/buildings.obj" : ""
        }
        RuntimeLoader {
            source: root.meshDirectory.length > 0
                    ? root.meshDirectory + "/roads.obj" : ""
        }
        RuntimeLoader {
            source: root.meshDirectory.length > 0
                    ? root.meshDirectory + "/green_areas.obj" : ""
        }
        RuntimeLoader {
            source: root.meshDirectory.length > 0
                    ? root.meshDirectory + "/water.obj" : ""
        }
    }
}
