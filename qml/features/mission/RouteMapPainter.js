.pragma library

// Stateless renderer: all model, projection and style inputs are supplied by the caller.
function paint(context, width, height, state, palette) {
    context.reset()
    context.clearRect(0, 0, width, height)

    var gridMeters = state.gridStepMeters()
    var eastMinimum = state.eastAt(0)
    var eastMaximum = state.eastAt(width)
    var northMinimum = state.northAt(height)
    var northMaximum = state.northAt(0)
    context.lineWidth = 1
    context.strokeStyle = palette.line
    context.globalAlpha = state.worldMapAvailable ? 0.46 : 1
    var firstEast = Math.floor(eastMinimum / gridMeters) * gridMeters
    for (var east = firstEast; east <= eastMaximum; east += gridMeters) {
        var x = state.mapX(east)
        context.beginPath(); context.moveTo(x, 0); context.lineTo(x, height); context.stroke()
    }
    var firstNorth = Math.floor(northMinimum / gridMeters) * gridMeters
    for (var north = firstNorth; north <= northMaximum; north += gridMeters) {
        var y = state.mapY(north)
        context.beginPath(); context.moveTo(0, y); context.lineTo(width, y); context.stroke()
    }
    context.globalAlpha = 1

    // The mission plan is always visible. It is not the recorded flight
    // path, so the global "show flight path" option must not hide it.
    if (state.missionModel.count > 0) {
        var segmentStartEast = state.plannedStartValid
                               ? state.plannedStartEast : state.vehicleEast
        var segmentStartNorth = state.plannedStartValid
                                ? state.plannedStartNorth : state.vehicleNorth
        for (var index = 0; index < state.missionModel.count; ++index) {
            var point = state.missionModel.get(index)
            var isCompleted = (state.missionRunning || state.missionPaused
                               || state.missionMonitorState === "stopped"
                               || state.missionMonitorState === "completed")
                              && index < state.missionCurrentItem
            var isCurrent = (state.missionRunning || state.missionPaused)
                            && index === state.missionCurrentItem
            var hasBuildingRisk = state.segmentHasRisk(index)
            context.strokeStyle = hasBuildingRisk ? palette.danger
                                : isCurrent ? palette.accent : palette.textSecondary
            context.globalAlpha = isCompleted ? 0.35 : isCurrent ? 0.95 : 0.62
            context.lineWidth = hasBuildingRisk ? 2.2 : isCurrent ? 1.8 : 1.25
            context.setLineDash(hasBuildingRisk ? [4, 3]
                                : isCurrent ? [9, 5] : [6, 6])
            context.beginPath()
            context.moveTo(state.mapX(segmentStartEast),
                           state.mapY(segmentStartNorth))
            context.lineTo(state.mapX(point.eastMeters),
                           state.mapY(point.northMeters))
            context.stroke()

            if (isCurrent) {
                var startX = state.mapX(segmentStartEast)
                var startY = state.mapY(segmentStartNorth)
                var endX = state.mapX(point.eastMeters)
                var endY = state.mapY(point.northMeters)
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

        context.fillStyle = palette.surface
        context.strokeStyle = palette.textSecondary
        context.lineWidth = 1.2
        context.beginPath()
        context.arc(state.mapX(state.plannedStartValid
                              ? state.plannedStartEast : state.vehicleEast),
                    state.mapY(state.plannedStartValid
                              ? state.plannedStartNorth : state.vehicleNorth),
                    3.5, 0, Math.PI * 2)
        context.fill()
        context.stroke()
    }

    // DepthFusion uses the same local east/north frame as the
    // mission editor. Mark every route sample whose 3D clearance
    // is below the configured metre, independently of the static
    // building-outline safety check above.
    if (state.depthRouteSafety.available
            && state.depthRouteSafety.collisionPoints) {
        context.setLineDash([])
        for (var collisionIndex = 0;
             collisionIndex < state.depthRouteSafety.collisionPoints.length;
             ++collisionIndex) {
            var collision = state.depthRouteSafety.collisionPoints[collisionIndex]
            var collisionX = state.mapX(Number(collision.eastMeters || 0))
            var collisionY = state.mapY(Number(collision.northMeters || 0))
            context.fillStyle = palette.danger
            context.strokeStyle = palette.surface
            context.lineWidth = 2
            context.beginPath()
            context.arc(collisionX, collisionY, 6, 0, Math.PI * 2)
            context.fill()
            context.stroke()
        }
    }

    if (state.flightPathVisible && state.actualPoints.length > 1) {
        var pathStartIndex = state.visibleActualPathStartIndex()
        context.lineJoin = "round"
        context.lineCap = "round"

        // A narrow surface-coloured underlay keeps the actual path clear
        // when it crosses the dashed mission plan or map grid.
        for (var pass = 0; pass < 2; ++pass) {
            context.strokeStyle = pass === 0 ? palette.surface : state.pathColor()
            context.globalAlpha = pass === 0 ? 0.88 : 1
            context.lineWidth = state.pathLineWidth() + (pass === 0 ? 2.2 : 0)
            context.beginPath()
            context.moveTo(state.mapX(state.actualPoints[pathStartIndex].east),
                           state.mapY(state.actualPoints[pathStartIndex].north))
            for (var pathIndex = pathStartIndex + 1;
                 pathIndex < state.actualPoints.length; ++pathIndex) {
                if (state.actualPoints[pathIndex].breakBefore) {
                    context.moveTo(state.mapX(state.actualPoints[pathIndex].east),
                                   state.mapY(state.actualPoints[pathIndex].north))
                } else {
                    context.lineTo(state.mapX(state.actualPoints[pathIndex].east),
                                   state.mapY(state.actualPoints[pathIndex].north))
                }
            }
            context.stroke()
        }
        context.globalAlpha = 1

        var actualEnd = state.actualPoints[state.actualPoints.length - 1]
        context.fillStyle = state.pathColor()
        context.beginPath()
        context.arc(state.mapX(actualEnd.east), state.mapY(actualEnd.north), 3.2,
                    0, Math.PI * 2)
        context.fill()
    }
}
