#pragma once

#include <QString>

class SensorServiceController;
class FlightSessionRecorder;
class LidarMappingController;
class DepthMappingController;
class VisualSurveyController;
class GimbalInspectionController;

// Synchronous session finalization only. Owns no controllers or runtime state.
// Call before stopping the sensor service or finishing the flight session.
namespace SensorSessionFinalizer {
void finalize(const QString &sensorType, SensorServiceController &sensorService,
    FlightSessionRecorder &sessionRecorder, LidarMappingController &lidarMapping,
    DepthMappingController &depthMapping, VisualSurveyController &visualSurvey,
    GimbalInspectionController &gimbalInspection);
}
