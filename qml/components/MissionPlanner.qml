import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
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
    readonly property bool editingLocked: App.missionBusy || App.missionRunning || App.missionPaused
    readonly property real mapScale: pixelsPerMeter
    readonly property real vehicleEast: App.vehicleScenePosition.x
    readonly property real vehicleNorth: -App.vehicleScenePosition.z
    readonly property bool selectionValid: selectedIndex >= 0
                                                   && selectedIndex < missionModel.count
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

    function zoomAt(pixelX, pixelY, factor) {
        var anchorEast = eastAt(pixelX)
        var anchorNorth = northAt(pixelY)
        pixelsPerMeter = clamp(pixelsPerMeter * factor, 0.25, 16)
        viewCenterEast = anchorEast - (pixelX - mapArea.width / 2) / mapScale
        viewCenterNorth = anchorNorth - (mapArea.height / 2 - pixelY) / mapScale
        viewInitialized = true
        viewManuallyAdjusted = true
        routeCanvas.requestPaint()
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
                              altitudeMeters: 6, speedMetersPerSecond: 3, yawDegrees: 45 })
        missionModel.append({ name: "教学楼", eastMeters: 46, northMeters: 36,
                              altitudeMeters: 8, speedMetersPerSecond: 4, yawDegrees: 45 })
        missionModel.append({ name: "巡检点", eastMeters: 62, northMeters: 4,
                              altitudeMeters: 8, speedMetersPerSecond: 4, yawDegrees: 135 })
        missionModel.append({ name: "返程点", eastMeters: 30, northMeters: -24,
                              altitudeMeters: 6, speedMetersPerSecond: 3, yawDegrees: -135 })
        selectedIndex = 1
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
                          yawDegrees: Number(point.yawDegrees) })
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

    function loadLibraryRoute(routeId) {
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
            missionModel.append(loadedPoint)
        }
        currentRouteId = response.id
        currentRouteName = response.name
        currentRouteAnchorEast = targetEast
        currentRouteAnchorNorth = targetNorth
        currentRouteAnchorValid = true
        missionSettings.endAction = response.endAction
        selectedIndex = missionModel.count > 0 ? 0 : -1
        plannedStartValid = false
        actualPoints = []
        dirty = false
        rememberCurrentRoute()
        saveDraft()
        viewManuallyAdjusted = false
        Qt.callLater(root.fitMissionToView)
        routeLibraryDialog.close()
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
        routeCanvas.requestPaint()
    }

    function setPointValue(role, value) {
        if (selectedIndex < 0 || selectedIndex >= missionModel.count || editingLocked)
            return
        missionModel.setProperty(selectedIndex, role, value)
        dirty = true
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
                              yawDegrees: 0 })
        selectedIndex = missionModel.count - 1
        dirty = true
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
            return "等待健康检查通过"
        if (!App.telemetryAvailable)
            return "等待完整遥测数据"
        if (!App.localPositionAvailable)
            return "等待本地位置"
        if (!App.globalPositionAvailable)
            return "等待 GPS 全局定位"
        if (App.keyboardControlActive)
            return "请先退出键盘飞行控制"
        if (editingLocked)
            return "当前任务正在处理"
        return "可以上传并执行"
    }

    function formatDuration(seconds) {
        var minutes = Math.floor(seconds / 60)
        var remaining = seconds % 60
        return minutes + ":" + (remaining < 10 ? "0" : "") + remaining
    }

    onPixelsPerMeterChanged: routeCanvas.requestPaint()
    onViewCenterEastChanged: routeCanvas.requestPaint()
    onViewCenterNorthChanged: routeCanvas.requestPaint()

    Component.onCompleted: {
        restoreMission()
        currentRouteId = missionSettings.currentRouteId
        currentRouteName = missionSettings.currentRouteName.length > 0
                           ? missionSettings.currentRouteName : "未命名航线"
        currentRouteAnchorEast = missionSettings.currentRouteAnchorEast
        currentRouteAnchorNorth = missionSettings.currentRouteAnchorNorth
        currentRouteAnchorValid = missionSettings.currentRouteAnchorValid
        Qt.callLater(fitMissionToView)
    }

    Connections {
        target: App
        function onVehicleScenePositionChanged() {
            if (!root.viewInitialized && App.localPositionAvailable) {
                Qt.callLater(root.fitMissionToView)
            }
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

                Canvas {
                    id: routeCanvas
                    anchors.fill: parent
                    antialiasing: true
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        var context = getContext("2d")
                        context.reset()
                        context.fillStyle = Theme.darkMode ? "#0B1522" : "#F5F8FB"
                        context.fillRect(0, 0, width, height)

                        var gridMeters = root.gridStepMeters()
                        var eastMinimum = root.eastAt(0)
                        var eastMaximum = root.eastAt(width)
                        var northMinimum = root.northAt(height)
                        var northMaximum = root.northAt(0)
                        context.lineWidth = 1
                        context.strokeStyle = Theme.line
                        var firstEast = Math.floor(eastMinimum / gridMeters) * gridMeters
                        for (var east = firstEast; east <= eastMaximum; east += gridMeters) {
                            var x = root.mapX(east)
                            context.beginPath(); context.moveTo(x, 0); context.lineTo(x, height); context.stroke()
                        }
                        var firstNorth = Math.floor(northMinimum / gridMeters) * gridMeters
                        for (var north = firstNorth; north <= northMaximum; north += gridMeters) {
                            var y = root.mapY(north)
                            context.beginPath(); context.moveTo(0, y); context.lineTo(width, y); context.stroke()
                        }

                        // The mission plan is always visible. It is not the recorded flight
                        // path, so the global "show flight path" option must not hide it.
                        if (missionModel.count > 0) {
                            var segmentStartEast = root.plannedStartValid
                                                   ? root.plannedStartEast : root.vehicleEast
                            var segmentStartNorth = root.plannedStartValid
                                                    ? root.plannedStartNorth : root.vehicleNorth
                            for (var index = 0; index < missionModel.count; ++index) {
                                var point = missionModel.get(index)
                                var isCompleted = (App.missionRunning || App.missionPaused
                                                   || App.missionMonitorState === "stopped"
                                                   || App.missionMonitorState === "completed")
                                                  && index < App.missionCurrentItem
                                var isCurrent = (App.missionRunning || App.missionPaused)
                                                && index === App.missionCurrentItem
                                context.strokeStyle = isCurrent ? Theme.accent : Theme.textSecondary
                                context.globalAlpha = isCompleted ? 0.35 : isCurrent ? 0.95 : 0.62
                                context.lineWidth = isCurrent ? 1.8 : 1.25
                                context.setLineDash(isCurrent ? [9, 5] : [6, 6])
                                context.beginPath()
                                context.moveTo(root.mapX(segmentStartEast),
                                               root.mapY(segmentStartNorth))
                                context.lineTo(root.mapX(point.eastMeters),
                                               root.mapY(point.northMeters))
                                context.stroke()

                                if (isCurrent) {
                                    var startX = root.mapX(segmentStartEast)
                                    var startY = root.mapY(segmentStartNorth)
                                    var endX = root.mapX(point.eastMeters)
                                    var endY = root.mapY(point.northMeters)
                                    var deltaX = endX - startX
                                    var deltaY = endY - startY
                                    var length = Math.hypot(deltaX, deltaY)
                                    if (length > 24) {
                                        var middleX = (startX + endX) / 2
                                        var middleY = (startY + endY) / 2
                                        var unitX = deltaX / length
                                        var unitY = deltaY / length
                                        context.setLineDash([])
                                        context.globalAlpha = 1
                                        context.lineWidth = 1.6
                                        context.beginPath()
                                        context.moveTo(middleX + unitX * 5, middleY + unitY * 5)
                                        context.lineTo(middleX - unitX * 5 - unitY * 4,
                                                       middleY - unitY * 5 + unitX * 4)
                                        context.moveTo(middleX + unitX * 5, middleY + unitY * 5)
                                        context.lineTo(middleX - unitX * 5 + unitY * 4,
                                                       middleY - unitY * 5 - unitX * 4)
                                        context.stroke()
                                    }
                                }
                                segmentStartEast = point.eastMeters
                                segmentStartNorth = point.northMeters
                            }
                            context.setLineDash([])
                            context.globalAlpha = 1

                            context.fillStyle = Theme.surface
                            context.strokeStyle = Theme.textSecondary
                            context.lineWidth = 1.2
                            context.beginPath()
                            context.arc(root.mapX(root.plannedStartValid
                                                  ? root.plannedStartEast : root.vehicleEast),
                                        root.mapY(root.plannedStartValid
                                                  ? root.plannedStartNorth : root.vehicleNorth),
                                        3.5, 0, Math.PI * 2)
                            context.fill()
                            context.stroke()
                        }

                        if (App.flightPathVisible && root.actualPoints.length > 1) {
                            var pathStartIndex = root.visibleActualPathStartIndex()
                            context.lineJoin = "round"
                            context.lineCap = "round"

                            // A narrow surface-coloured underlay keeps the actual path clear
                            // when it crosses the dashed mission plan or map grid.
                            for (var pass = 0; pass < 2; ++pass) {
                                context.strokeStyle = pass === 0 ? Theme.surface : root.pathColor()
                                context.globalAlpha = pass === 0 ? 0.88 : 1
                                context.lineWidth = root.pathLineWidth() + (pass === 0 ? 2.2 : 0)
                                context.beginPath()
                                context.moveTo(root.mapX(root.actualPoints[pathStartIndex].east),
                                               root.mapY(root.actualPoints[pathStartIndex].north))
                                for (var pathIndex = pathStartIndex + 1;
                                     pathIndex < root.actualPoints.length; ++pathIndex) {
                                    if (root.actualPoints[pathIndex].breakBefore) {
                                        context.moveTo(root.mapX(root.actualPoints[pathIndex].east),
                                                       root.mapY(root.actualPoints[pathIndex].north))
                                    } else {
                                        context.lineTo(root.mapX(root.actualPoints[pathIndex].east),
                                                       root.mapY(root.actualPoints[pathIndex].north))
                                    }
                                }
                                context.stroke()
                            }
                            context.globalAlpha = 1

                            var actualEnd = root.actualPoints[root.actualPoints.length - 1]
                            context.fillStyle = root.pathColor()
                            context.beginPath()
                            context.arc(root.mapX(actualEnd.east), root.mapY(actualEnd.north), 3.2,
                                        0, Math.PI * 2)
                            context.fill()
                        }
                    }
                }

                MouseArea {
                    id: mapMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    property real previousX: 0
                    property real previousY: 0
                    property real pressX: 0
                    property real pressY: 0
                    property bool moved: false
                    onPressed: mouse => {
                        previousX = mouse.x
                        previousY = mouse.y
                        pressX = mouse.x
                        pressY = mouse.y
                        moved = false
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return
                        var deltaX = mouse.x - previousX
                        var deltaY = mouse.y - previousY
                        if (Math.hypot(mouse.x - pressX, mouse.y - pressY) >= 4)
                            moved = true
                        if (moved) {
                            root.viewCenterEast -= deltaX / root.mapScale
                            root.viewCenterNorth += deltaY / root.mapScale
                            root.viewInitialized = true
                            root.viewManuallyAdjusted = true
                        }
                        previousX = mouse.x
                        previousY = mouse.y
                    }
                    onReleased: mouse => {
                        if (!moved && mouse.button === Qt.LeftButton && !root.editingLocked)
                            root.addPoint(mouse.x, mouse.y)
                    }
                    onWheel: wheel => {
                        var factor = Math.pow(1.0015, wheel.angleDelta.y)
                        root.zoomAt(wheel.x, wheel.y, factor)
                        wheel.accepted = true
                    }
                }

                Repeater {
                    model: missionModel
                    delegate: Item {
                        id: marker
                        required property int index
                        required property real eastMeters
                        required property real northMeters
                        readonly property real tipY: 41
                        width: 36
                        height: 45
                        x: root.mapX(eastMeters) - width / 2
                        y: root.mapY(northMeters) - tipY
                        z: root.selectedIndex === index ? 6 : 5

                        Shape {
                            x: 1
                            y: 3
                            width: 34
                            height: 42
                            opacity: root.selectedIndex === marker.index ? 0.28 : 0.18
                            ShapePath {
                                strokeWidth: 0
                                fillColor: "#000000"
                                startX: 17; startY: 2
                                PathCubic { x: 3; y: 15; control1X: 8; control1Y: 2; control2X: 3; control2Y: 8 }
                                PathCubic { x: 17; y: 40; control1X: 3; control1Y: 25; control2X: 12; control2Y: 34 }
                                PathCubic { x: 31; y: 15; control1X: 22; control1Y: 34; control2X: 31; control2Y: 25 }
                                PathCubic { x: 17; y: 2; control1X: 31; control1Y: 8; control2X: 26; control2Y: 2 }
                            }
                        }

                        Shape {
                            x: 1
                            y: 0
                            width: 34
                            height: 42
                            ShapePath {
                                strokeWidth: root.selectedIndex === marker.index ? 2 : 1.5
                                strokeColor: root.selectedIndex === marker.index
                                             ? Theme.accentHover : Theme.lineStrong
                                fillColor: root.selectedIndex === marker.index
                                           ? Theme.accent : Theme.surface
                                startX: 17; startY: 2
                                PathCubic { x: 3; y: 15; control1X: 8; control1Y: 2; control2X: 3; control2Y: 8 }
                                PathCubic { x: 17; y: 40; control1X: 3; control1Y: 25; control2X: 12; control2Y: 34 }
                                PathCubic { x: 31; y: 15; control1X: 22; control1Y: 34; control2X: 31; control2Y: 25 }
                                PathCubic { x: 17; y: 2; control1X: 31; control1Y: 8; control2X: 26; control2Y: 2 }
                            }
                        }

                        Rectangle {
                            x: 7
                            y: 6
                            width: 22
                            height: 18
                            radius: 9
                            color: root.selectedIndex === marker.index
                                   ? Qt.lighter(Theme.accent, 1.12) : Theme.surfaceRaised
                            border.width: 1
                            border.color: root.selectedIndex === marker.index
                                          ? Qt.rgba(1, 1, 1, 0.34) : Theme.line
                            Rectangle {
                                x: 4; y: 3; width: 10; height: 4; radius: 2
                                color: root.selectedIndex === marker.index
                                       ? Qt.rgba(1, 1, 1, 0.24) : Qt.rgba(1, 1, 1, 0.55)
                            }
                            Text {
                                anchors.centerIn: parent
                                anchors.verticalCenterOffset: 1
                                text: marker.index + 1
                                color: root.selectedIndex === marker.index
                                       ? Theme.accentContent : Theme.textPrimary
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.textTiny
                                font.weight: Font.DemiBold
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: !root.editingLocked
                            hoverEnabled: true
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            property real startMapX: 0
                            property real startMapY: 0
                            property real startEast: 0
                            property real startNorth: 0
                            onPressed: mouse => {
                                root.selectedIndex = marker.index
                                var mapPoint = marker.mapToItem(mapArea, mouse.x, mouse.y)
                                startMapX = mapPoint.x
                                startMapY = mapPoint.y
                                startEast = marker.eastMeters
                                startNorth = marker.northMeters
                                mouse.accepted = true
                            }
                            onPositionChanged: mouse => {
                                if (!pressed)
                                    return
                                var mapPoint = marker.mapToItem(mapArea, mouse.x, mouse.y)
                                var nextEast = startEast + (mapPoint.x - startMapX) / root.mapScale
                                var nextNorth = startNorth - (mapPoint.y - startMapY) / root.mapScale
                                missionModel.setProperty(marker.index, "eastMeters",
                                                         Math.round(nextEast * 10) / 10)
                                missionModel.setProperty(marker.index, "northMeters",
                                                         Math.round(nextNorth * 10) / 10)
                                root.dirty = true
                                routeCanvas.requestPaint()
                            }
                            onReleased: routeCanvas.requestPaint()
                            onClicked: mouse => mouse.accepted = true
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
                    width: 100
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
                        iconSource: App.missionPaused ? "qrc:/icons/play.svg" : "qrc:/icons/pause.svg"
                        kind: "secondary"
                        enabled: !App.missionBusy
                        onClicked: App.missionPaused ? App.resumeMission() : App.pauseMission()
                    }
                    ActionButton {
                        visible: App.missionRunning || App.missionPaused
                        Layout.preferredWidth: 78
                        compact: true
                        text: "终止"
                        iconSource: "qrc:/icons/x.svg"
                        kind: "secondary"
                        enabled: !App.missionBusy
                        onClicked: App.abortMissionToHold()
                    }
                    ActionButton {
                        visible: App.missionMonitorState === "stopped" && App.inAir
                        Layout.preferredWidth: 78
                        compact: true
                        text: "返航"
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
                        text: App.missionBusy ? "正在处理" : "上传并执行"
                        iconSource: "qrc:/icons/upload.svg"
                        enabled: root.canExecute
                        toolTipText: root.executeBlockReason()
                        onClicked: root.executeMission()
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 252
            Layout.fillHeight: true
            radius: Theme.radiusLarge
            color: Theme.surface
            border.width: 1
            border.color: Theme.line

            ScrollView {
                anchors.fill: parent
                anchors.margins: root.compactEditor ? 12 : 14
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                width: parent.width
                spacing: root.compactEditor ? 7 : 9

                RowLayout {
                    Layout.fillWidth: true
                    ThemedIcon { Layout.preferredWidth: 18; Layout.preferredHeight: 18; source: "qrc:/icons/map-pin.svg"; iconColor: Theme.accentHover }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text { Layout.fillWidth: true; text: root.selectionValid ? String(missionModel.get(root.selectedIndex).name || ("航点 " + (root.selectedIndex + 1))) : "航点"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSubtitle; font.weight: Font.Medium; elide: Text.ElideRight }
                        Text { text: root.selectionValid ? "第 " + String(root.selectedIndex + 1).padStart(2, "0") + " 个航点" : "未选择"; color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                    }
                    Rectangle {
                        Layout.preferredWidth: 50; Layout.preferredHeight: 24; radius: 12
                        color: root.editingLocked ? Theme.warningSoft : Theme.accentSoft
                        Text { anchors.centerIn: parent; text: root.editingLocked ? "已锁定" : "编辑中"; color: root.editingLocked ? Theme.warning : Theme.accentHover; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                    }
                }

                Text {
                    visible: !root.compactEditor
                    Layout.fillWidth: true
                    text: root.editingLocked
                          ? (App.missionPaused
                             ? "任务暂停时保持航线锁定；请继续任务，结束后再清除或修改。"
                             : "任务执行期间航线已锁定，结束后才能修改。")
                          : "调整当前航点；拖动地图标记可修改位置。"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTiny
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: "航点名称"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                    TextField {
                        id: waypointNameField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        enabled: root.selectionValid && !root.editingLocked
                        maximumLength: 24
                        selectByMouse: true
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSmall
                        onEditingFinished: {
                            var nextName = text.trim()
                            if (!nextName.length) nextName = "航点 " + (root.selectedIndex + 1)
                            text = nextName
                            root.setPointValue("name", nextName)
                        }
                        background: Rectangle { radius: Theme.radiusMedium; color: Theme.surfaceRaised; border.width: 1; border.color: waypointNameField.activeFocus ? Theme.accent : Theme.line }

                        Binding {
                            target: waypointNameField
                            property: "text"
                            when: !waypointNameField.activeFocus
                            restoreMode: Binding.RestoreBindingOrValue
                            value: root.selectionValid
                                   ? String(missionModel.get(root.selectedIndex).name
                                            || ("航点 " + (root.selectedIndex + 1)))
                                   : ""
                        }
                    }
                }

                component WaypointNumberRow: RowLayout {
                    id: waypointRow
                    property string label: "参数"
                    property string roleName: ""
                    property real currentValue: 0
                    property real minimum: 0
                    property real maximum: 100
                    property real step: 1
                    property int decimalPlaces: 1
                    property string unitText: ""
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    spacing: 8
                    Text { Layout.preferredWidth: 58; text: waypointRow.label; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                    NumericField {
                        Layout.fillWidth: true
                        value: waypointRow.currentValue
                        from: waypointRow.minimum; to: waypointRow.maximum
                        stepSize: waypointRow.step; decimals: waypointRow.decimalPlaces
                        unit: waypointRow.unitText
                        enabled: root.selectionValid && !root.editingLocked
                        accessibleName: waypointRow.label
                        onValueEdited: value => root.setPointValue(waypointRow.roleName, value)
                    }
                }

                WaypointNumberRow { label: "相对高度"; roleName: "altitudeMeters"; currentValue: root.selectionValid ? missionModel.get(root.selectedIndex).altitudeMeters : 8; minimum: 1; maximum: 100; step: 0.1; decimalPlaces: 1; unitText: "m" }
                WaypointNumberRow { label: "飞行速度"; roleName: "speedMetersPerSecond"; currentValue: root.selectionValid ? missionModel.get(root.selectedIndex).speedMetersPerSecond : 4; minimum: 0.5; maximum: 10; step: 0.1; decimalPlaces: 1; unitText: "m/s" }
                WaypointNumberRow { label: "航向角"; roleName: "yawDegrees"; currentValue: root.selectionValid ? missionModel.get(root.selectedIndex).yawDegrees : 0; minimum: -180; maximum: 180; step: 1; decimalPlaces: 0; unitText: "°" }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }
                Text { text: "位置（相对起飞点）"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textTiny }
                WaypointNumberRow { label: "向东"; roleName: "eastMeters"; currentValue: root.selectionValid ? missionModel.get(root.selectedIndex).eastMeters : 0; minimum: -2000; maximum: 2000; step: 0.1; decimalPlaces: 1; unitText: "m" }
                WaypointNumberRow { label: "向北"; roleName: "northMeters"; currentValue: root.selectionValid ? missionModel.get(root.selectedIndex).northMeters : 0; minimum: -2000; maximum: 2000; step: 0.1; decimalPlaces: 1; unitText: "m" }

                ColumnLayout {
                    Layout.fillWidth: true; spacing: 6
                    Text { text: "任务结束后"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.textSmall }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        radius: Theme.radiusMedium
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: Theme.line
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 2
                            spacing: 2
                            Repeater {
                                model: [
                                    { label: "返航", value: "rtl" },
                                    { label: "降落", value: "land" },
                                    { label: "悬停", value: "hold" }
                                ]
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: Theme.radiusSmall
                                    color: missionSettings.endAction === modelData.value
                                           ? Theme.surface : endActionMouse.containsMouse
                                                             ? Theme.surfaceInteractive : "transparent"
                                    border.width: missionSettings.endAction === modelData.value ? 1 : 0
                                    border.color: activeFocus ? Theme.accent : Theme.lineStrong
                                    opacity: root.editingLocked ? 0.55 : 1
                                    activeFocusOnTab: !root.editingLocked
                                    Accessible.name: "任务结束后" + modelData.label
                                    Accessible.role: Accessible.RadioButton
                                    Accessible.checked: missionSettings.endAction === modelData.value
                                    Keys.onReturnPressed: selectEndAction()
                                    Keys.onEnterPressed: selectEndAction()
                                    Keys.onSpacePressed: selectEndAction()

                                    function selectEndAction() {
                                        if (root.editingLocked) return
                                        if (missionSettings.endAction !== modelData.value) {
                                            missionSettings.endAction = modelData.value
                                            root.dirty = true
                                        }
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: missionSettings.endAction === modelData.value
                                               ? Theme.textPrimary : Theme.textMuted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.textTiny
                                        font.weight: missionSettings.endAction === modelData.value
                                                     ? Font.Medium : Font.Normal
                                    }
                                    MouseArea {
                                        id: endActionMouse
                                        anchors.fill: parent
                                        enabled: !root.editingLocked
                                        hoverEnabled: true
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: parent.selectEndAction()
                                    }
                                }
                            }
                        }
                    }
                }

                Item { Layout.preferredHeight: 4 }

                ActionButton {
                    Layout.fillWidth: true
                    compact: root.compactEditor
                    text: "删除此航点"
                    iconSource: "qrc:/icons/trash-2.svg"
                    kind: "danger"
                    enabled: !root.editingLocked && missionModel.count > 0
                    onClicked: root.removeSelectedPoint()
                }

                ActionButton {
                    visible: App.missionUploaded && !App.missionRunning && !App.missionPaused
                    Layout.fillWidth: true
                    text: "清除飞行器任务"
                    iconSource: "qrc:/icons/rotate-ccw.svg"
                    kind: "secondary"
                    enabled: !App.missionBusy
                    onClicked: clearDialog.open()
                }
                }
            }
        }
    }

    Dialog {
        id: routeLibraryDialog
        anchors.centerIn: parent
        width: Math.min(520, root.width - 40)
        height: Math.min(App.savedRoutes.length === 0 ? 320 : 440, root.height - 40)
        modal: true
        padding: 16
        background: Rectangle {
            radius: Theme.radiusLarge
            color: Theme.surface
            border.width: 1
            border.color: Theme.lineStrong
        }
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "航线库"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textTitle
                    font.weight: Font.Medium
                }
                Item { Layout.fillWidth: true }
                CompactIconButton {
                    iconSource: "qrc:/icons/folder-open.svg"
                    accessibleLabel: "打开航线保存目录"
                    toolTipText: "打开保存目录"
                    onClicked: App.openRoutesFolder()
                }
                CompactIconButton {
                    iconSource: "qrc:/icons/x.svg"
                    accessibleLabel: "关闭航线库"
                    toolTipText: "关闭"
                    onClicked: routeLibraryDialog.close()
                }
            }

            Text {
                Layout.fillWidth: true
                text: "加载时会以无人机当前位置为新起点，保持航线形状不变。"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusMedium
                color: Theme.surface
                border.width: 1
                border.color: Theme.line

                Text {
                    anchors.centerIn: parent
                    visible: App.savedRoutes.length === 0
                    text: "还没有保存的航线"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.textSmall
                }

                ListView {
                    id: routeLibraryList
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    spacing: 4
                    model: App.savedRoutes
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        id: routeLibraryRow
                        required property var modelData
                        width: routeLibraryList.width
                        height: 62
                        radius: Theme.radiusSmall
                        color: root.selectedLibraryRouteId === modelData.id
                               ? Theme.accentSoft
                               : routeRowMouse.containsMouse ? Theme.surfaceRaised
                                                            : "transparent"
                        border.width: activeFocus ? 1 : 0
                        border.color: Theme.accent
                        activeFocusOnTab: true
                        Accessible.name: modelData.name + "，" + modelData.waypointCount + " 个航点"
                        Accessible.role: Accessible.Button
                        Keys.onSpacePressed: selectRoute()
                        Keys.onReturnPressed: root.loadLibraryRoute(modelData.id)
                        Keys.onEnterPressed: root.loadLibraryRoute(modelData.id)

                        function selectRoute() {
                            root.selectedLibraryRouteId = modelData.id
                            root.selectedLibraryRouteName = modelData.name
                            root.routeOperationMessage = ""
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2
                            height: 34
                            radius: 1
                            visible: root.selectedLibraryRouteId === modelData.id
                            color: Theme.accent
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textSmall
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.waypointCount + " 个航点  ·  "
                                          + modelData.distanceMeters + " m  ·  约 "
                                          + root.formatDuration(modelData.estimatedSeconds)
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.textTiny
                                }
                            }
                            Text {
                                text: modelData.updatedLabel
                                color: Theme.textFaint
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.textTiny
                            }
                        }
                        MouseArea {
                            id: routeRowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: parent.selectRoute()
                            onDoubleClicked: root.loadLibraryRoute(modelData.id)
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.textTiny + 4
                text: root.routeOperationMessage
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textTiny
                elide: Text.ElideRight
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    Layout.preferredWidth: 76
                    compact: true
                    text: "新建"
                    kind: "secondary"
                    enabled: !root.editingLocked
                    onClicked: missionModel.count > 0 ? routeNewDialog.open()
                                                       : root.createNewRoute()
                }
                Item { Layout.fillWidth: true }
                CompactIconButton {
                    iconSource: "qrc:/icons/trash-2.svg"
                    accessibleLabel: "删除所选航线"
                    toolTipText: "删除航线"
                    danger: true
                    enabled: root.selectedLibraryRouteId.length > 0 && !root.editingLocked
                    onClicked: routeDeleteDialog.open()
                }
                CompactIconButton {
                    iconSource: "qrc:/icons/copy.svg"
                    accessibleLabel: "复制所选航线"
                    toolTipText: "复制航线"
                    enabled: root.selectedLibraryRouteId.length > 0 && !root.editingLocked
                    onClicked: {
                        var response = App.duplicateSavedRoute(root.selectedLibraryRouteId)
                        root.routeOperationMessage = response.message || ""
                        if (response.success) {
                            root.selectedLibraryRouteId = response.id
                            root.selectedLibraryRouteName = response.name
                        }
                    }
                }
                CompactIconButton {
                    iconSource: "qrc:/icons/pencil.svg"
                    accessibleLabel: "重命名所选航线"
                    toolTipText: "重命名"
                    enabled: root.selectedLibraryRouteId.length > 0 && !root.editingLocked
                    onClicked: {
                        root.routeNameDialogMode = "rename"
                        routeNameField.text = root.selectedLibraryRouteName
                        routeNameDialog.open()
                        routeNameField.forceActiveFocus()
                        routeNameField.selectAll()
                    }
                }
                ActionButton {
                    Layout.preferredWidth: 84
                    compact: true
                    text: "加载"
                    enabled: root.selectedLibraryRouteId.length > 0 && !root.editingLocked
                    onClicked: root.loadLibraryRoute(root.selectedLibraryRouteId)
                }
            }
        }
    }

    Dialog {
        id: routeNewDialog
        anchors.centerIn: parent
        width: Math.min(390, root.width - 40)
        modal: true
        padding: 20
        background: Rectangle {
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

    Dialog {
        id: routeNameDialog
        anchors.centerIn: parent
        width: Math.min(390, root.width - 40)
        modal: true
        padding: 20
        background: Rectangle {
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

    Dialog {
        id: routeDeleteDialog
        anchors.centerIn: parent
        width: Math.min(390, root.width - 40)
        modal: true
        padding: 20
        background: Rectangle {
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

    Dialog {
        id: executeDialog
        anchors.centerIn: parent
        width: Math.min(430, root.width - 40)
        modal: true
        padding: 20
        background: Rectangle { radius: Theme.radiusLarge; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.lineStrong }
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

    Dialog {
        id: clearDialog
        anchors.centerIn: parent
        width: Math.min(390, root.width - 40)
        modal: true
        padding: 20
        background: Rectangle { radius: Theme.radiusLarge; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.lineStrong }
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
