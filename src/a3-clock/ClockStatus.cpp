// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockStatus.h"

#include <cstdio>

namespace a3clock
{
namespace
{
const Health allHealths[] = { Health::acquiring, Health::locked, Health::holdover, Health::fault, Health::observing };

std::optional<double> number (const juce::var& v)
{
    if (! (v.isInt() || v.isInt64() || v.isDouble()))
        return std::nullopt;
    return (double) v;
}

juce::String text (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

juce::var countersJson (const ClockStatus& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("updates", (juce::int64) s.counters.updates);
    o->setProperty ("corrections", (juce::int64) s.counters.corrections);
    o->setProperty ("reanchors", (juce::int64) s.counters.reanchors);
    o->setProperty ("phcSteps", (juce::int64) s.counters.phcSteps);
    o->setProperty ("holdovers", (juce::int64) s.counters.holdovers);
    o->setProperty ("lockLosses", (juce::int64) s.counters.lockLosses);
    o->setProperty ("scarlettBreaks", (juce::int64) s.scarlettBreaks);
    o->setProperty ("feedRestarts", (juce::int64) s.feedRestarts);
    return juce::var (o);
}

std::int64_t count (const juce::var& counters, const char* name) { return (std::int64_t) number (counters[name]).value_or (0.0); }
}

std::optional<Health> healthFromWord (const juce::String& word)
{
    for (const auto health : allHealths)
        if (word == healthWord (health))
            return health;
    return std::nullopt;
}

juce::var toJson (const ClockStatus& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("master", text (s.master));
    o->setProperty ("health", healthWord (s.health));
    o->setProperty ("reason", text (s.reason));
    o->setProperty ("device", text (s.device));
    o->setProperty ("observeOnly", s.observeOnly);
    o->setProperty ("frequencyPpm", s.frequencyPpm);
    o->setProperty ("errorNs", s.errorNs);
    o->setProperty ("ratePpm", s.ratePpm ? juce::var (*s.ratePpm) : juce::var());
    o->setProperty ("lockedSeconds", s.lockedSeconds);
    o->setProperty ("counters", countersJson (s));
    o->setProperty ("writtenAtUnix", s.writtenAtUnix);
    return juce::var (o);
}

std::optional<ClockStatus> clockStatusFromJson (const juce::var& v)
{
    const auto health = healthFromWord (v["health"].toString());
    const auto frequency = number (v["frequencyPpm"]), error = number (v["errorNs"]), written = number (v["writtenAtUnix"]);
    if (! v.isObject() || ! health || ! frequency || ! error || ! written)
        return std::nullopt;
    ClockStatus s;
    s.master = v["master"].toString().toStdString();
    s.health = *health;
    s.reason = v["reason"].toString().toStdString();
    s.device = v["device"].toString().toStdString();
    s.observeOnly = (bool) v["observeOnly"];
    s.frequencyPpm = *frequency;
    s.errorNs = *error;
    s.ratePpm = number (v["ratePpm"]);
    s.lockedSeconds = number (v["lockedSeconds"]).value_or (0.0);
    const auto& counters = v["counters"];
    s.counters.updates = count (counters, "updates");
    s.counters.corrections = count (counters, "corrections");
    s.counters.reanchors = count (counters, "reanchors");
    s.counters.phcSteps = count (counters, "phcSteps");
    s.counters.holdovers = count (counters, "holdovers");
    s.counters.lockLosses = count (counters, "lockLosses");
    s.scarlettBreaks = count (counters, "scarlettBreaks");
    s.feedRestarts = count (counters, "feedRestarts");
    s.writtenAtUnix = *written;
    return s;
}

juce::File clockStatusFile (const std::optional<std::string>& runtimeDirectory, unsigned uid)
{
    const auto base = runtimeDirectory && ! runtimeDirectory->empty() ? text (*runtimeDirectory) : "/run/user/" + juce::String (uid);
    return juce::File (base).getChildFile ("a3/clock.json");
}

bool writeClockStatus (const juce::File& file, const ClockStatus& status)
{
    if (! file.getParentDirectory().createDirectory())
        return false;
    const auto temporary = file.getSiblingFile (file.getFileName() + ".new");
    return temporary.replaceWithText (juce::JSON::toString (toJson (status), true)) && temporary.moveFileTo (file);
}

ClockReport readClockReport (const juce::File& file, double nowUnix)
{
    ClockReport report;
    if (! file.existsAsFile())
    {
        report.why = "the clock role is not running (a3-clock.service): nothing steers the PTP clock";
        return report;
    }
    const auto status = clockStatusFromJson (juce::JSON::parse (file.loadFileAsString()));
    if (! status)
    {
        report.why = "the clock role's status is unreadable: " + file.getFullPathName().toStdString();
        return report;
    }
    report.status = *status;
    const double age = nowUnix - status->writtenAtUnix;
    if (age > clockStaleSeconds)
    {
        char line[160];
        std::snprintf (line, sizeof line, "the clock role stopped reporting %.0f s ago (a3-clock.service)", age);
        report.why = line;
        return report;
    }
    report.running = true;
    return report;
}

juce::var toJson (const ClockReport& report)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("running", report.running);
    o->setProperty ("why", text (report.why));
    o->setProperty ("status", report.running ? toJson (report.status) : juce::var());
    return juce::var (o);
}

ClockReport clockReportFromJson (const juce::var& v)
{
    ClockReport report;
    if (! v.isObject())
    {
        report.why = "the engine does not report the clock (an engine from before the clock role)";
        return report;
    }
    report.why = v["why"].toString().toStdString();
    if (! (bool) v["running"])
        return report;
    if (const auto status = clockStatusFromJson (v["status"]))
    {
        report.running = true;
        report.status = *status;
        return report;
    }
    report.why = "the engine's clock report is unreadable";
    return report;
}

ClockReportReader::ClockReportReader (juce::File statusFile, double periodSeconds) : file (std::move (statusFile)), period (periodSeconds) {}

const ClockReport& ClockReportReader::get (double nowSeconds, double nowUnix)
{
    if (! readAt || nowSeconds - *readAt >= period)
    {
        last = readClockReport (file, nowUnix);
        readAt = nowSeconds;
    }
    return last;
}

std::string logLine (const ClockStatus& s)
{
    char rate[32];
    if (s.ratePpm)
        std::snprintf (rate, sizeof rate, "%+.3f", *s.ratePpm);
    else
        std::snprintf (rate, sizeof rate, "-");
    char line[400];
    std::snprintf (line, sizeof line,
                   "a3-core clock: %s error %+.1f us freq %+.3f ppm rate %s ppm corrections %lld reanchors %lld steps %lld "
                   "holdovers %lld losses %lld breaks %lld feed-restarts %lld",
                   healthWord (s.health), s.errorNs / 1000.0, s.frequencyPpm, rate, (long long) s.counters.corrections,
                   (long long) s.counters.reanchors, (long long) s.counters.phcSteps, (long long) s.counters.holdovers,
                   (long long) s.counters.lockLosses, (long long) s.scarlettBreaks, (long long) s.feedRestarts);
    return line;
}
}
