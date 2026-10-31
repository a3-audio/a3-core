// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "XrunCount.h"

namespace a3app
{
namespace
{
// Running (a rate), its links made (when it makes any: not with --no-connect, nor on a JUCE device), the device there.
bool isUp (const BackendStatus& s)
{
    return s.sampleRate > 0 && (s.linksPlanned || ! s.connecting) && s.devicePresent && ! s.timing.restarting;
}
}

void XrunCount::goDown()
{
    isSettled = false;
    upSince = -1.0;
    sawMeasuring = false;
}

int XrunCount::since (const BackendStatus& s, double now)
{
    const std::lock_guard<std::mutex> guard (lock);
    if (s.deviceName != device)
    {
        device = s.deviceName;
        goDown();
    }
    if (! isUp (s))
    {
        goDown();
        return 0;
    }
    const auto& measured = s.roundTripMeasured;
    if (upSince < 0.0)
    {
        upSince = now;
        measuredAtUp = measured.atMillis;
    }
    sawMeasuring = sawMeasuring || measured.measuring;
    if (! isSettled)
    {
        const bool newMeasurement = measured.state == RoundTripReport::State::measured && measured.atMillis != measuredAtUp;
        const bool runEnded = sawMeasuring && ! measured.measuring;
        if (newMeasurement || runEnded || now - upSince >= settleSeconds)
        {
            isSettled = true;
            baseline = s.xruns;
        }
    }
    if (isSettled && s.xruns < baseline)
        baseline = s.xruns;   // a new backend counts from 0
    return isSettled ? s.xruns - baseline : 0;
}

void XrunCount::restart()
{
    const std::lock_guard<std::mutex> guard (lock);
    goDown();
    baseline = 0;
}

bool XrunCount::settled() const
{
    const std::lock_guard<std::mutex> guard (lock);
    return isSettled;
}
}
