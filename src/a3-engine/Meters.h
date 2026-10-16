// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>

namespace a3
{
// Peak and RMS per side with beat-analyzer's ballistics
// (beat-analyzer/src/analysis/vu_meter.cpp, VuMeter::followRms/notePeak, and
// the defaults VU_RMS_ATTACK 0.8 / VU_RMS_RELEASE 0.2 in beat_analyzer_app.cpp):
// the RMS of each window follows with a one-pole, 0.8 towards a louder window,
// 0.2 towards a quieter one. The analyzer's window is its period, 128 frames on
// the rig; here it is a fixed 128-frame window counted across push() calls, so
// the reading does not depend on the period. Peak is the analyzer's too: the
// raw maximum since the last takePeak() (VuMeter::notePeak, a CAS-max, reset
// by takePeakLinear), so a reader polling at its own rate misses no transient;
// no hold, no fall (the displays apply those).
//
// push() runs on the audio thread; takePeak() and rms() may be called from any thread.
class Meters
{
public:
    static constexpr int window = 128;
    static constexpr double rmsAttack = 0.8;
    static constexpr double rmsRelease = 0.2;

    void prepare (double sampleRate);
    void push (const float* const* io, int n);   // stereo, any n >= 0
    float takePeak (int ch);   // the maximum since the last take; resets it to 0
    float rms (int ch) const;

private:
    void closeWindow();

    struct Side
    {
        double sumSquares = 0, windowPeak = 0, rms = 0;
        std::atomic<float> peakSinceTake { 0.0f }, rmsOut { 0.0f };
    };

    Side sides[2];
    int framesInWindow = 0;
};
}
