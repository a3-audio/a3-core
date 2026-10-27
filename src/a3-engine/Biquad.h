// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Textbook bilinear-transform maths (the design juce::dsp::IIR makeLowPass
// produces), not Airwindows code, hence GPL alone.
#pragma once
#include <cmath>

namespace a3
{
struct BiquadCoeffs
{
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    // 2nd-order low-pass, prewarped; normalisedFreq = hz / sampleRate.
    static BiquadCoeffs lowPass (double normalisedFreq, double q)
    {
        const double k = std::tan (3.14159265358979323846 * normalisedFreq);
        const double norm = 1.0 / (1.0 + k / q + k * k);
        BiquadCoeffs c;
        c.b0 = k * k * norm;
        c.b1 = 2.0 * c.b0;
        c.b2 = c.b0;
        c.a1 = 2.0 * (k * k - 1.0) * norm;
        c.a2 = (1.0 - k / q + k * k) * norm;
        return c;
    }

    // t = 1 gives `from`, t = 0 gives `to`.
    static BiquadCoeffs lerp (const BiquadCoeffs& from, const BiquadCoeffs& to, double t)
    {
        const auto mix = [t] (double a, double b) { return a * t + b * (1.0 - t); };
        return { mix (from.b0, to.b0), mix (from.b1, to.b1), mix (from.b2, to.b2),
                 mix (from.a1, to.a1), mix (from.a2, to.a2) };
    }
};

// Transposed direct form II, like juce::dsp::IIR::Filter.
struct BiquadState
{
    double s1 = 0, s2 = 0;

    double process (double x, const BiquadCoeffs& c)
    {
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }
};
}
