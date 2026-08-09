import QtQuick
import QtQuick3D
import FlightBench

// A compact position footprint: two light rings and a centered concave heading
// dart. It stays flat on the grid and rotates with the live heading.
Node {
    eulerRotation.y: -App.headingDegrees

    Model {
        y: 0.032
        eulerRotation.x: -90
        source: "#Rectangle"
        scale: Qt.vector3d(0.011, 0.011, 0.011)
        materials: [ DefaultMaterial {
            diffuseMap: Texture {
                source: "qrc:/markers/position-marker.svg"
                generateMipmaps: true
            }
            opacity: 0.96
            lighting: DefaultMaterial.NoLighting
            cullMode: Material.NoCulling
        } ]
    }
}
