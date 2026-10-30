// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "RoundTripProbe.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <juce_dsp/juce_dsp.h>

namespace a3app
{
namespace
{
// A peak is the loop only when it stands this far above every lag away from it (an MLS through a clean
// digital loop stands about 20 x above its own sidelobes) and the recording there is mostly the signal.
constexpr double minClarity = 6.0, minCoherence = 0.5;
constexpr int peakGuard = 16;   // lags this close to the peak belong to it

// Fibonacci LFSR taps of a maximum-length sequence per order (each checked for period 2^n - 1).
std::vector<int> tapsFor (int order)
{
    switch (order)
    {
        case 10: return { 10, 7 };
        case 11: return { 11, 9 };
        case 12: return { 12, 11, 10, 4 };
        case 13: return { 13, 12, 11, 8 };
        case 14: return { 14, 13, 12, 2 };
        case 15: return { 15, 14 };
        case 16: return { 16, 15, 13, 4 };
        default: return {};
    }
}

// Cross-correlation of `recorded` against `sent` for lags 0..maxLag, by FFT (the control thread's, not the audio thread's).
std::vector<float> correlate (const std::vector<float>& sent, const std::vector<float>& recorded, int maxLag)
{
    const int order = (int) std::ceil (std::log2 ((double) (recorded.size() + sent.size())));
    juce::dsp::FFT fft (order);
    const auto size = (size_t) fft.getSize();
    std::vector<float> heard (2 * size, 0.0f), signal (2 * size, 0.0f);
    std::copy (recorded.begin(), recorded.end(), heard.begin());
    std::copy (sent.begin(), sent.end(), signal.begin());
    fft.performRealOnlyForwardTransform (heard.data());
    fft.performRealOnlyForwardTransform (signal.data());
    auto* h = reinterpret_cast<std::complex<float>*> (heard.data());
    const auto* s = reinterpret_cast<const std::complex<float>*> (signal.data());
    for (size_t bin = 0; bin < size; ++bin)
        h[bin] *= std::conj (s[bin]);
    fft.performRealOnlyInverseTransform (heard.data());
    heard.resize ((size_t) maxLag + 1);
    return heard;
}

double dotAt (const std::vector<float>& sent, const std::vector<float>& recorded, int lag, double& recordedEnergy)
{
    double dot = 0.0;
    recordedEnergy = 0.0;
    for (size_t i = 0; i < sent.size(); ++i)
    {
        const double heard = recorded[(size_t) lag + i];
        dot += (double) sent[i] * heard;
        recordedEnergy += heard * heard;
    }
    return dot;
}
}

std::vector<float> testSequence (int order, float amplitude)
{
    const auto taps = tapsFor (order);
    if (taps.empty())
        return {};
    std::vector<float> sequence ((size_t) (1 << order) - 1);
    unsigned state = 1;
    for (auto& sample : sequence)
    {
        sample = (state & 1u) != 0 ? amplitude : -amplitude;
        unsigned bit = 0;
        for (const int tap : taps)
            bit ^= state >> (order - tap);
        state = (state >> 1) | ((bit & 1u) << (order - 1));
    }
    return sequence;
}

LoopDelay findLoopDelay (const std::vector<float>& sent, const std::vector<float>& recorded)
{
    LoopDelay result;
    if (sent.empty() || recorded.size() < sent.size())
        return result;
    const int maxLag = (int) (recorded.size() - sent.size());
    const auto correlation = correlate (sent, recorded, maxLag);

    int peak = 0;
    for (int lag = 1; lag <= maxLag; ++lag)
        if (std::abs (correlation[(size_t) lag]) > std::abs (correlation[(size_t) peak]))
            peak = lag;
    float elsewhere = 0.0f;
    for (int lag = 0; lag <= maxLag; ++lag)
        if (std::abs (lag - peak) > peakGuard)
            elsewhere = std::max (elsewhere, std::abs (correlation[(size_t) lag]));

    double recordedEnergy = 0.0, sentEnergy = 0.0;
    const double dot = dotAt (sent, recorded, peak, recordedEnergy);
    for (const auto sample : sent)
        sentEnergy += (double) sample * sample;
    result.samples = peak;
    result.coherence = recordedEnergy > 0.0 ? std::abs (dot) / std::sqrt (sentEnergy * recordedEnergy) : 0.0;
    result.clarity = elsewhere > 0.0f ? std::abs (correlation[(size_t) peak]) / elsewhere : (result.coherence > 0.0 ? 1.0e9 : 0.0);
    result.found = result.coherence >= minCoherence && result.clarity >= minClarity;
    return result;
}

RoundTripProbe::RoundTripProbe (int maxDelaySamples)
    : signal (testSequence()), recording (signal.size() + (size_t) std::max (0, maxDelaySamples), 0.0f)
{
}

bool RoundTripProbe::start()
{
    int expected = idle;
    return state.compare_exchange_strong (expected, armed);
}

bool RoundTripProbe::busy() const { return state.load() != idle; }
bool RoundTripProbe::finished() const { return state.load() == done; }

std::optional<LoopDelay> RoundTripProbe::collect()
{
    if (state.load (std::memory_order_acquire) != done)
        return std::nullopt;
    const auto found = findLoopDelay (signal, recording);
    state.store (idle, std::memory_order_release);
    return found;
}

void RoundTripProbe::process (const float* loopIn, float* loopOut, int numSamples)
{
    int now = state.load (std::memory_order_acquire);
    if (now == armed)
    {
        position = 0;
        state.store (running, std::memory_order_relaxed);
        now = running;
    }
    if (now != running)
    {
        if (loopOut != nullptr)
            std::fill (loopOut, loopOut + numSamples, 0.0f);
        return;
    }
    const int total = (int) recording.size(), played = (int) signal.size();
    for (int i = 0; i < numSamples; ++i, ++position)
    {
        const bool recordingOn = position < total;
        if (recordingOn)
            recording[(size_t) position] = loopIn != nullptr ? loopIn[i] : 0.0f;
        if (loopOut != nullptr)
            loopOut[i] = position < played ? signal[(size_t) position] : 0.0f;
    }
    if (position >= total)
        state.store (done, std::memory_order_release);
}

RoundTripMeter::RoundTripMeter (std::optional<RoundTripLoop> l, juce::String why, std::optional<AnalogLoop> pair, juce::String analogWhy)
    : loop (l), unavailable (std::move (why)), analog (std::move (pair)), analogUnavailable (std::move (analogWhy))
{
    if (analog)
        stored = analog->converters;
    if (! loop)
        last = { RoundTripReport::State::noLoop, false, 0, 0, "no loop configured" };
    else if (unavailable.isNotEmpty())
        last = { RoundTripReport::State::noLoop, false, 0, 0, unavailable };
    else
        last.state = RoundTripReport::State::notMeasured;
}

void RoundTripMeter::request()
{
    if (hasLoop())
        probe.start();
}

void RoundTripMeter::requestAnalog()
{
    if (! hasAnalogPair())
        return;
    if (! hasLoop())
    {
        analogFailure = "the analog chain is measured against the digital loop, and there is none";
        return;
    }
    if (probe.busy() || analogProbe.busy())
    {
        analogFailure = "a measurement is running; ask again";
        return;
    }
    probe.start();
    analogProbe.start();
    analogRun = true;
    runDigital.reset();
    runAnalog.reset();
    analogFailure = {};
}

std::optional<ConverterShare> RoundTripMeter::takeNewConverterShare()
{
    auto share = fresh;
    fresh.reset();
    return share;
}

void RoundTripMeter::finishAnalogRun (juce::int64 wallMillis, double sampleRate)
{
    analogRun = false;
    if (! runDigital->found)
    {
        analogFailure = "no digital loop found in the same run";
        return;
    }
    if (! runAnalog->found)
    {
        analogFailure = "no analog loop found on output " + juce::String (analog->output) + " -> input " + juce::String (analog->input)
                      + ": is the cable plugged in?";
        return;
    }
    stored = fresh = ConverterShare { runAnalog->samples - runDigital->samples, sampleRate,
                                      juce::Time (wallMillis).formatted ("%Y-%m-%d") };
}

void RoundTripMeter::service (double now, juce::int64 wallMillis, double sampleRate)
{
    if (const auto heard = analogProbe.collect())
        runAnalog = heard;
    if (firstService < 0.0)
        firstService = now;
    if (! autoDone && now - firstService >= autoAfterSeconds)
    {
        autoDone = true;
        request();
    }
    const auto found = probe.collect();
    if (found && analogRun)
        runDigital = found;
    if (analogRun && runDigital && runAnalog)
        finishAnalogRun (wallMillis, sampleRate);
    if (! found)
        return;
    if (found->found)
    {
        last = { RoundTripReport::State::measured, false, found->samples, wallMillis, {} };
        return;
    }
    last = { RoundTripReport::State::noLoopFound, false, 0, wallMillis,
             "no loop found on " + loopRangeText (*loop, false) + " -> "
                 + loopRangeText (*loop, true) };
}

RoundTripReport RoundTripMeter::report() const
{
    auto report = last;
    report.measuring = probe.busy();
    report.analog = stored ? RoundTripReport::Analog::measured
                           : analog ? RoundTripReport::Analog::notMeasured : RoundTripReport::Analog::notConfigured;
    report.converters = stored.value_or (ConverterShare {});
    report.analogMeasuring = analogProbe.busy();
    report.analogReason = analogUnavailable.isNotEmpty() ? analogUnavailable : analogFailure;
    return report;
}
}
