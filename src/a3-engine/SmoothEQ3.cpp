// SPDX-FileCopyrightText: 2018 Chris Johnson
// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later AND MIT
// Algorithm: Airwindows SmoothEQ3 by Chris Johnson (MIT, 2018)
#include "SmoothEQ3.h"
#include <cmath>

namespace a3
{
namespace
{
constexpr double pi = 3.14159265358979323846;

double gainLaw (double p)
{
    const double x = (p - 0.5) * 2.0;
    return 1.0 + x * x * x;
}

double onePoleCoefficient (double hz, double sampleRate)
{
    const double k = 2.0 - std::cos (2.0 * pi * hz / sampleRate);
    return k - std::sqrt (k * k - 1.0);
}
}

void SmoothEQ3::prepare (double sampleRate)
{
    high = BiquadCoeffs::lowPass (4000.0 / sampleRate, 1.0);
    low = BiquadCoeffs::lowPass (200.0 / sampleRate, 1.0);
    highPoleCoeff = onePoleCoefficient (4000.0, sampleRate);
    lowPoleCoeff = onePoleCoefficient (200.0, sampleRate);
    for (auto& c : channels)
        c = {};
    grid.reset();
}

void SmoothEQ3::setParameters (float treble, float mid, float bass)
{
    pendingTreble = treble;
    pendingMid = mid;
    pendingBass = bass;
}

void SmoothEQ3::latchControlBlock()
{
    gainTreble = gainLaw (pendingTreble);
    gainMid = gainLaw (pendingMid);
    gainBass = gainLaw (pendingBass);
}

void SmoothEQ3::processChannel (Channel& st, float* io, int begin, int count) const
{
    for (int i = begin; i < begin + count; ++i)
    {
        const double x = io[i];
        const double midAndBass = st.highLp.process (x, high);
        const double bass = st.lowLp.process (midAndBass, low);
        double y = bass * gainBass + (midAndBass - bass) * gainMid + (x - midAndBass) * gainTreble;
        st.highPole = st.highPole * highPoleCoeff + y * (1.0 - highPoleCoeff);
        st.lowPole = st.lowPole * lowPoleCoeff + st.highPole * (1.0 - lowPoleCoeff);
        y = st.lowPole * gainBass + (st.highPole - st.lowPole) * gainMid + (y - st.highPole) * gainTreble;
        io[i] = float (y);
    }
}

void SmoothEQ3::process (float* const* io, int numSamples)
{
    grid.advance (numSamples,
                  [this] { latchControlBlock(); },
                  [this, io] (int begin, int count)
                  {
                      processChannel (channels[0], io[0], begin, count);
                      processChannel (channels[1], io[1], begin, count);
                  });
}
}
