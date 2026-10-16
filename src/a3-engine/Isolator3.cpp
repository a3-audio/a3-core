// SPDX-FileCopyrightText: 2018 Chris Johnson
// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later AND MIT
// Algorithm: Airwindows Isolator3 by Chris Johnson (MIT, 2018)
#include "Isolator3.h"
#include <algorithm>
#include <cmath>

namespace a3
{
namespace
{
// The original's rounded Butterworth section Qs (1 / (2 sin((2k+1) pi / 28))),
// copied rather than computed so the coefficients match bit for bit.
constexpr double sectionQ[] { 4.46570214, 1.51387132, 0.93979296, 0.70710678,
                              0.59051105, 0.52972649, 0.50316379 };
}

void Isolator3::prepare (double sampleRate)
{
    // Same operation order as the original, so the scale matches to the last bit.
    overallScale = 1.0;
    overallScale /= 44100.0;
    overallScale *= sampleRate;
    spacing = std::clamp (static_cast<int> (std::floor (overallScale)), 1, 16);
    lower = {};
    upper = {};
    for (auto& c : channels)
        c = {};
    grid.reset();
}

void Isolator3::setParameters (float iso, float q)
{
    pendingIso = iso;
    pendingQ = q;
}

double Isolator3::edgeToNormalisedFreq (double edge) const
{
    return std::max (0.00025, std::pow (edge, 2.0 * std::sqrt (overallScale)) * 0.4999);
}

void Isolator3::latchControlBlock()
{
    const double f = pendingIso;
    const double q = pendingQ;
    const double upperEdge = std::min (1.0, ((f * 2.0) * (1.0 - q)) + (f * q));
    const double lowerEdge = std::max (0.0, (((f * 2.0) - 1.0) * (1.0 - q)) + (f * q));
    const double upperFreq = edgeToNormalisedFreq (upperEdge);
    const double lowerFreq = edgeToNormalisedFreq (lowerEdge);
    for (int s = 0; s < sections; ++s)
    {
        upper.from[s] = upper.to[s];
        lower.from[s] = lower.to[s];
        upper.to[s] = BiquadCoeffs::lowPass (upperFreq, sectionQ[s]);
        lower.to[s] = BiquadCoeffs::lowPass (lowerFreq, sectionQ[s]);
    }
}

double Isolator3::ClipOnly2::process (double x, int spacing)
{
    x = std::clamp (x, -4.0, 4.0);
    if (wasPosClip)
        lastSample = (x < lastSample) ? 0.7058208 + (x * 0.2609148) : 0.2491717 + (lastSample * 0.7390851);
    wasPosClip = false;
    if (x > 0.9549925859)
    {
        wasPosClip = true;
        x = 0.7058208 + (lastSample * 0.2609148);
    }
    if (wasNegClip)
        lastSample = (x > lastSample) ? -0.7058208 + (x * 0.2609148) : -0.2491717 + (lastSample * 0.7390851);
    wasNegClip = false;
    if (x < -0.9549925859)
    {
        wasNegClip = true;
        x = -0.7058208 + (lastSample * 0.2609148);
    }
    intermediate[spacing] = x;
    const double out = lastSample;
    for (int k = spacing; k > 0; --k)
        intermediate[k - 1] = intermediate[k];
    lastSample = intermediate[0];
    return out;
}

double Isolator3::processSample (Channel& ch, double x, const BiquadCoeffs* lowerC, const BiquadCoeffs* upperC) const
{
    double lowPart = x;
    for (int s = 0; s < sections; ++s)
        lowPart = ch.lower[s].process (lowPart, lowerC[s]);
    double band = x - lowPart;
    for (int s = 0; s < sections; ++s)
        band = ch.upper[s].process (band, upperC[s]);
    return ch.clip.process (band, spacing);
}

void Isolator3::process (float* const* io, int numSamples)
{
    grid.advance (numSamples,
                  [this] { latchControlBlock(); },
                  [this, io] (int begin, int count)
                  {
                      const int blockPosition = grid.framesIntoBlock;
                      for (int i = 0; i < count; ++i)
                      {
                          // The original's per-sample weight: remaining frames / block size.
                          const double t = static_cast<double> (ControlGrid::size - 1 - (blockPosition + i))
                                           / ControlGrid::size;
                          BiquadCoeffs lowerC[sections], upperC[sections];
                          for (int s = 0; s < sections; ++s)
                          {
                              lowerC[s] = BiquadCoeffs::lerp (lower.from[s], lower.to[s], t);
                              upperC[s] = BiquadCoeffs::lerp (upper.from[s], upper.to[s], t);
                          }
                          for (int c = 0; c < 2; ++c)
                              io[c][begin + i] = static_cast<float> (
                                  processSample (channels[c], io[c][begin + i], lowerC, upperC));
                      }
                  });
}
}
