import "RouteMapPainter.js" as RouteMapPainter
import QtQuick
import "../../controls"
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import FlightBench

Item {
    id: root
    readonly property bool compactEditor: height < 400
    property int selectedIndex: 1
    property bool dirty: false
    property bool wasMissionRunning: false
    property bool missionTrackActive: false
    property var actualPoints: []
    property real actualLastSampleMs: 0
    property bool plannedStartValid: false
    property real plannedStartEast: 0
    property real plannedStartNorth: 0
    property string currentRouteId: ""
    property string currentRouteName: "未命名航线"
    property real currentRouteAnchorEast: 0
    property real currentRouteAnchorNorth: 0
    property bool currentRouteAnchorValid: false
    property string selectedLibraryRouteId: ""
    property string selectedLibraryRouteName: ""
    property string routeNameDialogMode: "save"
    property string routeOperationMessage: ""
    property real pixelsPerMeter: 2.2
    property real viewCenterEast: 0
    property real viewCenterNorth: 0
    property bool viewInitialized: false
    property bool viewManuallyAdjusted: false
    property var missionSafety: ({ available: false, safe: true,
                                   riskyWaypointIndices: [], riskySegmentIndices: [] })
    property var depthRouteSafety: ({ available: false, safe: true,
                                      collisionCount: 0, collisionPoints: [] })
    readonly property var worldMap: App.selectedWorldMap || ({ available: false })
    readonly property bool worldMapAvailable: Boolean(worldMap.available)
    readonly property url worldMapSource: worldMapAvailable
                                                  ? (Theme.darkMode
                                                     ? worldMap.darkSource
                                                     : worldMap.lightSource)
                                                  : ""
    readonly property bool missionRouteSafe: (!missionSafety.available
                                               || Boolean(missionSafety.safe))
                                              && (!depthRouteSafety.available
                                                  || Boolean(depthRouteSafety.safe))
    readonly property bool editingLocked: App.missionBusy || App.missionRunning || App.missionPaused
    readonly property real mapScale: pixelsPerMeter
    readonly property real vehicleEast: App.vehicleScenePosition.x
    readonly property real vehicleNorth: -App.vehicleScenePosition.z
    readonly property bool selectionValid: selectedIndex >= 0
                                                   && selectedIndex < missionModel.count
    readonly property bool gimbalMission: App.selectedSensorType === "gimbal-camera"
    readonly property real routeDistance: calculateDistance()
    readonly property real actualTrackDistance: calculateActualTrackDistance()
    readonly property real averageSpeed: calculateAverageSpeed()
    readonly property int estimatedSeconds: Math.round(routeDistance / Math.max(1, averageSpeed)
                                                       + missionModel.count * 2)
    readonly property bool missionValid: missionModel.count >= 2 && missionModel.count <= 20
    readonly property bool canExecute: missionValid
                                               && App.vehicleConnected
                                               && App.vehicleReady
                                               && App.telemetryAvailable
                                               && App.localPositionAvailable
                                               && App.globalPositionAvailable
                                               && !App.keyboardControlActive
                                               && missionRouteSafe
                                               && !editingLocked

    Settings {
        id: missionSettings
        category: "MissionPlanner"
        property string missionJson: ""
        property string endAction: "rtl"
        property string currentRouteId: ""
        property string currentRouteName: "未命名航线"
        property real currentRouteAnchorEast: 0
        property real currentRouteAnchorNorth: 0
        property bool currentRouteAnchorValid: false
    }

    ListModel { id: missionModel }

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function mapX(eastMeters) {
        return mapArea.width / 2 + (eastMeters - viewCenterEast) * mapScale
    }

    function mapY(northMeters) {
        return mapArea.height / 2 - (northMeters - viewCenterNorth) * mapScale
    }

    function eastAt(pixelX) {
        return viewCenterEast + (pixelX - mapArea.width / 2) / mapScale
    }

    function northAt(pixelY) {
        return viewCenterNorth + (mapArea.height / 2 - pixelY) / mapScale
    }

    function gridStepMeters() {
        var targetMeters = 72 / mapScale
        var magnitude = Math.pow(10, Math.floor(Math.log(targetMeters) / Math.LN10))
        var normalized = targetMeters / magnitude
        if (normalized <= 1)
            return magnitude
        if (normalized <= 2)
            return 2 * magnitude
        if (normalized <= 5)
            return 5 * magnitude
        return 10 * magnitude
    }

    function listContains(values, target) {
        if (!values)
            return false
        for (var index = 0; index < values.length; ++index) {
            if (Number(values[index]) === target)
                return true
        }
        return false
    }

    function segmentHasRisk(index) {
        return listContains(missionSafety.riskySegmentIndices, index)
    }

    function waypointHasRisk(index) {
        return listContains(missionSafety.riskyWaypointIndices, index)
    }

    function scheduleSafetyAnalysis() {
        safetyAnalysisTimer.restart()
    }

    function refreshSafetyAnalysis() {
        var startEast = plannedStartValid ? plannedStartEast : vehicleEast
        var startNorth = plannedStartValid ? plannedStartNorth : vehicleNorth
        var startAltitude = plannedStartValid ? App.vehicleScenePosition.y
                                              : (App.localPositionAvailable
                                                 ? App.vehicleScenePosition.y : 0)
        missionSafety = App.analyzeMissionRouteSafety(missionData(),
                                                      startEast, startNorth,
                                                      startAltitude)
        depthRouteSafety = App.analyzeDepthMapRoute(missionData(), 1.0)
        routeCanvas.requestPaint()
    }

    function zoomAt(pixelX, pixelY, factor) {
        var anchorEast = eastAt(pixelX)
        var anchorNorth = northAt(pixelY)
        pixelsPerMeter = clamp(pixelsPerMeter * factor, 0.25, 16)
        viewCenterEast = anchorEast - (pixelX - mapArea.width / 2) / mapScale
        viewCenterNorth = anchorNorth - (mapArea.height / 2 - pixelY) / mapScale
        viewInitialized = true
        viewManuallyAdjusted = true
    }

    function scheduleMapPaint() {
        // Scale and center normally change together. Coalesce their repaint
        // requests so a wheel burst renders the Canvas at most once per frame.
        if (!mapPaintTimer.running)
            mapPaintTimer.start()
    }

    function centerOnVehicle() {
        if (App.localPositionAvailable) {
            viewCenterEast = vehicleEast
            viewCenterNorth = vehicleNorth
        } else if (missionModel.count > 0) {
            var eastTotal = 0
            var northTotal = 0
            for (var index = 0; index < missionModel.count; ++index) {
                eastTotal += missionModel.get(index).eastMeters
                northTotal += missionModel.get(index).northMeters
            }
            viewCenterEast = eastTotal / missionModel.count
            viewCenterNorth = northTotal / missionModel.count
        } else {
            viewCenterEast = 0
            viewCenterNorth = 0
        }
        pixelsPerMeter = 2.2
        viewInitialized = App.localPositionAvailable
        viewManuallyAdjusted = true
        routeCanvas.requestPaint()
    }

    function fitWorldToView() {
        if (!worldMapAvailable || mapArea.width < 120 || mapArea.height < 120) {
            fitMissionToView()
            return
        }
        var minimumEast = Number(worldMap.minEastMeters)
        var maximumEast = Number(worldMap.maxEastMeters)
        var minimumNorth = Number(worldMap.minNorthMeters)
        var maximumNorth = Number(worldMap.maxNorthMeters)
        var horizontalPadding = 56
        var verticalPadding = 56
        pixelsPerMeter = clamp(Math.min((mapArea.width - horizontalPadding)
                                         / (maximumEast - minimumEast),
                                        (mapArea.height - verticalPadding)
                                         / (maximumNorth - minimumNorth)),
                               0.25, 16)
        viewCenterEast = (minimumEast + maximumEast) / 2
        viewCenterNorth = (minimumNorth + maximumNorth) / 2
        viewInitialized = true
        viewManuallyAdjusted = true
        routeCanvas.requestPaint()
    }

    function fitMissionToView() {
        if (mapArea.width < 120 || mapArea.height < 120 || missionModel.count === 0) {
            centerOnVehicle()
            viewManuallyAdjusted = false
            return
        }

        var startEast = App.localPositionAvailable ? vehicleEast
                                                   : missionModel.get(0).eastMeters
        var startNorth = App.localPositionAvailable ? vehicleNorth
                                                    : missionModel.get(0).northMeters
        var minimumEast = startEast
        var maximumEast = startEast
        var minimumNorth = startNorth
        var maximumNorth = startNorth
        for (var index = 0; index < missionModel.count; ++index) {
            var point = missionModel.get(index)
            minimumEast = Math.min(minimumEast, point.eastMeters)
            maximumEast = Math.max(maximumEast, point.eastMeters)
            minimumNorth = Math.min(minimumNorth, point.northMeters)
            maximumNorth = Math.max(maximumNorth, point.northMeters)
        }

        // The asymmetric vertical padding accounts for the pin-shaped marker
        // extending farther above its coordinate than below it.
        var horizontalPadding = 96
        var topPadding = 62
        var bottomPadding = 30
        var eastSpan = Math.max(10, maximumEast - minimumEast)
        var northSpan = Math.max(10, maximumNorth - minimumNorth)
        var fittedScale = Math.min((mapArea.width - horizontalPadding) / eastSpan,
                                   (mapArea.height - topPadding - bottomPadding) / northSpan)
        pixelsPerMeter = clamp(fittedScale, 0.25, 16)
        viewCenterEast = (minimumEast + maximumEast) / 2
        viewCenterNorth = (minimumNorth + maximumNorth) / 2
                          + (topPadding - bottomPadding) / (2 * pixelsPerMeter)
        viewInitialized = App.localPositionAvailable
        viewManuallyAdjusted = false
        routeCanvas.requestPaint()
    }

    function pathColor() {
        return Theme.flightPathColor(App.flightPathColorPreset)
    }

    function pathLineWidth() {
        return 1 + clamp((App.flightPathWidthMeters - 0.02) / 0.10, 0, 1) * 2
    }

    function visibleActualPathStartIndex() {
        if (App.flightPathMaxVisibleLengthMeters <= 0 || actualPoints.length < 2)
            return 0
        var distance = 0
        for (var index = actualPoints.length - 1; index > 0; --index) {
            distance += Math.hypot(actualPoints[index].east - actualPoints[index - 1].east,
                                   actualPoints[index].north - actualPoints[index - 1].north)
            if (distance >= App.flightPathMaxVisibleLengthMeters)
                return index - 1
        }
        return 0
    }

    function calculateDistance() {
        var distance = 0
        if (missionModel.count === 0)
            return 0
        var startIndex = 0
        var lastEast
        var lastNorth
        if (plannedStartValid || App.localPositionAvailable) {
            lastEast = plannedStartValid ? plannedStartEast : vehicleEast
            lastNorth = plannedStartValid ? plannedStartNorth : vehicleNorth
        } else {
            lastEast = missionModel.get(0).eastMeters
            lastNorth = missionModel.get(0).northMeters
            startIndex = 1
        }
        for (var index = startIndex; index < missionModel.count; ++index) {
            var point = missionModel.get(index)
            distance += Math.hypot(point.eastMeters - lastEast,
                                   point.northMeters - lastNorth)
            lastEast = point.eastMeters
            lastNorth = point.northMeters
        }
        return distance
    }

    function calculateActualTrackDistance() {
        var distance = 0
        for (var index = 1; index < actualPoints.length; ++index) {
            if (actualPoints[index].breakBefore)
                continue
            distance += Math.hypot(actualPoints[index].east - actualPoints[index - 1].east,
                                   actualPoints[index].north - actualPoints[index - 1].north)
        }
        return distance
    }

    function beginMissionVisualization() {
        plannedStartEast = vehicleEast
        plannedStartNorth = vehicleNorth
        plannedStartValid = App.localPositionAvailable
        missionTrackActive = false
        actualLastSampleMs = Date.now()
        actualPoints = plannedStartValid
                       ? [{ east: plannedStartEast, north: plannedStartNorth,
                            breakBefore: false, timestampMs: actualLastSampleMs }]
                       : []
        routeCanvas.requestPaint()
    }

    function calculateAverageSpeed() {
        if (missionModel.count === 0)
            return 0
        var total = 0
        for (var index = 0; index < missionModel.count; ++index)
            total += missionModel.get(index).speedMetersPerSecond
        return total / missionModel.count
    }

    function appendDefaultMission() {
        missionModel.append({ name: "入口", eastMeters: 20, northMeters: 18,
                              altitudeMeters: 6, speedMetersPerSecond: 3, yawDegrees: 45,
                              gimbalYawDegrees: 0, gimbalPitchDegrees: -45,
                              capturePhoto: false })
        missionModel.append({ name: "教学楼", eastMeters: 46, northMeters: 36,
                              altitudeMeters: 8, speedMetersPerSecond: 4, yawDegrees: 45,
                              gimbalYawDegrees: 0, gimbalPitchDegrees: -45,
                              capturePhoto: false })
        missionModel.append({ name: "巡检点", eastMeters: 62, northMeters: 4,
                              altitudeMeters: 8, speedMetersPerSecond: 4, yawDegrees: 135,
                              gimbalYawDegrees: 0, gimbalPitchDegrees: -45,
                              capturePhoto: false })
        missionModel.append({ name: "返程点", eastMeters: 30, northMeters: -24,
                              altitudeMeters: 6, speedMetersPerSecond: 3, yawDegrees: -135,
                              gimbalYawDegrees: 0, gimbalPitchDegrees: -45,
                              capturePhoto: false })
        selectedIndex = 1
        scheduleSafetyAnalysis()
    }

    function restoreMission() {
        missionModel.clear()
        if (missionSettings.missionJson.length > 0) {
            try {
                var saved = JSON.parse(missionSettings.missionJson)
                if (Array.isArray(saved) && saved.length >= 2 && saved.length <= 20) {
                    for (var index = 0; index < saved.length; ++index) {
                        var savedPoint = saved[index]
                        savedPoint.name = String(savedPoint.name || ("航点 " + (index + 1)))
                        savedPoint.gimbalYawDegrees = Number(savedPoint.gimbalYawDegrees || 0)
                        savedPoint.gimbalPitchDegrees = savedPoint.gimbalPitchDegrees === undefined
                                                          ? -45 : Number(savedPoint.gimbalPitchDegrees)
                        savedPoint.capturePhoto = Boolean(savedPoint.capturePhoto)
                        missionModel.append(savedPoint)
                    }
                }
            } catch (error) {
                console.warn("Unable to restore mission draft:", error)
            }
        }
        if (missionModel.count === 0)
            appendDefaultMission()
        selectedIndex = Math.min(Math.max(0, selectedIndex), missionModel.count - 1)
        dirty = false
        scheduleSafetyAnalysis()
        routeCanvas.requestPaint()
    }

    function missionData() {
        var result = []
        for (var index = 0; index < missionModel.count; ++index) {
            var point = missionModel.get(index)
            result.push({ name: String(point.name || ("航点 " + (index + 1))),
                          northMeters: Number(point.northMeters),
                          eastMeters: Number(point.eastMeters),
                          altitudeMeters: Number(point.altitudeMeters),
                          speedMetersPerSecond: Number(point.speedMetersPerSecond),
                          yawDegrees: Number(point.yawDegrees),
                          gimbalYawDegrees: Number(point.gimbalYawDegrees || 0),
                          gimbalPitchDegrees: point.gimbalPitchDegrees === undefined
                                               ? -45 : Number(point.gimbalPitchDegrees),
                          capturePhoto: Boolean(point.capturePhoto) })
        }
        return result
    }

    function saveDraft(notify) {
        missionSettings.missionJson = JSON.stringify(missionData())
        dirty = false
        if (notify)
            savedNotice.restart()
    }

    function rememberCurrentRoute() {
        missionSettings.currentRouteId = currentRouteId
        missionSettings.currentRouteName = currentRouteName
        missionSettings.currentRouteAnchorEast = currentRouteAnchorEast
        missionSettings.currentRouteAnchorNorth = currentRouteAnchorNorth
        missionSettings.currentRouteAnchorValid = currentRouteAnchorValid
    }

    function ensureRouteAnchor() {
        if (currentRouteAnchorValid)
            return
        currentRouteAnchorEast = App.localPositionAvailable ? vehicleEast : 0
        currentRouteAnchorNorth = App.localPositionAvailable ? vehicleNorth : 0
        currentRouteAnchorValid = true
    }

    function saveNamedRoute(name, routeId) {
        ensureRouteAnchor()
        var response = App.saveRoute(routeId, name, missionData(),
                                     missionSettings.endAction,
                                     currentRouteAnchorEast,
                                     currentRouteAnchorNorth)
        routeOperationMessage = response.message || ""
        if (!response.success)
            return false
        currentRouteId = response.id
        currentRouteName = response.name
        dirty = false
        rememberCurrentRoute()
        saveDraft(true)
        return true
    }

    function requestRouteSave() {
        if (editingLocked)
            return
        if (currentRouteId.length > 0) {
            saveNamedRoute(currentRouteName, currentRouteId)
            return
        }
        routeNameDialogMode = "save"
        routeNameField.text = currentRouteName === "未命名航线" ? "" : currentRouteName
        routeNameDialog.open()
        routeNameField.forceActiveFocus()
    }

    function loadLibraryRoute(routeId, endActionOverride) {
        var targetEast = App.localPositionAvailable ? vehicleEast : 0
        var targetNorth = App.localPositionAvailable ? vehicleNorth : 0
        var response = App.loadSavedRoute(routeId, targetEast, targetNorth)
        routeOperationMessage = response.message || ""
        if (!response.success)
            return
        missionModel.clear()
        for (var index = 0; index < response.waypoints.length; ++index) {
            var loadedPoint = response.waypoints[index]
            loadedPoint.name = String(loadedPoint.name || ("航点 " + (index + 1)))
            loadedPoint.gimbalYawDegrees = Number(loadedPoint.gimbalYawDegrees || 0)
            loadedPoint.gimbalPitchDegrees = loadedPoint.gimbalPitchDegrees === undefined
                                                 ? -45 : Number(loadedPoint.gimbalPitchDegrees)
            loadedPoint.capturePhoto = Boolean(loadedPoint.capturePhoto)
            missionModel.append(loadedPoint)
        }
        currentRouteId = response.id
        currentRouteName = response.name
        currentRouteAnchorEast = targetEast
        currentRouteAnchorNorth = targetNorth
        currentRouteAnchorValid = true
        var requestedEndAction = String(endActionOverride || "")
        missionSettings.endAction = requestedEndAction === "land"
                                    || requestedEndAction === "rtl"
                                    || requestedEndAction === "hold"
                                    ? requestedEndAction : response.endAction
        selectedIndex = missionModel.count > 0 ? 0 : -1
        plannedStartValid = false
        actualPoints = []
        dirty = false
        rememberCurrentRoute()
        saveDraft()
        viewManuallyAdjusted = false
        Qt.callLater(root.fitMissionToView)
        routeLibraryDialog.close()
        scheduleSafetyAnalysis()
        routeCanvas.requestPaint()
    }

    function openRouteLibrary() {
        selectedLibraryRouteId = currentRouteId
        selectedLibraryRouteName = currentRouteName
        routeOperationMessage = ""
        routeLibraryDialog.open()
    }

    function createNewRoute() {
        missionModel.clear()
        currentRouteId = ""
        currentRouteName = "未命名航线"
        currentRouteAnchorEast = App.localPositionAvailable ? vehicleEast : 0
        currentRouteAnchorNorth = App.localPositionAvailable ? vehicleNorth : 0
        currentRouteAnchorValid = true
        selectedIndex = -1
        plannedStartValid = false
        actualPoints = []
        dirty = false
        viewManuallyAdjusted = false
        rememberCurrentRoute()
        saveDraft()
        centerOnVehicle()
        viewManuallyAdjusted = false
        routeLibraryDialog.close()
        scheduleSafetyAnalysis()
        routeCanvas.requestPaint()
    }

    function setPointValue(role, value) {
        if (selectedIndex < 0 || selectedIndex >= missionModel.count || editingLocked)
            return
        missionModel.setProperty(selectedIndex, role, value)
        dirty = true
        scheduleSafetyAnalysis()
        routeCanvas.requestPaint()
    }

    function addPoint(pixelX, pixelY) {
        if (editingLocked || missionModel.count >= 20)
            return
        missionModel.append({ name: "航点 " + (missionModel.count + 1),
                              eastMeters: Math.round(eastAt(pixelX) * 10) / 10,
                              northMeters: Math.round(northAt(pixelY) * 10) / 10,
                              altitudeMeters: 8,
                              speedMetersPerSecond: 4,
                              yawDegrees: 0,
                              gimbalYawDegrees: 0,
                              gimbalPitchDegrees: -45,
                              capturePhoto: false })
        selectedIndex = missionModel.count - 1
        dirty = true
        scheduleSafetyAnalysis()
        routeCanvas.requestPaint()
    }

    function removeSelectedPoint() {
        if (editingLocked || selectedIndex < 0 || selectedIndex >= missionModel.count)
            return
        var removedIndex = selectedIndex
        selectedIndex = -1
        missionModel.remove(removedIndex)
        selectedIndex = Math.min(removedIndex, missionModel.count - 1)
        dirty = true
        scheduleSafetyAnalysis()
        routeCanvas.requestPaint()
    }

    function executeMission() {
        if (!canExecute)
            return
        if (dirty)
            saveDraft()
        executeDialog.open()
    }

    function executeBlockReason() {
        if (!missionValid)
            return missionModel.count < 2 ? "至少需要 2 个航点" : "航点数量不能超过 20 个"
        if (!App.vehicleConnected)
            return "等待飞行器连接"
        if (!App.vehicleReady)
            return "等待起飞检查通过"
        if (!App.telemetryAvailable)
            return "等待完整遥测数据"
        if (!App.localPositionAvailable)
            return "等待本地位置"
        if (!App.globalPositionAvailable)
            return "等待 GPS 全局定位"
        if (App.keyboardControlActive)
            return "请先退出键盘飞行控制"
        if (depthRouteSafety.available && !depthRouteSafety.safe)
            return "航线与已建三维障碍过近，请移动红色碰撞位置附近的航点"
        if (!missionRouteSafe)
            return String(missionSafety.message || "航线可能穿过建筑")
        if (editingLocked)
            return "当前任务正在处理"
        return "可以上传并执行"
    }

    function formatDuration(seconds) {
        var minutes = Math.floor(seconds / 60)
        var remaining = seconds % 60
        return minutes + ":" + (remaining < 10 ? "0" : "") + remaining
    }

    onPixelsPerMeterChanged: scheduleMapPaint()
    onViewCenterEastChanged: scheduleMapPaint()
    onViewCenterNorthChanged: scheduleMapPaint()

    Component.onCompleted: {
        restoreMission()
        currentRouteId = missionSettings.currentRouteId
        currentRouteName = missionSettings.currentRouteName.length > 0
                           ? missionSettings.currentRouteName : "未命名航线"
        currentRouteAnchorEast = missionSettings.currentRouteAnchorEast
        currentRouteAnchorNorth = missionSettings.currentRouteAnchorNorth
        currentRouteAnchorValid = missionSettings.currentRouteAnchorValid
        // Open around the planned route so nearby buildings are readable.
        // The explicit scene-fit action remains available for the full campus.
        Qt.callLater(fitMissionToView)
        Qt.callLater(refreshSafetyAnalysis)
    }

    Connections {
        target: App
        function onVehicleScenePositionChanged() {
            if (!root.viewInitialized && App.localPositionAvailable) {
                Qt.callLater(root.fitMissionToView)
            }
            if (!root.editingLocked)
                root.scheduleSafetyAnalysis()
            routeCanvas.requestPaint()
            if (!root.missionTrackActive || !App.inAir)
                return
            var now = Date.now()
            var point = { east: App.vehicleScenePosition.x,
                          north: -App.vehicleScenePosition.z,
                          breakBefore: false,
                          timestampMs: now }
            var points = root.actualPoints.slice()
            var distance = points.length === 0 ? 0
                                               : Math.hypot(point.east - points[points.length - 1].east,
                                                            point.north - points[points.length - 1].north)
            if (points.length === 0 || distance >= 0.25) {
                point.breakBefore = points.length > 0
                                    && (distance > 12 || now - root.actualLastSampleMs > 2500)
                points.push(point)
                if (points.length > 1600)
                    points.shift()
                root.actualPoints = points
                root.actualLastSampleMs = now
                routeCanvas.requestPaint()
            }
        }
        function onMissionStateChanged() {
            if (App.missionRunning && !root.wasMissionRunning) {
                root.missionTrackActive = true
                if (root.actualPoints.length === 0 && App.localPositionAvailable) {
                    root.actualLastSampleMs = Date.now()
                    root.actualPoints = [{ east: root.vehicleEast, north: root.vehicleNorth,
                                           breakBefore: false,
                                           timestampMs: root.actualLastSampleMs }]
                }
                routeCanvas.requestPaint()
            }
            if (!App.missionUploaded && !App.missionBusy && !App.missionRunning
                    && !App.missionPaused) {
                root.missionTrackActive = false
                root.plannedStartValid = false
            }
            root.wasMissionRunning = App.missionRunning
        }
        function onInAirChanged() {
            if (!App.inAir)
                root.missionTrackActive = false
        }
        function onFlightPathDisplaySettingsChanged() {
            routeCanvas.requestPaint()
        }
        function onSelectedWorldChanged() {
            root.scheduleSafetyAnalysis()
            root.viewManuallyAdjusted = false
            Qt.callLater(root.fitMissionToView)
        }
    }

    Connections {
        target: Theme
        function onDarkModeChanged() {
            routeCanvas.requestPaint()
        }
    }

    Timer {
        id: savedNotice
        interval: 1800
    }

    Connections {
        target: App.depthMapping
        function onMapChanged() {
            if (root.visible)
                root.scheduleSafetyAnalysis()
        }
    }

    onVisibleChanged: {
        if (visible)
            scheduleSafetyAnalysis()
    }

    Timer {
        id: safetyAnalysisTimer
        interval: 40
        onTriggered: root.refreshSafetyAnalysis()
    }

    Timer {
        id: mapPaintTimer
        interval: 16
        repeat: false
        onTriggered: routeCanvas.requestPaint()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 430
            radius: Theme.radiusLarge
            color: Theme.darkMode ? "#0B1522" : "#F5F8FB"
            border.width: 1
            border.color: Theme.line
            clip: true

            Item {
                id: mapArea
                anchors.fill: parent
                anchors.bottomMargin: 70
                onWidthChanged: {
                    if (!root.viewManuallyAdjusted)
                        Qt.callLater(root.fitMissionToView)
                }
                onHeightChanged: {
                    if (!root.viewManuallyAdjusted)
                        Qt.callLater(root.fitMissionToView)
                }

                Image {
                    id: worldMapImage
                    visible: root.worldMapAvailable
                    x: root.worldMapAvailable ? root.mapX(Number(root.worldMap.minEastMeters)) : 0
                    y: root.worldMapAvailable ? root.mapY(Number(root.worldMap.maxNorthMeters)) : 0
                    width: root.worldMapAvailable
                           ? (Number(root.worldMap.maxEastMeters)
                              - Number(root.worldMap.minEastMeters)) * root.mapScale : 0
                    height: root.worldMapAvailable
                            ? (Number(root.worldMap.maxNorthMeters)
                               - Number(root.worldMap.minNorthMeters)) * root.mapScale : 0
                    source: root.worldMapSource
                    fillMode: Image.Stretch
                    smooth: true
                    mipmap: true
                    // The source changes with the theme. Loading this packaged
                    // SVG synchronously avoids a cancelled async request leaving
                    // the map blank during startup/theme switching.
                    asynchronous: false
                    cache: true
                    // Rasterize the SVG once. Binding sourceSize to visual size
                    // forced a complete decode for every wheel step.
                    sourceSize.width: 2048
                    sourceSize.height: 0
                }

                Canvas {
                    id: routeCanvas
                    anchors.fill: parent
                    z: 1
                    antialiasing: true
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: RouteMapPainter.paint(getContext("2d"), width, height, {
                        gridStepMeters: root.gridStepMeters,
                        eastAt: root.eastAt, northAt: root.northAt,
                        mapX: root.mapX, mapY: root.mapY,
                        worldMapAvailable: root.worldMapAvailable,
                        missionModel: missionModel,
                        plannedStartValid: root.plannedStartValid,
                        plannedStartEast: root.plannedStartEast,
                        plannedStartNorth: root.plannedStartNorth,
                        vehicleEast: root.vehicleEast, vehicleNorth: root.vehicleNorth,
                        missionRunning: App.missionRunning, missionPaused: App.missionPaused,
                        missionMonitorState: App.missionMonitorState,
                        missionCurrentItem: App.missionCurrentItem,
                        segmentHasRisk: root.segmentHasRisk,
                        depthRouteSafety: root.depthRouteSafety,
                        flightPathVisible: App.flightPathVisible,
                        actualPoints: root.actualPoints,
                        visibleActualPathStartIndex: root.visibleActualPathStartIndex,
                        pathColor: root.pathColor, pathLineWidth: root.pathLineWidth
                    }, {
                        line: Theme.line, danger: Theme.danger, accent: Theme.accent,
                        textSecondary: Theme.textSecondary, surface: Theme.surface
                    })
                }

                MissionMapInteraction {
                    anchors.fill: parent
                    editingLocked: root.editingLocked
                    onFocusRequested: mapArea.forceActiveFocus(Qt.MouseFocusReason)
                    onPanRequested: (deltaX, deltaY) => {
                        root.viewCenterEast -= deltaX / root.mapScale
                        root.viewCenterNorth += deltaY / root.mapScale
                        root.viewInitialized = true
                        root.viewManuallyAdjusted = true
                    }
                    onPointRequested: (pixelX, pixelY) => root.addPoint(pixelX, pixelY)
                    onZoomRequested: (pixelX, pixelY, factor) => root.zoomAt(pixelX, pixelY, factor)
                }

                Repeater {
                    model: missionModel
                    delegate: WaypointMapMarker {
                        id: marker
                        dragSurface: mapArea
                        buildingRisk: root.waypointHasRisk(index)
                        selected: root.selectedIndex === index
                        editingLocked: root.editingLocked
                        x: root.mapX(eastMeters) - width / 2
                        y: root.mapY(northMeters) - tipY
                        onSelectedRequested: root.selectedIndex = index
                        onDragged: (startEast, startNorth, deltaX, deltaY) => {
                            var nextEast = startEast + deltaX / root.mapScale
                            var nextNorth = startNorth - deltaY / root.mapScale
                            missionModel.setProperty(index, "eastMeters", Math.round(nextEast * 10) / 10)
                            missionModel.setProperty(index, "northMeters", Math.round(nextNorth * 10) / 10)
                            root.dirty = true
                            root.scheduleSafetyAnalysis()
                            routeCanvas.requestPaint()
                        }
                        onDragFinished: {
                            root.scheduleSafetyAnalysis()
                            routeCanvas.requestPaint()
                        }
                    }
                }

                Item {
                    visible: App.localPositionAvailable
                    x: root.mapX(App.vehicleScenePosition.x) - 12
                    y: root.mapY(-App.vehicleScenePosition.z) - 12
                    width: 24
                    height: 24
                    z: 8
                    Rectangle {
                        anchors.centerIn: parent
                        width: 20; height: 20; radius: 10
                        color: Theme.darkMode ? "#18283B" : "#FFFFFF"
                        border.width: 2; border.color: Theme.warning
                    }
                    ThemedIcon {
                        anchors.centerIn: parent
                        width: 12; height: 12
                        source: "qrc:/icons/plane.svg"
                        iconColor: Theme.warning
                        rotation: App.headingDegrees
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    width: root.worldMapAvailable ? 133 : 100
                    height: 34
                    radius: Theme.radiusMedium
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.line
                    z: 20

                    Row {
                        anchors.centerIn: parent
                        spacing: 0
                        CompactIconButton {
                            width: 32
                            height: 32
                            iconSource: "qrc:/icons/zoom-out.svg"
                            accessibleLabel: "缩小地图"
                            toolTipText: "缩小地图"
                            onClicked: root.zoomAt(mapArea.width / 2, mapArea.height / 2, 1 / 1.35)
                        }
                        Rectangle { width: 1; height: 16; anchors.verticalCenter: parent.verticalCenter; color: Theme.line }
                        CompactIconButton {
                            width: 32
                            height: 32
                            iconSource: "qrc:/icons/zoom-in.svg"
                            accessibleLabel: "放大地图"
                            toolTipText: "放大地图"
                            onClicked: root.zoomAt(mapArea.width / 2, mapArea.height / 2, 1.35)
                        }
                        Rectangle {
                            visible: root.worldMapAvailable
                            width: visible ? 1 : 0
                            height: 16
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.line
                        }
                        CompactIconButton {
                            visible: root.worldMapAvailable
                            width: visible ? 32 : 0
                            height: 32
                            iconSource: "qrc:/icons/maximize-2.svg"
                            accessibleLabel: "显示完整场景"
                            toolTipText: "显示完整场景"
                            onClicked: root.fitWorldToView()
                        }
                        Rectangle { width: 1; height: 16; anchors.verticalCenter: parent.verticalCenter; color: Theme.line }
                        CompactIconButton {
                            width: 32
                            height: 32
                            iconSource: "qrc:/icons/locate-fixed.svg"
                            accessibleLabel: "回到无人机"
                            toolTipText: "回到无人机"
                            onClicked: root.centerOnVehicle()
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 12
                    anchors.topMargin: 48
                    width: legendRow.implicitWidth + 18
                    height: 28
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.line
                    visible: missionModel.count > 0
                    z: 20

                    Row {
                        id: legendRow
                        anchors.centerIn: parent
                        spacing: 7
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Repeater {
                                model: 3
                                Rectangle {
                                    width: 7; height: 1
                                    color: Theme.textSecondary
                                }
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "计划"
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                        }
                        Rectangle {
                            visible: root.worldMapAvailable
                            anchors.verticalCenter: parent.verticalCenter
                            width: 1
                            height: 14
                            color: Theme.line
                        }
                        ThemedIcon {
                            visible: root.worldMapAvailable
                            anchors.verticalCenter: parent.verticalCenter
                            width: 13
                            height: 13
                            source: root.missionRouteSafe ? "qrc:/icons/map-pin.svg"
                                                          : "qrc:/icons/heart-pulse.svg"
                            iconColor: root.missionRouteSafe ? Theme.textMuted : Theme.danger
                        }
                        Text {
                            visible: root.worldMapAvailable
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.missionRouteSafe
                                  ? "校园底图"
                                  : "建筑风险 · 建议 ≥ "
                                    + Number(root.missionSafety.requiredAltitudeMeters || 0).toFixed(1)
                                    + " m"
                            color: root.missionRouteSafe ? Theme.textMuted : Theme.danger
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            font.weight: root.missionRouteSafe ? Font.Normal : Font.Medium
                        }
                        Text {
                            visible: root.depthRouteSafety.available
                                     && !root.depthRouteSafety.safe
                            anchors.verticalCenter: parent.verticalCenter
                            text: "DepthFusion 碰撞 "
                                  + Number(root.depthRouteSafety.collisionCount || 0)
                                  + " 段"
                            color: Theme.danger
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                            font.weight: Font.Medium
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22; height: 2; radius: 1
                            color: root.pathColor()
                            opacity: App.flightPathVisible ? 1 : 0.3
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: !App.flightPathVisible ? "实际已隐藏"
                                  : root.actualPoints.length > 1
                                  ? "实际 " + root.actualTrackDistance.toFixed(0) + " m"
                                  : "实际"
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                        }
                    }
                }

                Row {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 12
                    spacing: 6
                    z: 20
                    Rectangle {
                        width: Math.min(174, routeNameRow.implicitWidth + 18)
                        height: 30
                        radius: Theme.radiusSmall
                        color: routeNameMouse.containsMouse ? Theme.surfaceRaised : Theme.surface
                        border.width: 1
                        border.color: activeFocus ? Theme.accent : Theme.line
                        activeFocusOnTab: true
                        Accessible.name: "打开航线库，当前航线 " + root.currentRouteName
                        Accessible.role: Accessible.Button
                        Keys.onReturnPressed: root.openRouteLibrary()
                        Keys.onEnterPressed: root.openRouteLibrary()
                        Keys.onSpacePressed: root.openRouteLibrary()
                        Row {
                            id: routeNameRow
                            anchors.centerIn: parent
                            spacing: 6
                            ThemedIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14; height: 14
                                source: "qrc:/icons/route.svg"
                                iconColor: Theme.textSecondary
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(118, implicitWidth)
                                text: root.currentRouteName
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.textTiny
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            ThemedIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14
                                height: 14
                                source: "qrc:/icons/chevron-down.svg"
                                iconColor: Theme.textMuted
                            }
                        }
                        MouseArea {
                            id: routeNameMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.openRouteLibrary()
                        }
                    }
                    Rectangle {
                        width: pointCountText.implicitWidth + 18; height: 30; radius: Theme.radiusSmall
                        color: Theme.surface; border.width: 1; border.color: Theme.line
                        Text { id: pointCountText; anchors.centerIn: parent; text: missionModel.count + " 个航点"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                    }
                    Rectangle {
                        width: distanceText.implicitWidth + 18; height: 30; radius: Theme.radiusSmall
                        color: Theme.surface; border.width: 1; border.color: Theme.line
                        Text { id: distanceText; anchors.centerIn: parent; text: root.routeDistance.toFixed(0) + " m"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                    }
                    Rectangle {
                        width: durationText.implicitWidth + 18; height: 30; radius: Theme.radiusSmall
                        color: Theme.surface; border.width: 1; border.color: Theme.line
                        Text { id: durationText; anchors.centerIn: parent; text: "约 " + root.formatDuration(root.estimatedSeconds); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    text: root.editingLocked
                          ? "任务执行期间已锁定航点 · 地图仍可缩放和拖动"
                          : "单击添加航点 · 拖动航点 · 拖动地图 · 滚轮缩放"
                    color: Theme.textFaint
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                }

                Item {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 14
                    anchors.bottomMargin: 11
                    width: Math.max(48, root.gridStepMeters() * root.mapScale)
                    height: 22

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        text: root.gridStepMeters() >= 1000
                              ? (root.gridStepMeters() / 1000).toFixed(0) + " km"
                              : root.gridStepMeters().toFixed(root.gridStepMeters() < 1 ? 1 : 0) + " m"
                        color: Theme.textMuted
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.textTiny
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.textMuted
                        Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; width: 1; height: 5; color: Theme.textMuted }
                        Rectangle { anchors.right: parent.right; anchors.bottom: parent.bottom; width: 1; height: 5; color: Theme.textMuted }
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 70
                color: Theme.surface
                border.width: 1
                border.color: Theme.line

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 12
                    spacing: 8
                    Rectangle {
                        Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4
                        color: App.missionMonitorState === "warning" ? Theme.warning
                               : App.missionMonitorState === "stopped" ? Theme.danger
                               : App.missionRunning ? Theme.success
                               : App.missionPaused ? Theme.warning
                                                   : Theme.textFaint
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: App.missionRunning || App.missionPaused
                                  || App.missionMonitorState === "stopped"
                                  ? App.missionMonitorMessage
                                  : App.missionBusy || App.missionUploaded
                                  ? App.missionStatusMessage
                                  : root.canExecute ? "航线已就绪，可以上传并执行"
                                                    : root.executeBlockReason()
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: App.missionRunning || App.missionPaused
                                  || App.missionMonitorState === "stopped"
                                  ? "航点 " + Math.min(App.missionCurrentItem + 1, App.missionTotalItems)
                                    + "/" + App.missionTotalItems
                                    + "  ·  下个 " + App.missionDistanceToNextMeters.toFixed(1) + " m"
                                    + "  ·  偏差 " + App.missionCrossTrackErrorMeters.toFixed(1) + " m"
                                    + "  ·  剩余 " + App.missionRemainingDistanceMeters.toFixed(0) + " m"
                                    + "  ·  约 " + root.formatDuration(App.missionEstimatedSeconds)
                                  : root.canExecute
                                  ? "计划 " + root.routeDistance.toFixed(0) + " m"
                                    + "  ·  约 " + root.formatDuration(root.estimatedSeconds)
                                    + "  ·  执行后实时监控偏差与剩余航程"
                                  : savedNotice.running ? "草稿已保存到本机"
                                  : root.dirty ? "有未保存的修改" : "本机草稿"
                            color: root.dirty ? Theme.warning : Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textTiny
                        }
                    }
                    ActionButton {
                        Layout.preferredWidth: 74
                        compact: true
                        text: "保存"
                        accessibleLabel: "保存航线"
                        toolTipText: "保存航线"
                        iconSource: "qrc:/icons/save.svg"
                        kind: "secondary"
                        enabled: (!root.currentRouteId.length || root.dirty) && !root.editingLocked
                        onClicked: root.requestRouteSave()
                    }
                    ActionButton {
                        visible: App.missionRunning || App.missionPaused
                        Layout.preferredWidth: 84
                        compact: true
                        text: App.missionPaused ? "继续" : "暂停"
                        accessibleLabel: App.missionPaused ? "继续任务" : "暂停任务"
                        toolTipText: App.missionPaused ? "继续任务" : "暂停任务"
                        iconSource: App.missionPaused ? "qrc:/icons/play.svg" : "qrc:/icons/pause.svg"
                        kind: "secondary"
                        enabled: !App.missionBusy
                        onClicked: App.missionPaused ? App.resumeMission() : App.pauseMission()
                    }
                    ActionButton {
                        visible: App.missionRunning || App.missionPaused
                        Layout.preferredWidth: 104
                        compact: true
                        text: "停止任务"
                        accessibleLabel: "停止航点任务并原地悬停"
                        toolTipText: "停止航点任务，飞行器将在当前位置悬停"
                        iconSource: "qrc:/icons/route-off.svg"
                        kind: "secondary"
                        enabled: !App.missionBusy
                        onClicked: App.abortMissionToHold()
                    }
                    ActionButton {
                        visible: App.missionMonitorState === "stopped" && App.inAir
                        Layout.preferredWidth: 78
                        compact: true
                        text: "返航"
                        accessibleLabel: "返航"
                        toolTipText: "返航"
                        iconSource: "qrc:/icons/rotate-ccw.svg"
                        kind: "secondary"
                        enabled: !App.missionBusy
                        onClicked: App.returnToLaunch()
                    }
                    ActionButton {
                        visible: App.missionMonitorState === "stopped" && App.inAir
                        Layout.preferredWidth: 78
                        compact: true
                        text: "降落"
                        accessibleLabel: "降落"
                        toolTipText: "降落"
                        iconSource: "qrc:/icons/plane-landing.svg"
                        kind: "secondary"
                        enabled: !App.missionBusy
                        onClicked: App.land()
                    }
                    ActionButton {
                        id: executeMissionButton
                        visible: !App.missionRunning && !App.missionPaused
                                 && !(App.missionMonitorState === "stopped" && App.inAir)
                        Layout.preferredWidth: 122
                        compact: true
                        text: App.missionBusy ? "处理中" : "上传并执行"
                        accessibleLabel: App.missionBusy ? "正在处理航线" : "上传并执行航线"
                        iconSource: "qrc:/icons/upload.svg"
                        enabled: root.canExecute
                        toolTipText: root.executeBlockReason()
                        onClicked: root.executeMission()
                    }
                }
            }
        }

        WaypointEditorPanel {
            compactEditor: root.compactEditor
            selectionValid: root.selectionValid
            editingLocked: root.editingLocked
            gimbalMission: root.gimbalMission
            selectedIndex: root.selectedIndex
            pointCount: missionModel.count
            point: root.selectionValid ? missionModel.get(root.selectedIndex) : ({})
            endAction: missionSettings.endAction
            onPointValueEdited: (role, value) => root.setPointValue(role, value)
            onEndActionSelected: action => {
                if (root.editingLocked || missionSettings.endAction === action) return
                missionSettings.endAction = action
                root.dirty = true
            }
            onRemoveRequested: root.removeSelectedPoint()
            onClearRequested: clearDialog.open()
            onFocusRequested: root.forceActiveFocus(Qt.MouseFocusReason)
        }
    }

    RouteLibraryDialog {
        id: routeLibraryDialog
        host: root
        routes: App.savedRoutes
        selectedRouteId: root.selectedLibraryRouteId
        operationMessage: root.routeOperationMessage
        editingLocked: root.editingLocked
        durationFormatter: root.formatDuration
        onRouteSelected: (routeId, routeName) => {
            root.selectedLibraryRouteId = routeId
            root.selectedLibraryRouteName = routeName
            root.routeOperationMessage = ""
        }
        onLoadRequested: routeId => root.loadLibraryRoute(routeId)
        onFolderRequested: App.openRoutesFolder()
        onNewRequested: missionModel.count > 0 ? routeNewDialog.open() : root.createNewRoute()
        onDeleteRequested: routeDeleteDialog.open()
        onDuplicateRequested: {
            var response = App.duplicateSavedRoute(root.selectedLibraryRouteId)
            root.routeOperationMessage = response.message || ""
            if (response.success) {
                root.selectedLibraryRouteId = response.id
                root.selectedLibraryRouteName = response.name
            }
        }
        onRenameRequested: {
            root.routeNameDialogMode = "rename"
            routeNameField.text = root.selectedLibraryRouteName
            routeNameDialog.open()
            routeNameField.forceActiveFocus()
            routeNameField.selectAll()
        }
    }

    MotionDialog {
        id: routeNewDialog
        anchors.centerIn: parent
        width: Math.min(390, root.width - 40)
        modal: true
        padding: 20
        background: DialogSurface {
            radius: Theme.radiusLarge
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.lineStrong
        }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: "新建航线？"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTitle
                font.weight: Font.Medium
            }
            Text {
                Layout.fillWidth: true
                text: root.dirty
                      ? "当前航线有未保存的修改。新建后这些修改不会写入航线库。"
                      : "将清空地图上的当前航点，创建一条空白航线。"
                color: root.dirty ? Theme.warning : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textBody
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    Layout.preferredWidth: 82
                    text: "取消"
                    kind: "secondary"
                    onClicked: routeNewDialog.close()
                }
                ActionButton {
                    Layout.preferredWidth: 96
                    text: "确认新建"
                    onClicked: {
                        routeNewDialog.close()
                        root.createNewRoute()
                    }
                }
            }
        }
    }

    MotionDialog {
        id: routeNameDialog
        anchors.centerIn: parent
        width: Math.min(390, root.width - 40)
        modal: true
        padding: 20
        background: DialogSurface {
            radius: Theme.radiusLarge
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.lineStrong
        }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: root.routeNameDialogMode === "rename" ? "重命名航线" : "保存航线"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTitle
                font.weight: Font.Medium
            }
            TextField {
                id: routeNameField
                Layout.fillWidth: true
                maximumLength: 40
                placeholderText: "例如：矩形巡航"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textBody
                selectByMouse: true
                background: Rectangle {
                    implicitHeight: 38
                    radius: Theme.radiusMedium
                    color: Theme.surface
                    border.width: 1
                    border.color: routeNameField.activeFocus ? Theme.accent : Theme.lineStrong
                }
                onAccepted: routeNameConfirm.clicked()
            }
            Text {
                Layout.fillWidth: true
                text: root.routeNameDialogMode === "save"
                      ? "航点将按相对起点保存，可在下一次仿真中复用。" : "只修改名称，不改变航点。"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    Layout.preferredWidth: 82
                    text: "取消"
                    kind: "secondary"
                    onClicked: routeNameDialog.close()
                }
                ActionButton {
                    id: routeNameConfirm
                    Layout.preferredWidth: 92
                    text: root.routeNameDialogMode === "rename" ? "确认" : "保存"
                    enabled: routeNameField.text.trim().length > 0
                    onClicked: {
                        if (root.routeNameDialogMode === "rename") {
                            var response = App.renameSavedRoute(root.selectedLibraryRouteId,
                                                                routeNameField.text)
                            root.routeOperationMessage = response.message || ""
                            if (response.success) {
                                root.selectedLibraryRouteName = response.name
                                if (root.currentRouteId === response.id) {
                                    root.currentRouteName = response.name
                                    root.rememberCurrentRoute()
                                }
                                routeNameDialog.close()
                            }
                        } else if (root.saveNamedRoute(routeNameField.text, "")) {
                            root.selectedLibraryRouteId = root.currentRouteId
                            root.selectedLibraryRouteName = root.currentRouteName
                            routeNameDialog.close()
                        }
                    }
                }
            }
        }
    }

    MotionDialog {
        id: routeDeleteDialog
        anchors.centerIn: parent
        width: Math.min(390, root.width - 40)
        modal: true
        padding: 20
        background: DialogSurface {
            radius: Theme.radiusLarge
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.lineStrong
        }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: "删除航线？"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTitle
                font.weight: Font.Medium
            }
            Text {
                Layout.fillWidth: true
                text: "将从本机航线库删除“" + root.selectedLibraryRouteName
                      + "”。已经生成的飞行记录不会受影响。"
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textBody
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    Layout.preferredWidth: 82
                    text: "取消"
                    kind: "secondary"
                    onClicked: routeDeleteDialog.close()
                }
                ActionButton {
                    Layout.preferredWidth: 92
                    text: "删除"
                    kind: "danger"
                    onClicked: {
                        var deletedId = root.selectedLibraryRouteId
                        var response = App.deleteSavedRoute(deletedId)
                        root.routeOperationMessage = response.message || ""
                        if (response.success) {
                            if (root.currentRouteId === deletedId) {
                                root.currentRouteId = ""
                                root.currentRouteName = "未命名航线"
                                root.dirty = true
                                root.rememberCurrentRoute()
                            }
                            root.selectedLibraryRouteId = ""
                            root.selectedLibraryRouteName = ""
                            routeDeleteDialog.close()
                        }
                    }
                }
            }
        }
    }

    MotionDialog {
        id: executeDialog
        anchors.centerIn: parent
        width: Math.min(430, root.width - 40)
        modal: true
        padding: 20
        background: DialogSurface { radius: Theme.radiusLarge; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.lineStrong }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "确认执行航点任务"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            Text {
                Layout.fillWidth: true
                text: "将上传 " + missionModel.count + " 个航点，预计航程 "
                      + root.routeDistance.toFixed(0) + " 米。飞行器会自动解锁并开始任务。"
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textBody
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(42, executeWarningText.implicitHeight + 20)
                radius: Theme.radiusMedium
                color: Theme.warningSoft
                Text { id: executeWarningText; anchors.fill: parent; anchors.margins: 10; text: "执行前请确认仿真画面无遮挡，并保持急停方式可用。"; color: Theme.warning; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall; wrapMode: Text.WordWrap }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { Layout.preferredWidth: 86; text: "取消"; kind: "secondary"; onClicked: executeDialog.close() }
                ActionButton {
                    Layout.preferredWidth: 116
                    text: "确认执行"
                    onClicked: {
                        executeDialog.close()
                        root.beginMissionVisualization()
                        App.uploadAndStartMission(root.missionData(), missionSettings.endAction,
                                                  root.currentRouteId,
                                                  root.currentRouteName)
                    }
                }
            }
        }
    }

    MotionDialog {
        id: clearDialog
        anchors.centerIn: parent
        width: Math.min(390, root.width - 40)
        modal: true
        padding: 20
        background: DialogSurface { radius: Theme.radiusLarge; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.lineStrong }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "清除飞行器任务？"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTitle; font.weight: Font.Medium }
            Text { Layout.fillWidth: true; text: "只会清除已经上传到 PX4 的任务，本机保存的航线草稿会保留。"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textBody; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { Layout.preferredWidth: 86; text: "取消"; kind: "secondary"; onClicked: clearDialog.close() }
                ActionButton { Layout.preferredWidth: 112; text: "确认清除"; kind: "danger"; onClicked: { clearDialog.close(); App.clearMission() } }
            }
        }
    }
}
