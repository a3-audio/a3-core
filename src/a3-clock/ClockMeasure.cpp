// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockMeasure.h"

#include <charconv>
#include <cmath>

namespace a3clock
{
namespace
{
constexpr std::uint32_t freewheelFlag = 1u << 0;   // SPA_IO_CLOCK_FLAG_FREEWHEEL

// The text right after `"key": ` in the line, or empty.
std::string_view valueOf (std::string_view line, std::string_view key)
{
    for (std::size_t at = line.find (key); at != std::string_view::npos; at = line.find (key, at + 1))
    {
        if (at == 0 || line[at - 1] != '"' || at + key.size() >= line.size() || line[at + key.size()] != '"')
            continue;
        auto rest = line.substr (at + key.size() + 1);
        const auto colon = rest.find_first_not_of (" \t");
        if (colon == std::string_view::npos || rest[colon] != ':')
            return {};
        rest.remove_prefix (colon + 1);
        const auto start = rest.find_first_not_of (" \t");
        return start == std::string_view::npos ? std::string_view() : rest.substr (start);
    }
    return {};
}

template <typename Int>
bool integerOf (std::string_view line, std::string_view key, Int& out)
{
    const auto text = valueOf (line, key);
    const auto [end, problem] = std::from_chars (text.data(), text.data() + text.size(), out);
    return problem == std::errc() && end != text.data();
}

// A string value, without its quotes.
bool stringOf (std::string_view line, std::string_view key, std::string_view& out)
{
    const auto text = valueOf (line, key);
    if (text.size() < 2 || text.front() != '"')
        return false;
    const auto close = text.find ('"', 1);
    if (close == std::string_view::npos)
        return false;
    out = text.substr (1, close - 1);
    return true;
}

double nsOfSamples (std::int64_t samples, int rate) { return (double) samples * 1e9 / (double) rate; }
}

std::optional<ProfilerClock> parseProfilerClock (std::string_view line)
{
    std::string_view type, rate;
    if (! stringOf (line, "type", type) || type != "clock")
        return std::nullopt;
    ProfilerClock r;
    if (! (integerOf (line, "id", r.id) && integerOf (line, "flags", r.flags) && integerOf (line, "nsec", r.nsec)
           && integerOf (line, "position", r.position) && integerOf (line, "delay", r.delay) && integerOf (line, "xrun", r.xrun)
           && stringOf (line, "rate", rate) && stringOf (line, "name", r.name)))
        return std::nullopt;
    if (rate.substr (0, 2) != "1/")
        return std::nullopt;
    const auto [end, problem] = std::from_chars (rate.data() + 2, rate.data() + rate.size(), r.rate);
    if (problem != std::errc() || end != rate.data() + rate.size() || r.rate <= 0)
        return std::nullopt;
    return r;
}

std::optional<ProfilerDriver> parseProfilerDriver (std::string_view line)
{
    std::string_view type;
    ProfilerDriver d;
    if (! stringOf (line, "type", type) || type != "driver" || ! integerOf (line, "id", d.id) || ! stringOf (line, "name", d.name))
        return std::nullopt;
    return d;
}

ScarlettIntake::ScarlettIntake (IntakeConfig config) : cfg (std::move (config)) {}

void ScarlettIntake::addDriver (const ProfilerDriver& d)
{
    if (d.name.substr (0, cfg.node.size()) == cfg.node)
        scarlettId = d.id;
}

void ScarlettIntake::restart()
{
    if (started)
        ++breaks;
    started = false;
    clearWindow();
}

void ScarlettIntake::clearWindow()
{
    sumNsec = sumScarlett = 0.0;
    count = 0;
}

void ScarlettIntake::startEpoch (const ProfilerClock& r, std::int64_t samples)
{
    if (started)
        ++breaks;
    started = true;
    ++currentEpoch;
    id = r.id;
    rate = r.rate;
    xrun = r.xrun;
    epochSamples = samples;
    clearWindow();
}

ScarlettIntake::Taken ScarlettIntake::add (const ProfilerClock& r)
{
    if (scarlettId < 0 || r.id != scarlettId || r.rate <= 0)
        return Taken::ignored;   // another driver, or the Scarlett's not seen yet: never a fallback
    if ((r.flags & freewheelFlag) != 0)
    {
        restart();   // counted once, when the freewheel starts
        return Taken::ignored;
    }
    const std::int64_t samples = r.position + r.delay;
    bool broken = ! started || r.id != id || r.rate != rate || r.xrun != xrun;
    if (! broken)
    {
        const auto gap = r.nsec - lastNsec;
        const double predicted = (double) lastSamples + (double) gap * (double) rate / 1e9;
        broken = gap <= 0 || (double) gap > cfg.maxGapNs || std::abs (nsOfSamples (1, rate) * ((double) samples - predicted)) > cfg.maxJumpNs;
    }
    if (broken)
        startEpoch (r, samples);
    lastNsec = r.nsec;
    lastSamples = samples;

    const auto scarlettNs = (std::int64_t) std::llround (nsOfSamples (samples - epochSamples, rate));
    if (count == 0)
    {
        baseNsec = r.nsec;
        baseScarlettNs = scarlettNs;
    }
    sumNsec += (double) (r.nsec - baseNsec);
    sumScarlett += (double) (scarlettNs - baseScarlettNs);
    ++count;
    return broken ? Taken::discontinuity : Taken::used;
}

std::optional<ScarlettPoint> ScarlettIntake::take()
{
    if (count < cfg.minRecords)
        return std::nullopt;
    ScarlettPoint point;
    point.epoch = currentEpoch;
    point.records = count;
    point.monoNs = baseNsec + (std::int64_t) std::llround (sumNsec / count);
    point.scarlettNs = baseScarlettNs + (std::int64_t) std::llround (sumScarlett / count);
    clearWindow();
    return point;
}

void PhcHistory::add (PhcSample sample)
{
    if (size == capacity)
    {
        for (int i = 1; i < capacity; ++i)
            samples[(std::size_t) i - 1] = samples[(std::size_t) i];
        --size;
    }
    samples[(std::size_t) size++] = sample;
}

std::optional<std::int64_t> PhcHistory::phcAt (std::int64_t monoNs) const
{
    if (size < 2)
        return std::nullopt;
    int second = 1;   // the pair (second - 1, second): the first that ends after monoNs, else the last
    while (second < size - 1 && samples[(std::size_t) second].monoNs < monoNs)
        ++second;
    const auto& a = samples[(std::size_t) second - 1];
    const auto& b = samples[(std::size_t) second];
    if (b.monoNs == a.monoNs)
        return std::nullopt;
    const long double slope = (long double) (b.phcNs - a.phcNs) / (long double) (b.monoNs - a.monoNs);
    return a.phcNs + (std::int64_t) std::llround (slope * (long double) (monoNs - a.monoNs));
}

void RateEstimate::add (std::uint64_t sampleEpoch, double seconds, double phaseNs)
{
    if (sampleEpoch != epoch)
    {
        epoch = sampleEpoch;
        size = next = 0;
    }
    times[(std::size_t) next] = seconds;
    phases[(std::size_t) next] = phaseNs;
    next = (next + 1) % capacity;
    if (size < capacity)
        ++size;
}

std::optional<double> RateEstimate::ppm() const
{
    if (size < 3)
        return std::nullopt;
    double mt = 0, mp = 0;
    for (int i = 0; i < size; ++i)
    {
        mt += times[(std::size_t) i];
        mp += phases[(std::size_t) i];
    }
    mt /= size;
    mp /= size;
    double stt = 0, stp = 0;
    for (int i = 0; i < size; ++i)
    {
        const double dt = times[(std::size_t) i] - mt;
        stt += dt * dt;
        stp += dt * (phases[(std::size_t) i] - mp);
    }
    if (stt <= 0.0)
        return std::nullopt;
    return stp / stt / 1000.0;   // ns per s -> ppm
}
}
