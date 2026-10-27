// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Generator.h"
#include <cmath>
#include <cstdlib>
#include <vector>

namespace a3bench
{
namespace
{
constexpr double twoPi = 6.283185307179586;
// Paul Kellet's pink filter on white noise of +-1, scaled so that the RMS of
// the result equals the amplitude. 1.764 is the unscaled RMS over 60 s of this
// generator's own noise (the filter's gain, not a tuned number).
constexpr double pinkUnscaledRms = 1.764;
constexpr double pinkNormalise = 1.0 / pinkUnscaledRms;
}

namespace
{
std::vector<std::string> splitColons (const std::string& text)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t at; (at = text.find (':', start)) != std::string::npos; start = at + 1)
        parts.push_back (text.substr (start, at - start));
    parts.push_back (text.substr (start));
    return parts;
}

bool number (const std::string& text, double& out)
{
    char* end = nullptr;
    out = std::strtod (text.c_str(), &end);
    return ! text.empty() && end == text.c_str() + text.size() && std::isfinite (out);
}
}

bool parseGenSpec (const std::string& text, GenSpec& out)
{
    const auto parts = splitColons (text);
    GenSpec spec;
    double level = 0.0;
    if (parts[0] == "sine" && parts.size() == 3)
    {
        if (! number (parts[1], spec.hz) || spec.hz <= 0.0 || ! number (parts[2], level))
            return false;
        spec.shape = Shape::sine;
    }
    else if ((parts[0] == "pink" || parts[0] == "sweep") && parts.size() == 2)
    {
        if (! number (parts[1], level))
            return false;
        spec.shape = parts[0] == "pink" ? Shape::pink : Shape::sweep;
    }
    else
        return false;
    if (level > 0.0)
        return false;   // above full scale is never what a bench run wants
    spec.levelDb = (float) level;
    out = spec;
    return true;
}

void Generator::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    phase = sweepPosition = 0.0;
    b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0;
}

void Generator::set (Shape newShape, double newHz, float levelDb)
{
    shape = newShape;
    hz = newHz;
    amplitude = std::pow (10.0f, levelDb / 20.0f);
}

float Generator::nextPink()
{
    rng ^= rng << 13;   // xorshift32: deterministic and allocation-free
    rng ^= rng >> 17;
    rng ^= rng << 5;
    const double white = (double) rng / 2147483648.0 - 1.0;
    b0 = 0.99886 * b0 + white * 0.0555179;
    b1 = 0.99332 * b1 + white * 0.0750759;
    b2 = 0.96900 * b2 + white * 0.1538520;
    b3 = 0.86650 * b3 + white * 0.3104856;
    b4 = 0.55000 * b4 + white * 0.5329522;
    b5 = -0.7616 * b5 - white * 0.0168980;
    const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
    b6 = white * 0.115926;
    return (float) (pink * pinkNormalise);
}

void Generator::render (float* l, float* r, int n)
{
    for (int i = 0; i < n; ++i)
    {
        float value = 0.0f;
        switch (shape)
        {
            case Shape::off:
                break;
            case Shape::sine:
                value = amplitude * (float) std::sin (phase);
                phase += twoPi * hz / sampleRate;
                break;
            case Shape::pink:
                value = amplitude * nextPink();
                break;
            case Shape::sweep:
            {
                const double sweepHz = sweepFromHz * std::pow (sweepToHz / sweepFromHz, sweepPosition);
                value = amplitude * (float) std::sin (phase);
                phase += twoPi * sweepHz / sampleRate;
                sweepPosition += 1.0 / (sweepSeconds * sampleRate);
                if (sweepPosition >= 1.0)
                    sweepPosition = 0.0;
                break;
            }
        }
        if (phase >= twoPi)
            phase -= twoPi;
        l[i] = r[i] = value;
    }
}
}
