// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockDaemon.h"

#include <algorithm>
#include <cmath>

namespace a3clock
{
namespace
{
const char* const writeRefused = "the PTP clock refused a frequency write: steering stopped, the last frequency is held";
const char* const frequencyUnknown = "the PTP clock's frequency cannot be read: nothing is steered";
constexpr double bootFrequencyPpm = 0.001;   // below this the PHC was reset (a boot or a driver reset)
}

ClockDaemon::ClockDaemon (PhcDevice& device, ProfilerFeed& profiler, PtpPortState* portState, DaemonOptions chosen, StatusSink statusSink,
                          Journal lines, Remember rememberLocked, double nowSeconds)
    : phc (device), feed (profiler), port (portState), options (std::move (chosen)), sink (std::move (statusSink)),
      journal (std::move (lines)), remember (std::move (rememberLocked)), intake (options.intake),
      nextUpdate (nowSeconds + options.steering.updateSeconds), nextLog (nowSeconds + options.logSeconds), nextPortCheck (nowSeconds),
      lastScarlettRecord (nowSeconds)
{
    current.device = options.device;
    current.interface = options.interface;
    current.observeOnly = options.observeOnly;
    current.frequencyPpm = phc.frequencyPpm().value_or (0.0);
    if (! options.refusal.empty())
    {
        current.health = lastHealth = Health::fault;
        current.reason = options.refusal;
        say ("a3-core clock: not steering: " + options.refusal);
        return;   // nothing is measured or written; the status says why
    }
    if (options.observeOnly)
    {
        current.health = lastHealth = Health::observing;
        current.reason = "observing: measuring, never writing the PTP clock";
    }
    else
        startSteering (nowSeconds);
    startFeed (nowSeconds);
}

void ClockDaemon::startSteering (double nowSeconds)
{
    auto config = options.steering;
    if (const auto limit = phc.maxAdjustmentPpm())
        config.frequencyRangePpm = std::max (1.0, std::min (config.frequencyRangePpm, *limit - options.rangeMarginPpm));
    const auto found = phc.frequencyPpm();
    // A PHC at 0 was just reset (igb does that at every boot): the last locked frequency is the warm start.
    const auto warm = found && std::abs (*found) < bootFrequencyPpm ? options.warmFrequencyPpm : std::nullopt;
    steering.emplace (config, found.value_or (0.0), nowSeconds, warm);
    if (! found)
    {
        steering->writeFailed (frequencyUnknown);
        say ("a3-core clock: " + options.device + ": " + phc.problem());
    }
    current.health = lastHealth = steering->health();
    current.reason = steering->reason();
    say ("a3-core clock: steering " + options.device + " (" + options.interface + ") from " + std::to_string (current.frequencyPpm)
         + " ppm" + (warm ? ", warm start to " + std::to_string (*warm) + " ppm" : std::string()) + ", range +-"
         + std::to_string (config.frequencyRangePpm) + " ppm");
}

void ClockDaemon::startFeed (double nowSeconds)
{
    lastScarlettRecord = nowSeconds;
    if (feed.start())
    {
        failedStartSaidAt.reset();
        return;
    }
    feedRestartAt = nowSeconds + options.feedRestartSeconds;
    if (! failedStartSaidAt || nowSeconds - *failedStartSaidAt >= options.failedStartLogSeconds)
    {
        say ("a3-core clock: pw-profiler cannot be started; trying again every " + std::to_string ((int) options.feedRestartSeconds) + " s");
        failedStartSaidAt = nowSeconds;
    }
}

void ClockDaemon::line (std::string_view text, double nowSeconds)
{
    if (const auto driver = parseProfilerDriver (text))
    {
        intake.addDriver (*driver);
        if (driver->name.substr (0, options.intake.node.size()) != options.intake.node)
            lastOtherDriver.assign (driver->name);
        return;
    }
    if (const auto record = parseProfilerClock (text))
        if (intake.add (*record) != ScarlettIntake::Taken::ignored)
            lastScarlettRecord = nowSeconds;
}

void ClockDaemon::drainFeed (double nowSeconds)
{
    if (feedRestartAt)
    {
        if (nowSeconds < *feedRestartAt)
            return;
        feedRestartAt.reset();
        ++current.feedRestarts;
        startFeed (nowSeconds);
        if (feedRestartAt)
            return;
    }
    const bool alive = feed.drain ([this, nowSeconds] (std::string_view text) { line (text, nowSeconds); });
    if (! alive)
    {
        say ("a3-core clock: the profiler ended; holding the frequency and starting it again");
        intake.restart();
        feedRestartAt = nowSeconds + options.feedRestartSeconds;
        return;
    }
    if (nowSeconds - lastScarlettRecord < options.feedSilenceSeconds)
        return;
    // Alive but silent about the Scarlett: a hung PipeWire, or the Scarlett not driving.
    say ("a3-core clock: no clock record of " + options.intake.node + " for " + std::to_string ((int) options.feedSilenceSeconds)
         + " s" + (lastOtherDriver.empty() ? std::string() : " (the profiler shows " + lastOtherDriver + ")")
         + "; starting the profiler again");
    intake.restart();
    ++current.feedRestarts;
    startFeed (nowSeconds);
}

void ClockDaemon::checkPort (double nowSeconds)
{
    if (port == nullptr || nowSeconds < nextPortCheck)
        return;
    nextPortCheck = nowSeconds + options.portCheckSeconds;
    const auto state = port->portState();
    const auto word = state.value_or ("");
    if (word != current.portState)
        say ("a3-core clock: ptp4l's port on " + options.interface + " is " + (state ? word : std::string ("not readable (unverified)")));
    current.portState = word;
    portPaused = state && word != "MASTER";
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
    // ptp4l not the grandmaster: the PHC is not what the decks follow, so nothing is steered.
    const auto decision = steering->update (nowSeconds, portPaused ? std::nullopt : measurement);
    if (decision.write && ! phc.setFrequencyPpm (*decision.write))
    {
        steering->writeFailed (writeRefused);
        say ("a3-core clock: " + options.device + ": " + phc.problem());
    }
    current.health = steering->health();
    current.reason = portPaused && current.health != Health::fault ? "ptp4l's port is " + current.portState + ", not MASTER: not steering"
                                                                   : std::string (steering->reason());
    current.frequencyPpm = steering->frequencyPpm();
    current.errorNs = steering->errorNs();
    current.counters = steering->counters();
    if (measurement && steering->tracking())
        rate.add (measurement->epoch, nowSeconds, current.errorNs);
    // Only a lock that has held a while is worth a warm start (the first seconds still settle).
    if (current.health == Health::locked && current.lockedSeconds >= options.rememberSeconds && remember && nowSeconds >= nextRemember)
    {
        remember (current.frequencyPpm);
        nextRemember = nowSeconds + options.rememberSeconds;
    }
}

// Observing: the error is the phase against the epoch's first, the frequency the PHC's own.
void ClockDaemon::observe (double nowSeconds, std::optional<Measurement> measurement)
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
    rate.add (measurement->epoch, nowSeconds, current.errorNs);
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
    if (! options.refusal.empty())
    {
        if (nowSeconds >= nextUpdate)
        {
            nextUpdate = nowSeconds + options.steering.updateSeconds;
            publish (nowSeconds, nowUnix);
        }
        return;
    }
    drainFeed (nowSeconds);
    if (nowSeconds < nextUpdate)
        return;
    nextUpdate += options.steering.updateSeconds;
    if (nextUpdate <= nowSeconds)
        nextUpdate = nowSeconds + options.steering.updateSeconds;   // fell behind: never a burst of updates

    checkPort (nowSeconds);
    const auto measurement = measure();
    if (options.observeOnly)
        observe (nowSeconds, measurement);
    else
        steer (nowSeconds, measurement);
    publish (nowSeconds, nowUnix);
}
}
