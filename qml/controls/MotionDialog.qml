import QtQuick
import QtQuick.Controls.Basic
import FlightBench

Dialog {
    id: root

    background: DialogSurface { }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motionNormal
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                property: "scale"
                from: Theme.reducedMotion ? 1 : 0.975
                to: 1
                duration: Theme.motionNormal
                easing.type: Easing.OutCubic
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.motionFast
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                property: "scale"
                from: 1
                to: Theme.reducedMotion ? 1 : 0.985
                duration: Theme.motionFast
                easing.type: Easing.InCubic
            }
        }
    }

    Overlay.modal: Rectangle {
        color: Theme.overlay
    }
}
