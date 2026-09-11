import QtQuick
import QtTest
import FlightBench
import "../../qml/features/mission"
import "../../qml/features/records"
import "../../qml/features/mission/RouteMapPainter.js" as RouteMapPainter

TestCase {
    id: testCase
    name: "FeaturePanels"
    visible: true
    when: windowShown
    width: 900
    height: 850
    ListModel {
        id: points
        ListElement { name: "起点"; altitudeMeters: 8; speedMetersPerSecond: 4; yawDegrees: 0; eastMeters: 0; northMeters: 0; gimbalYawDegrees: 0; gimbalPitchDegrees: -45; capturePhoto: false }
        ListElement { name: "终点"; altitudeMeters: 12; speedMetersPerSecond: 3; yawDegrees: 90; eastMeters: 10; northMeters: 20; gimbalYawDegrees: 10; gimbalPitchDegrees: -30; capturePhoto: true }
    }
    WaypointEditorPanel {
        id: editor
        width: 252
        height: 780
        selectedIndex: 0
        selectionValid: selectedIndex >= 0 && selectedIndex < points.count
        pointCount: points.count
        point: selectionValid ? points.get(selectedIndex) : ({})
        onPointValueEdited: (role, value) => points.setProperty(selectedIndex, role, value)
    }
    SessionSensorSummary {
        id: summary
        x: 270
        width: 600
        height: 128
        canOpenHistory: true
        canDeleteData: true
    }
    SignalSpy { id: edits; target: editor; signalName: "pointValueEdited" }
    SignalSpy { id: actions; target: editor; signalName: "endActionSelected" }
    SignalSpy { id: surveySpy; target: summary; signalName: "visualSurveyRequested" }
    SignalSpy { id: depthSpy; target: summary; signalName: "depthMapRequested" }
    SignalSpy { id: deleteSpy; target: summary; signalName: "deleteRequested" }
    RouteLibraryDialog {
        id: library
        host: testCase
        durationFormatter: seconds => String(seconds)
    }
    SignalSpy { id: routeSelection; target: library; signalName: "routeSelected" }
    SignalSpy { id: routeLoad; target: library; signalName: "loadRequested" }
    SignalSpy { id: routeDuplicate; target: library; signalName: "duplicateRequested" }
    MissionMapInteraction { id: mapInput; x: 300; y: 200; width: 300; height: 200 }
    SignalSpy { id: pointSpy; target: mapInput; signalName: "pointRequested" }
    SignalSpy { id: panSpy; target: mapInput; signalName: "panRequested" }
    SignalSpy { id: focusSpy; target: mapInput; signalName: "focusRequested" }
    Item {
        id: markerSurface
        x: 300; y: 430; width: 250; height: 200
        Repeater {
            id: markers
            model: points
            delegate: WaypointMapMarker {
                dragSurface: markerSurface
                x: 30 + eastMeters * 2
                y: 70 - northMeters * 2
            }
        }
    }
    SignalSpy { id: markerSelection; signalName: "selectedRequested" }
    SignalSpy { id: markerDrag; signalName: "dragged" }
    SignalSpy { id: markerRelease; signalName: "dragFinished" }
    function test_markerInteraction() {
        const marker = markers.itemAt(0)
        verify(marker !== null)
        compare(marker.index, 0)
        compare(marker.eastMeters, points.get(0).eastMeters)
        markerSelection.target=marker; markerDrag.target=marker; markerRelease.target=marker
        markerSelection.clear(); markerDrag.clear(); markerRelease.clear()
        marker.editingLocked=false
        mouseDrag(marker, 18, 12, 20, 10, Qt.LeftButton)
        compare(markerSelection.count, 1)
        verify(markerDrag.count > 0)
        compare(markerRelease.count, 1)
        const args=markerDrag.signalArguments[markerDrag.count-1]
        compare(args[0], points.get(0).eastMeters)
        compare(args[1], points.get(0).northMeters)
        compare(args[2], 20)
        compare(args[3], 10)
        marker.selected=true
        compare(marker.z, 6)
        marker.editingLocked=true
        markerSelection.clear(); markerDrag.clear()
        mouseDrag(marker, 18, 12, 20, 10, Qt.LeftButton)
        compare(markerSelection.count, 0)
        compare(markerDrag.count, 0)
        marker.editingLocked=false
        marker.selected=false
        compare(marker.z, 5)
    }
    function test_mapInteraction() {
        pointSpy.clear(); panSpy.clear(); focusSpy.clear()
        mapInput.editingLocked = false
        mouseClick(mapInput, 30, 30)
        compare(pointSpy.count, 1)
        compare(focusSpy.count, 1)
        compare(pointSpy.signalArguments[0][0], 30)
        mouseClick(mapInput, 30, 30, Qt.MiddleButton)
        compare(pointSpy.count, 1)
        mouseDrag(mapInput, 30, 30, 40, 20, Qt.LeftButton)
        verify(panSpy.count > 0)
        compare(pointSpy.count, 1)
        mapInput.editingLocked = true
        mouseClick(mapInput, 30, 30)
        compare(pointSpy.count, 1)
        panSpy.clear()
        mouseDrag(mapInput, 30, 30, 40, 20, Qt.MiddleButton)
        verify(panSpy.count > 0) // Viewing remains available while editing is locked.
        compare(pointSpy.count, 1)
        mapInput.editingLocked = false
    }
    function test_routeLibrary() {
        library.routes = []
        library.open()
        tryCompare(library, "opened", true)
        compare(library.height, 320)
        library.routes = [{id:"test-route",name:"测试航线",waypointCount:3,distanceMeters:40,estimatedSeconds:20,updatedLabel:"今天"}]
        wait(30)
        compare(library.height, 440)
        const row = findChildWhere(library.contentItem, item => typeof item.selectRoute === "function")
        verify(row !== null)
        routeSelection.clear(); routeLoad.clear(); routeDuplicate.clear()
        row.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(routeSelection.count, 1)
        compare(routeSelection.signalArguments[0][0], "test-route")
        compare(library.selectedRouteId, "") // Selection belongs to the planner.
        keyClick(Qt.Key_Return)
        compare(routeLoad.count, 1)
        compare(routeLoad.signalArguments[0][0], "test-route")
        library.selectedRouteId = "test-route"
        const duplicate = findChildWhere(library.contentItem, item => item.accessibleLabel === "复制所选航线")
        verify(duplicate !== null)
        mouseClick(duplicate)
        compare(routeDuplicate.count, 1)
        library.editingLocked = true
        tryCompare(duplicate, "enabled", false)
        mouseClick(duplicate)
        compare(routeDuplicate.count, 1)
        library.routes = []
        library.selectedRouteId = ""
        wait(20)
        compare(library.height, 320)
        library.close()
        library.editingLocked = false
    }
    function findChildWhere(item, predicate) {
        if (predicate(item)) return item
        for (const child of item.children || []) {
            const result = findChildWhere(child, predicate)
            if (result) return result
        }
        return null
    }
    function init() {
        failOnWarning(/.*(recursive rearrange|Binding loop|Unable to assign|is not defined).*/)
        editor.editingLocked = false
        editor.selectedIndex = 0
        editor.gimbalMission = false
        summary.canOpenHistory = true
        edits.clear(); actions.clear()
    }
    function test_mapPainter() {
        const strokes = [], arcs = []
        let path = [], dash = []
        const context = {
            reset: function() {}, clearRect: function() {},
            beginPath: function() { path = [] },
            moveTo: function(x,y) { path.push(["move",x,y]) },
            lineTo: function(x,y) { path.push(["line",x,y]) },
            setLineDash: function(values) { dash = values },
            stroke: function() { strokes.push({path:path.slice(),dash:dash.slice(),color:this.strokeStyle,alpha:this.globalAlpha,width:this.lineWidth}) },
            fill: function() {},
            arc: function(x,y,r) { arcs.push([x,y,r]) }
        }
        const palette = {line:"grid",danger:"risk",accent:"active",textSecondary:"plan",surface:"surface"}
        const state = {
            gridStepMeters: () => 100,
            eastAt: x => x-50, northAt: y => 50-y,
            mapX: east => east+50, mapY: north => 50-north,
            worldMapAvailable: false,
            missionModel: {count:1,get: index => ({eastMeters:30,northMeters:20})},
            plannedStartValid:true,plannedStartEast:0,plannedStartNorth:0,
            vehicleEast:10,vehicleNorth:10,
            missionRunning:false,missionPaused:false,missionMonitorState:"",missionCurrentItem:0,
            segmentHasRisk: index => false,
            depthRouteSafety:{available:false},
            flightPathVisible:false,actualPoints:[],
            visibleActualPathStartIndex: () => 0,
            pathColor: () => "track",pathLineWidth: () => 2
        }
        function paint() { strokes.length=0; arcs.length=0; RouteMapPainter.paint(context,100,100,state,palette) }
        paint()
        let plan=strokes.filter(stroke => stroke.color === "plan" && stroke.dash.length > 0)
        compare(plan.length,1)
        compare(JSON.stringify(plan[0].path),JSON.stringify([["move",50,50],["line",80,30]]))
        compare(JSON.stringify(plan[0].dash),"[6,6]")
        state.missionRunning=true
        paint()
        verify(strokes.some(stroke => stroke.color === "active" && stroke.width === 1.8))
        state.segmentHasRisk=index => true
        paint()
        verify(strokes.some(stroke => stroke.color === "risk" && stroke.width === 2.2))
        state.missionCurrentItem=1
        paint()
        verify(strokes.some(stroke => stroke.color === "risk" && stroke.alpha === 0.35))
        state.depthRouteSafety={available:true,collisionPoints:[{eastMeters:5,northMeters:7}]}
        state.flightPathVisible=true
        state.actualPoints=[{east:0,north:0},{east:1,north:2,breakBefore:true},{east:3,north:4}]
        paint()
        verify(arcs.some(arc => arc[0]===55 && arc[1]===43 && arc[2]===6))
        const track=strokes.filter(stroke => stroke.color === "track")
        compare(track.length,1)
        compare(JSON.stringify(track[0].path),JSON.stringify([["move",50,50],["move",51,48],["line",53,46]]))
        compare(context.globalAlpha,1)
        state.flightPathVisible=false
        paint()
        verify(!strokes.some(stroke => stroke.color === "track"))
        verify(strokes.some(stroke => stroke.color === "risk")) // Plan stays visible.
    }
    function test_authoritativeModelAndLock() {
        const altitude = findChildWhere(editor, item => item.accessibleName === "相对高度")
        verify(altitude !== null)
        points.setProperty(0, "altitudeMeters", 9)
        tryCompare(altitude, "value", 9)
        altitude.commit(11)
        compare(edits.count, 1)
        compare(edits.signalArguments[0][0], "altitudeMeters")
        compare(points.get(0).altitudeMeters, 11)
        editor.selectedIndex = 1
        tryCompare(altitude, "value", 12)
        editor.editingLocked = true
        tryCompare(altitude, "enabled", false)
        editor.selectedIndex = -1
        wait(20)
        compare(altitude.enabled, false)
    }
    function test_transientMissingEntry() {
        const savedPoint = points.get(0)
        const snapshot = JSON.parse(JSON.stringify(savedPoint))
        const fields = [["相对高度",8], ["飞行速度",4], ["航向角",0], ["向东",0], ["向北",0]]
        // Reproduce a valid selection arriving before its model entry.
        editor.point = ({})
        wait(20)
        for (const row of fields) {
            const field = findChildWhere(editor, item => item.accessibleName === row[0])
            compare(field.value, row[1])
        }
        editor.point = null
        wait(20)
        compare(editor.pointNumber("gimbalPitchDegrees", -45), -45)
        editor.point = {altitudeMeters: NaN, speedMetersPerSecond: Infinity, yawDegrees: 0}
        wait(20)
        compare(editor.pointNumber("altitudeMeters",8),8)
        compare(editor.pointNumber("yawDegrees",99),0)
        compare(edits.count, 0)
        compare(JSON.stringify(points.get(0)), JSON.stringify(snapshot))
        editor.point = Qt.binding(() => editor.selectionValid ? points.get(editor.selectedIndex) : ({}))
    }
    function test_resetModel() {
        const snapshots = []
        for (let index = 0; index < points.count; ++index)
            snapshots.push(JSON.parse(JSON.stringify(points.get(index))))
        for (let cycle = 0; cycle < 3; ++cycle) {
            editor.selectedIndex = 1
            points.clear()
            wait(10)
            compare(editor.selectionValid, false)
            for (const point of snapshots) points.append(point)
            wait(10)
            compare(editor.selectionValid, true)
            const altitude = findChildWhere(editor, item => item.accessibleName === "相对高度")
            compare(altitude.value, snapshots[1].altitudeMeters)
        }
        compare(edits.count, 0)
    }
    function test_endActionIsRequestOnly() {
        const land = findChildWhere(editor, item => item.Accessible.name === "任务结束后降落")
        verify(land !== null)
        land.selectEndAction()
        compare(actions.count, 1)
        compare(actions.signalArguments[0][0], "land")
        compare(editor.endAction, "rtl") // Parent owns persistence and dirty state.
        editor.editingLocked = true
        land.selectEndAction()
        compare(actions.count, 1)
    }
    function test_sensorActions() {
        summary.sensor = {available:true, replayable:true, visualSurveyAvailable:true, depthMapAvailable:true}
        wait(20)
        for (const row of [
                 ["打开视觉巡检影像地图", surveySpy],
                 ["打开保存的三维地图", depthSpy],
                 ["仅删除本次传感器数据", deleteSpy]]) {
            const button = findChildWhere(summary, item => item.accessibleLabel === row[0])
            verify(button !== null)
            row[1].clear()
            mouseClick(button)
            compare(row[1].count, 1)
        }
        summary.canOpenHistory = false
        const depth = findChildWhere(summary, item => item.accessibleLabel === "打开保存的三维地图")
        tryCompare(depth, "enabled", false)
        summary.sensor = ({})
        tryCompare(summary, "visible", false)
        summary.sensor = {deleted:true}
        tryCompare(summary, "visible", true)
    }
    function test_layoutVariants() {
        for (const mode of ["light", "dark"]) {
            Theme.mode = mode
            for (const compact of [true, false]) {
                editor.compactEditor = compact
                editor.gimbalMission = !compact
                editor.height = compact ? 350 : 780
                wait(30)
                verify(editor.width === 252)
            }
        }
    }
}
