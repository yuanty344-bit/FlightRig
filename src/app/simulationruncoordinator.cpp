#include "app/simulationruncoordinator.h"

bool SimulationRunCoordinator::start(const std::function<bool()> &replaying,
    const std::function<bool()> &nameAvailable,
    const std::function<QVariantMap()> &preflightCheck,
    const std::function<void()> &launch)
{
    if (replaying()) {
        m_actions.status(QStringLiteral("请先退出历史回放，再启动仿真"));
        return false;
    }
    if (!nameAvailable()) {
        m_actions.status(QStringLiteral("当前容器名称已被占用，请在运行环境中更换名称"));
        return false;
    }
    const QVariantMap preflight = preflightCheck();
    if (!preflight.value(QStringLiteral("canStart")).toBool()) {
        m_actions.status(preflight.value(QStringLiteral("summary")).toString());
        return false;
    }
    launch();
    return true;
}

void SimulationRunCoordinator::stop()
{
    m_actions.releaseKeyboard();
    if (m_landingAndStopping) {
        m_actions.status(QStringLiteral("正在降落，落地并锁定后将自动停止仿真…"));
        return;
    }
    if (m_actions.connected() && (m_actions.inAir() || m_actions.armed())) {
        m_actions.status(QStringLiteral("飞行器仍在空中或已解锁，请使用“降落并停止”"));
        m_actions.safety(
                         QStringLiteral("已阻止飞行中直接停止仿真"));
        return;
    }
    m_actions.finalizeSensors();
    m_actions.stopSensors();
    m_actions.stopDocker();
}

void SimulationRunCoordinator::landAndStop()
{
    if (!m_actions.running()) {
        m_actions.status(QStringLiteral("仿真尚未运行"));
        return;
    }
    if (m_landingAndStopping) {
        m_actions.status(QStringLiteral("正在降落，落地并锁定后将自动停止仿真…"));
        return;
    }
    if (!m_actions.connected() || (!m_actions.inAir() && !m_actions.armed())) {
        stop();
        return;
    }
    if (m_actions.commandBusy()) {
        m_actions.status(QStringLiteral("当前指令尚未完成，请稍后再试"));
        return;
    }

    setLandingAndStopping(true);
    m_actions.releaseKeyboard();
    m_actions.markLanding();
    m_actions.safety(
                     QStringLiteral("已请求降落，落地后自动停止仿真"));
    m_actions.land();
}

void SimulationRunCoordinator::checkLanding()
{
    if (!m_landingAndStopping || !m_actions.running() || !m_actions.connected()
        || m_actions.inAir() || m_actions.armed()) {
        return;
    }

    m_actions.defer(600, [this] {
        if (!m_landingAndStopping || !m_actions.running() || !m_actions.connected()
            || m_actions.inAir() || m_actions.armed()) {
            return;
        }
        setLandingAndStopping(false);
        m_actions.status(QStringLiteral("落地与锁定已确认，正在停止仿真…"));
        m_actions.safety(
                         QStringLiteral("落地与锁定已确认，允许停止仿真"));
        m_actions.finalizeSensors();
        m_actions.stopSensors();
        m_actions.stopDocker();
    });
}


void SimulationRunCoordinator::setLandingAndStopping(bool value)
{
    if (m_landingAndStopping == value) return;
    m_landingAndStopping = value;
    m_actions.landingChanged();
}
