// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What the clock role says about itself: the status file the engine reads and serves to the window, and
// the journal line the soak test reads.
#include <gtest/gtest.h>

#include "ClockStatus.h"

using namespace a3clock;

namespace
{
ClockStatus locked()
{
    ClockStatus s;
    s.health = Health::locked;
    s.reason = "the PTP clock follows the Scarlett";
    s.device = "/dev/ptp0";
    s.interface = "enp5s0";
    s.portState = "MASTER";
    s.frequencyPpm = 14.1023;
    s.errorNs = 2'345.0;
    s.ratePpm = 0.012;
    s.lockedSeconds = 3600.0;
    s.counters.updates = 4000;
    s.counters.corrections = 3990;
    s.counters.reanchors = 1;
    s.counters.phcSteps = 0;
    s.counters.holdovers = 2;
    s.counters.lockLosses = 0;
    s.scarlettBreaks = 1;
    s.feedRestarts = 3;
    s.writtenAtUnix = 1'761'800'000.0;
    return s;
}

struct TempFile
{
    juce::File file = juce::File::createTempFile ("a3-clock-status");
    ~TempFile() { file.deleteFile(); }
};
}

TEST (ClockStatus, GoesThroughJsonUnchanged)
{
    const auto back = clockStatusFromJson (toJson (locked()));
    ASSERT_TRUE (back);
    EXPECT_EQ (back->master, "scarlett-ptp");
    EXPECT_EQ (back->health, Health::locked);
    EXPECT_EQ (back->reason, "the PTP clock follows the Scarlett");
    EXPECT_EQ (back->device, "/dev/ptp0");
    EXPECT_EQ (back->interface, "enp5s0");
    EXPECT_EQ (back->portState, "MASTER");
    EXPECT_FALSE (back->observeOnly);
    EXPECT_DOUBLE_EQ (back->frequencyPpm, 14.1023);
    EXPECT_DOUBLE_EQ (back->errorNs, 2'345.0);
    ASSERT_TRUE (back->ratePpm);
    EXPECT_DOUBLE_EQ (*back->ratePpm, 0.012);
    EXPECT_DOUBLE_EQ (back->lockedSeconds, 3600.0);
    EXPECT_EQ (back->counters.updates, 4000);
    EXPECT_EQ (back->counters.corrections, 3990);
    EXPECT_EQ (back->counters.reanchors, 1);
    EXPECT_EQ (back->counters.holdovers, 2);
    EXPECT_EQ (back->scarlettBreaks, 1);
    EXPECT_EQ (back->feedRestarts, 3);
    EXPECT_DOUBLE_EQ (back->writtenAtUnix, 1'761'800'000.0);
}

TEST (ClockStatus, NoRateYetStaysNone)
{
    auto s = locked();
    s.ratePpm.reset();
    EXPECT_FALSE (clockStatusFromJson (toJson (s))->ratePpm);
}

TEST (ClockStatus, AnUnknownHealthIsRefused)
{
    auto json = toJson (locked());
    json.getDynamicObject()->setProperty ("health", "fine");
    EXPECT_FALSE (clockStatusFromJson (json));
    EXPECT_FALSE (clockStatusFromJson (juce::var ("text")));
}

TEST (ClockStatus, TheFileLivesInTheRuntimeDirectory)
{
    EXPECT_EQ (clockStatusFile ("/run/user/1000", 1000).getFullPathName(), "/run/user/1000/a3/clock.json");
    EXPECT_EQ (clockStatusFile (std::nullopt, 1001).getFullPathName(), "/run/user/1001/a3/clock.json");
}

TEST (ClockStatus, AReportOfAFreshFileIsRunning)
{
    TempFile temp;
    ASSERT_TRUE (writeClockStatus (temp.file, locked()));
    const auto report = readClockReport (temp.file, 1'761'800'002.0);
    EXPECT_TRUE (report.running);
    EXPECT_EQ (report.status.health, Health::locked);
}

TEST (ClockStatus, NoFileIsNotRunningAndSaysWhichUnit)
{
    const auto report = readClockReport (juce::File ("/nonexistent/a3/clock.json"), 0.0);
    EXPECT_FALSE (report.running);
    EXPECT_NE (report.why.find ("a3-clock.service"), std::string::npos);
}

TEST (ClockStatus, AStaleFileIsNotRunning)
{
    TempFile temp;
    writeClockStatus (temp.file, locked());
    const auto report = readClockReport (temp.file, 1'761'800'000.0 + clockStaleSeconds + 1.0);
    EXPECT_FALSE (report.running);
    EXPECT_NE (report.why.find ("stopped reporting"), std::string::npos);
}

TEST (ClockStatus, AnUnreadableFileIsNotRunning)
{
    TempFile temp;
    temp.file.replaceWithText ("{ half");
    EXPECT_FALSE (readClockReport (temp.file, 0.0).running);
}

TEST (ClockStatus, AReportGoesThroughJsonToo)
{
    ClockReport report;
    report.running = true;
    report.status = locked();
    const auto back = clockReportFromJson (toJson (report));
    EXPECT_TRUE (back.running);
    EXPECT_EQ (back.status.counters.corrections, 3990);
    ClockReport absent;
    absent.why = "the clock role is not running";
    const auto none = clockReportFromJson (toJson (absent));
    EXPECT_FALSE (none.running);
    EXPECT_EQ (none.why, "the clock role is not running");
    EXPECT_FALSE (clockReportFromJson (juce::var()).running);   // an engine before the clock: says so
}

TEST (ClockStatus, TheJournalLineCarriesWhatTheSoakTestReads)
{
    const auto line = logLine (locked());
    EXPECT_EQ (line,
               "a3-core clock: locked error +2.3 us freq +14.102 ppm rate +0.012 ppm corrections 3990 "
               "reanchors 1 steps 0 holdovers 2 losses 0 breaks 1 feed-restarts 3 port MASTER");
    auto s = locked();
    s.ratePpm.reset();
    EXPECT_NE (logLine (s).find ("rate - ppm"), std::string::npos);
}

TEST (ClockStatus, TheReaderReadsTheFileOnceAPeriod)
{
    TempFile temp;
    auto s = locked();
    writeClockStatus (temp.file, s);
    ClockReportReader reader (temp.file);
    EXPECT_EQ (reader.get (10.0, s.writtenAtUnix).status.counters.corrections, 3990);
    s.counters.corrections = 4000;
    writeClockStatus (temp.file, s);
    EXPECT_EQ (reader.get (10.5, s.writtenAtUnix).status.counters.corrections, 3990);   // not yet
    EXPECT_EQ (reader.get (11.0, s.writtenAtUnix).status.counters.corrections, 4000);
}

TEST (ClockStatus, LockedIsLockedOnlyWithTheGrandmasterKnown)
{
    ClockReport report;
    report.running = true;
    report.status = locked();
    EXPECT_EQ (clockIndicator (report).word, "LOCKED");
    EXPECT_EQ (clockIndicator (report).tone, ClockIndicator::Tone::ok);
    report.status.portState.clear();
    EXPECT_EQ (clockIndicator (report).word, "UNVERIFIED");
    EXPECT_EQ (clockIndicator (report).tone, ClockIndicator::Tone::caution);
    EXPECT_NE (logLine (report.status).find ("port unverified"), std::string::npos);
}

TEST (ClockStatus, EveryOtherStateWarns)
{
    ClockReport report;
    EXPECT_EQ (clockIndicator (report).word, "NOT RUNNING");
    EXPECT_EQ (clockIndicator (report).tone, ClockIndicator::Tone::danger);
    report.running = true;
    report.status = locked();
    report.status.health = Health::fault;
    EXPECT_EQ (clockIndicator (report).tone, ClockIndicator::Tone::danger);
    EXPECT_NE (clockIndicator (report).detail.find ("restart a3-clock.service"), std::string::npos);
    report.status.health = Health::holdover;
    EXPECT_EQ (clockIndicator (report).tone, ClockIndicator::Tone::danger);
    report.status.health = Health::acquiring;
    EXPECT_EQ (clockIndicator (report).tone, ClockIndicator::Tone::caution);
    const auto json = toJson (clockIndicator (report));
    EXPECT_EQ (json["word"].toString(), "ACQUIRING");
    EXPECT_EQ (json["tone"].toString(), "caution");
}
