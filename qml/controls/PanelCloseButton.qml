import QtQuick
import FlightBench

CompactIconButton {
    implicitWidth: Theme.closeButtonExtent
    implicitHeight: Theme.closeButtonExtent
    iconSource: "qrc:/icons/x.svg"
    outlined: true
    accessibleLabel: "关闭"
    toolTipText: "关闭"
}
