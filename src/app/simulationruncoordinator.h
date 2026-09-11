#pragma once
#include <QString>
#include <QVariantMap>
#include <functional>
#include <utility>

// Owns stop/landing/restart coordination, not vehicle or Docker state.
// All actions are synchronous except defer, tied to the facade's lifetime.
class SimulationRunCoordinator {
public:
    struct Actions {
        std::function<bool()> running, connected, inAir, armed, commandBusy;
        std::function<void()> releaseKeyboard, markLanding, land, finalizeSensors,
            stopSensors, stopDocker, landingChanged;
        std::function<void(const QString &)> status, safety;
        std::function<void(int, std::function<void()>)> defer;
    };
    void bind(Actions actions) { m_actions = std::move(actions); }
    void stop();
    bool start(const std::function<bool()> &replaying,
               const std::function<bool()> &nameAvailable,
               const std::function<QVariantMap()> &preflightCheck,
               const std::function<void()> &launch);
    void landAndStop();
    void checkLanding();
    bool landingAndStopping() const { return m_landingAndStopping; }
    void setLandingAndStopping(bool value);
    void requestRestart() { m_restartRequested = true; }
    bool consumeRestart() { return std::exchange(m_restartRequested, false); }
private:
    Actions m_actions;
    bool m_landingAndStopping{false};
    bool m_restartRequested{false};
};
