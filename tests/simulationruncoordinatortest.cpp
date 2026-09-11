#include "app/simulationruncoordinator.h"
#include <QStringList>
#include <iostream>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x << '\n'; return 1; } } while (false)
int main() {
    bool running=true, connected=true, inAir=false, armed=false, busy=false;
    QStringList calls;
    std::vector<std::function<void()>> deferred;
    SimulationRunCoordinator run;
    int delay=0;
    run.bind({
        [&]{return running;}, [&]{return connected;}, [&]{return inAir;}, [&]{return armed;}, [&]{return busy;},
        [&]{calls<<"release";}, [&]{calls<<"mark";}, [&]{calls<<"land";},
        [&]{calls<<"finalize";}, [&]{calls<<"sensors";}, [&]{calls<<"docker";},
        [&]{calls<<"changed";}, [&](const QString &){calls<<"status";},
        [&](const QString &){calls<<"safety";},
        [&](int ms, std::function<void()> callback){delay=ms;deferred.push_back(callback);}
    });
    CHECK(!run.consumeRestart());
    run.requestRestart(); CHECK(run.consumeRestart()); CHECK(!run.consumeRestart());
    for (int mask=0; mask<8; ++mask) {
        connected=mask&1; inAir=mask&2; armed=mask&4;
        calls.clear(); run.stop();
        CHECK(calls.first()=="release");
        const bool blocked=connected&&(inAir||armed);
        CHECK(calls.contains("docker")==!blocked);
        if (!blocked) CHECK(calls == QStringList({"release","finalize","sensors","docker"}));
    }
    connected=true; inAir=true; armed=true;
    running=false; calls.clear(); run.landAndStop();
    CHECK(calls==QStringList({"status"}));
    running=true; busy=true; calls.clear(); run.landAndStop();
    CHECK(calls==QStringList({"status"}));
    busy=false; calls.clear(); run.landAndStop();
    CHECK(run.landingAndStopping());
    CHECK(calls==QStringList({"changed","release","mark","safety","land"}));
    calls.clear(); run.stop(); CHECK(calls==QStringList({"release","status"}));
    run.checkLanding(); CHECK(deferred.empty());
    inAir=false; armed=false; run.checkLanding();
    CHECK(deferred.size()==1 && delay==600);
    armed=true; calls.clear(); deferred.back()();
    CHECK(calls.empty() && run.landingAndStopping());
    armed=false; run.checkLanding(); run.checkLanding();
    CHECK(deferred.size()==3);
    calls.clear(); deferred[1](); deferred[2]();
    CHECK(!run.landingAndStopping());
    CHECK(calls==QStringList({"changed","status","safety","finalize","sensors","docker"}));
    // Disconnection during the delay must not stop a landing sequence.
    inAir=true; armed=true; run.landAndStop();
    inAir=false; armed=false; run.checkLanding(); connected=false;
    calls.clear(); deferred.back()(); CHECK(calls.empty() && run.landingAndStopping());
    run.setLandingAndStopping(false);
    bool replay=true, available=false, canStart=false;
    auto start=[&] {
        return run.start([&]{calls<<"replay";return replay;},
            [&]{calls<<"name";return available;},
            [&]{calls<<"preflight";return QVariantMap{{"canStart",canStart},{"summary","blocked"}};},
            [&]{calls<<"launch";});
    };
    calls.clear(); CHECK(!start()); CHECK(calls==QStringList({"replay","status"}));
    replay=false; calls.clear(); CHECK(!start()); CHECK(calls==QStringList({"replay","name","status"}));
    available=true; calls.clear(); CHECK(!start()); CHECK(calls==QStringList({"replay","name","preflight","status"}));
    canStart=true; calls.clear(); CHECK(start()); CHECK(calls==QStringList({"replay","name","preflight","launch"}));
    std::cout << "Start guards, landing recheck, shutdown order and restart tests passed\n";
}
