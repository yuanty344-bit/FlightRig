#include "infrastructure/runtimeinformation.h"
#include <QCoreApplication>
#include <QSysInfo>

namespace RuntimeInformation {
QString applicationVersion()
{
    return QCoreApplication::applicationVersion();
}

QString qtRuntimeVersion()
{
    return QString::fromLatin1(qVersion());
}

QString mavsdkVersion()
{
#ifdef FLIGHTBENCH_MAVSDK_VERSION
    return QStringLiteral(FLIGHTBENCH_MAVSDK_VERSION);
#else
    return QStringLiteral("3.17.1");
#endif
}

QString buildDescription()
{
#ifdef _MSC_VER
#ifdef NDEBUG
    return QStringLiteral("MSVC %1 · Release · 64 位").arg(_MSC_VER);
#else
    return QStringLiteral("MSVC %1 · Debug · 64 位").arg(_MSC_VER);
#endif
#else
    return QStringLiteral("C++ · 64 位");
#endif
}

QString platformDescription()
{
    return QStringLiteral("%1 · %2")
        .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
}

}
