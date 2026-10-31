// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// XRUNS from the moment the engine is up (2026-10-31, the maintainer: the xruns of a start or a reconnect do not
// count). Up: the engine runs and has made its links (when it makes any), the device is there and WirePlumber is not
// restarting. The count starts at the first round-trip measurement that ends after that, or settleSeconds of being
// up, whichever comes first. Down again (the device gone, a WirePlumber restart, another device, a new session) and
// up again is a start again; while down XRUNS shows 0. The backend's raw count is left as it is (BackendStatus::xrunsTotal).
#pragma once
#include "AudioBackend.h"
#include <mutex>

namespace a3app
{
class XrunCount
{
public:
    static constexpr double settleSeconds = 10.0;

    // The xruns since the engine was up (0 until then); `now` in seconds on a monotonic clock.
    int since (const BackendStatus&, double now);
    void restart();   // a new session: its backend counts from 0
    bool settled() const;

private:
    void goDown();

    mutable std::mutex lock;
    bool isSettled = false;
    int baseline = 0;
    double upSince = -1.0;              // < 0: not up
    juce::int64 measuredAtUp = 0;       // the measurement there was when it came up: it does not settle the count
    bool sawMeasuring = false;          // a measurement ran since it came up
    juce::String device;
};
}
