#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Four monotonic timestamps, never wall clocks. The one-way estimate assumes
// symmetric transit; half the measured round trip is its uncertainty bound.
// Keep the least congested recent exchange, and fail closed if it expires.
class SensorClockSync
{
public:
    using Time = std::int64_t;
    void reset() { m_samples.clear(); }
    bool observe(Time localSend, Time remoteReceive, Time remoteSend, Time localReceive)
    {
        if (localSend < 0 || remoteReceive < 0 || remoteSend < remoteReceive
            || localReceive < localSend) return false;
        const auto roundTrip = (localReceive - localSend) - (remoteSend - remoteReceive);
        if (roundTrip < 0 || roundTrip > 100) return false;
        prune(localReceive);
        if (m_samples.size() >= 16) m_samples.erase(m_samples.begin());
        m_samples.push_back({localReceive, roundTrip,
            ((double(localSend) - double(remoteReceive))
             + (double(localReceive) - double(remoteSend))) / 2.0});
        return true;
    }
    int latency(Time remoteStamp, Time localReceive) const
    {
        const auto *sample = best(localReceive);
        if (!sample || remoteStamp < 0) return -1;
        const double age = double(localReceive) - double(remoteStamp) - sample->offset;
        if (age < -(sample->roundTrip / 2.0 + 2.0)) return -1;
        return static_cast<int>(std::llround(std::clamp(age, 0.0, 60000.0)));
    }
    int uncertainty(Time now) const
    {
        const auto *sample = best(now);
        return sample ? static_cast<int>((sample->roundTrip + 1) / 2 + 1) : -1;
    }
private:
    struct Sample { Time at; Time roundTrip; double offset; };
    const Sample *best(Time now) const
    {
        const Sample *result = nullptr;
        for (const auto &sample : m_samples) {
            if (now < sample.at || now - sample.at > 8000) continue;
            if (!result || sample.roundTrip <= result->roundTrip) result = &sample;
        }
        return result;
    }
    void prune(Time now)
    {
        std::erase_if(m_samples, [now](const Sample &s) { return now < s.at || now - s.at > 8000; });
    }
    std::vector<Sample> m_samples;
};
