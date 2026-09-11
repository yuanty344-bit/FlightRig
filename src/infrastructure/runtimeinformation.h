#pragma once

#include <QString>

// Read-only process/build identity. No settings or controller state.
namespace RuntimeInformation {
QString applicationVersion();
QString qtRuntimeVersion();
QString mavsdkVersion();
QString buildDescription();
QString platformDescription();
}
