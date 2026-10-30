// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockDaemon.h"

#include <cmath>

namespace a3clock
{
namespace
{
const char* const writeRefused = "the PTP clock refused a frequency write: steering stopped, the last frequency is held";
const char* const frequencyUnknown = "the PTP clock's frequency cannot be read: nothing is steered";
}

ClockDaemon::ClockDaemon (PhcDevice& device, ProfilerFeed& profiler, DaemonOptions chosen, StatusSink statusSink, Journal lines,
                          double nowSeconds)
    : phc (device), feed (profiler), options (std::move (chosen)), sink (std::move (statusSink)), journal (std::move (lines)),
      intake (options.intake), nextUpdate (nowSeconds + options.steering.updateSeconds), nextLog (nowSeconds + options.logSeconds)
{
    current.device = options.device;
    current.observeOnly = options.observeOnly;
    const auto found = phc.frequencyPpm();
    current.frequencyPpm = found.value_or (0.0);
    if (options.observeOnly)
    {
        current.health = lastHealth = Health::observing;
        current.reason = "observing: measuring, never writing the PTP clock";
    }
    else
    {
        steering.emplace (options.steering, current.frequencyPpm, nowSeconds);
        if (! found)
        {
            steering->writeFailed (frequencyUnknown);
            say ("a3-core clock: " + options.device + ": " + phc.problem());
        }
        current.health = lastHealth = steering->health();
        current.reason = steering->reason();
    }
    say ("a3-core clock: " + std::string (options.observeOnly ? "observing " : "steering ") + options.device + " from "
         + std::to_string (current.frequencyPpm) + " ppm");
    if (! feed.start())
        feedRestartAt = nowSeconds + options.feedRestartSeconds;
}

void ClockDaemon::drainFeed (double nowSeconds)
{
    if (feedRestartAt)
    {
        if (nowSeconds < *feedRestartAt)
            return;
        feedRestartAt.reset();
        ++current.feedRestarts;
        if (! feed.start())
        {
            feedRestartAt = nowSeconds + options.feedRestartSeconds;
            return;
        }
    }
    const bool alive = feed.drain ([this] (std::string_view line) {
        if (const auto record = parseProfilerClock (line))
            intake.add (*record);
    });
    if (alive)
        return;
    say ("a3-core clock: the profiler ended; holding the frequency and starting it again");
    intake.restart();
    feedRestartAt = nowSeconds + options.feedRestartSeconds;
}

std::optional<Measurement> ClockDaemon::measure()
{
    if (const auto sample = phc.read())
    {
        history.add (*sample);
        phcProblemSaid = false;
    }
    else
    {
        history.clear();   // a gap in the readings: never interpolate across it
        if (! phcProblemSaid)
            say ("a3-core clock: cannot read " + options.device + ": " + phc.problem());
        phcProblemSaid = true;
    }
    const auto point = intake.take();
    if (! point)
        return std::nullopt;
    const auto phcNs = history.phcAt (point->monoNs);
    if (! phcNs)
        return std::nullopt;
    return Measurement { point->epoch, *phcNs - point->scarlettNs };
}

void ClockDaemon::steer (double nowSeconds, std::optional<Measurement> measurement)
{
    const auto decision = steering->update (nowSeconds, measurement);
    if (decision.write && ! phc.setFrequencyPpm (*decision.write))
    {
        steering->writeFailed (writeRefused);
        say ("a3-core clock: " + options.device + ": " + phc.problem());
    }
    current.health = steering->health();
    current.reason = steering->reason();
    current.frequencyPpm = steering->frequencyPpm();
    current.errorNs = steering->errorNs();
    current.counters = steering->counters();
    if (measurement)
        rate.add (measurement->epoch, nowSeconds, current.errorNs);
}

// Observing: the error is the phase against the epoch's first, the frequency the PHC's own.
void ClockDaemon::observe (std::optional<Measurement> measurement)
{
    ++current.counters.updates;
    if (! measurement)
        return;
    if (measurement->epoch != observedEpoch)
    {
        observedEpoch = measurement->epoch;
        observedOffset = measurement->phaseNs;
    }
    current.errorNs = (double) (measurement->phaseNs - observedOffset);
}

void ClockDaemon::publish (double nowSeconds, double nowUnix)
{
    current.ratePpm = rate.ppm();
    current.scarlettBreaks = intake.discontinuities();
    if (current.health == Health::locked)
    {
        if (! lockedSince)
            lockedSince = nowSeconds;
        current.lockedSeconds = nowSeconds - *lockedSince;
    }
    else
    {
        lockedSince.reset();
        current.lockedSeconds = 0.0;
    }
    if (current.health != lastHealth)
        say ("a3-core clock: " + std::string (healthWord (lastHealth)) + " -> " + healthWord (current.health) + " (" + current.reason + ")");
    lastHealth = current.health;
    current.writtenAtUnix = nowUnix;
    if (sink)
        sink (current);
    if (nowSeconds >= nextLog)
    {
        say (logLine (current));
        nextLog += options.logSeconds;
        if (nextLog <= nowSeconds)
            nextLog = nowSeconds + options.logSeconds;
    }
}

void ClockDaemon::tick (double nowSeconds, double nowUnix)
{
    drainFeed (nowSeconds);
    if (nowSeconds < nextUpdate)
        return;
    nextUpdate += options.steering.updateSeconds;
    if (nextUpdate <= nowSeconds)
        nextUpdate = nowSeconds + options.steering.updateSeconds;   // fell behind: never a burst of updates

    const auto measurement = measure();
    if (options.observeOnly)
    {
        observe (measurement);
        if (measurement)
            rate.add (measurement->epoch, nowSeconds, current.errorNs);
    }
    else
        steer (nowSeconds, measurement);
    publish (nowSeconds, nowUnix);
}
}
