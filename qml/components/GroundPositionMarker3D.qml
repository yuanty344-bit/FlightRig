import QtQuick
import QtQuick3D
import FlightBench

// A ground decal for the vehicle's horizontal position. The parent places this
// node just above the highest terrain overlay, so roads, grass and water cannot
// z-fight with it while buildings can still occlude it naturally.
Node {
    id: root
    property real headingDegrees: App.headingDegrees
    // The footprint is a navigational marker rather than a physical object.
    // Scale it with camera distance so it keeps a stable readable size on
    // screen instead of shrinking below a pixel when the user zooms out.
    property real visualScale: 1.0
    property real surfaceOffset: 0.006
    eulerRotation.y: -headingDegrees

    Model {
        y: root.surfaceOffset
        eulerRotation.x: -90
        source: "#Rectangle"
        scale: Qt.vector3d(0.011 * root.visualScale,
                          0.011 * root.visualScale,
                          0.011 * root.visualScale)
        castsShadows: false
        receivesShadows: false
        pickable: false
        materials: [ DefaultMaterial {
            diffuseMap: Texture {
                source: "qrc:/markers/position-marker.svg"
                // Mipmaps made the thin rings disappear intermittently at
                // medium distances. The source is already a small SVG marker.
                generateMipmaps: false
            }
            opacity: 1.0
            lighting: DefaultMaterial.NoLighting
            cullMode: Material.NoCulling
        } ]
    }
}
