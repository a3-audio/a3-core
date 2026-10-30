// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The clock role's Linux side, as far as a test may go: the profiler feed with a stand-in command, and a
// PTP clock that is not there. Nothing here opens /dev/ptp0.
#include <gtest/gtest.h>

#include <thread>

#include "ClockRole.h"
#include "Platform.h"

namespace
{
std::vector<std::string> drainUntilEnded (a3app::ProfilerProcess& feed)
{
    std::vector<std::string> lines;
    for (int i = 0; i < 200; ++i)
    {
        const bool alive = feed.drain ([&] (std::string_view line) { lines.emplace_back (line); });
        if (! alive)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return lines;
}
}

TEST (ClockRole, TheFeedHandsOverWholeLinesAndSaysWhenItEnded)
{
    const auto platform = a3core::makePlatform();
    a3app::ProfilerProcess feed (*platform, { "sh", "-c", "printf 'one\\ntwo\\nthr'; sleep 0.1; printf 'ee\\n'" });
    ASSERT_TRUE (feed.start());
    EXPECT_EQ (drainUntilEnded (feed), (std::vector<std::string> { "one", "two", "three" }));
}

TEST (ClockRole, TheFeedStartsAgainAfterItEnded)
{
    const auto platform = a3core::makePlatform();
    a3app::ProfilerProcess feed (*platform, { "sh", "-c", "echo again" });
    ASSERT_TRUE (feed.start());
    EXPECT_EQ (drainUntilEnded (feed).size(), 1u);
    ASSERT_TRUE (feed.start());
    EXPECT_EQ (drainUntilEnded (feed).size(), 1u);
}

TEST (ClockRole, TheProfilerIsPwProfilerAsJson)
{
    EXPECT_EQ (a3app::profilerCommand(), (std::vector<std::string> { "pw-profiler", "-J" }));
}

TEST (ClockRole, AMissingPtpClockIsSaidNotACrash)
{
    a3app::LinuxPhc phc ("/nonexistent/ptp9");
    EXPECT_FALSE (phc.read());
    EXPECT_FALSE (phc.frequencyPpm());
    EXPECT_FALSE (phc.setFrequencyPpm (1.0));
    EXPECT_NE (phc.problem().find ("/nonexistent/ptp9"), std::string::npos);
}

TEST (ClockRole, ScaledPpmIsTheKernelsUnit)
{
    EXPECT_EQ (a3app::scaledPpm (14.1), 924058);   // 14.1 * 65536, rounded
    EXPECT_DOUBLE_EQ (a3app::ppmOfScaled (-65536), -1.0);
}

namespace
{
struct TempDir
{
    juce::File dir = juce::File::createTempFile ("a3-clock-role");
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};
}

// The PHC is the audio port's, found the way PipeWire's clock.interface finds it.
TEST (ClockRole, ThePhcIsTheAudioPortsFromSysfs)
{
    TempDir sys;
    sys.dir.getChildFile ("enp5s0/device/ptp/ptp1").createDirectory();
    sys.dir.getChildFile ("eno1/device/ptp/ptp0").createDirectory();
    EXPECT_EQ (a3app::phcOfInterface (sys.dir, "enp5s0"), std::optional<std::string> ("/dev/ptp1"));
    EXPECT_EQ (a3app::phcOfInterface (sys.dir, "eno1"), std::optional<std::string> ("/dev/ptp0"));
    EXPECT_FALSE (a3app::phcOfInterface (sys.dir, "wlan0"));
}

TEST (ClockRole, ItRefusesAPhcThatIsNotTheAudioPorts)
{
    EXPECT_EQ (a3app::phcRefusal ("enp5s0", "/dev/ptp1", ""), "");
    EXPECT_EQ (a3app::phcRefusal ("enp5s0", "/dev/ptp1", "/dev/ptp1"), "");
    EXPECT_NE (a3app::phcRefusal ("enp5s0", "/dev/ptp1", "/dev/ptp0").find ("is not enp5s0's PTP clock"), std::string::npos);
    EXPECT_NE (a3app::phcRefusal ("", std::nullopt, "").find ("no audio port"), std::string::npos);
    EXPECT_NE (a3app::phcRefusal ("enp5s0", std::nullopt, "").find ("enp5s0 has no PTP hardware clock"), std::string::npos);
}

TEST (ClockRole, ReadsThePortStateFromPmc)
{
    const std::string answer = "sending: GET PORT_DATA_SET\n"
                               "\ta0369f.fffe.1d2e3f-1 seq 0 RESPONSE MANAGEMENT PORT_DATA_SET \n"
                               "\t\tportIdentity            a0369f.fffe.1d2e3f-1\n"
                               "\t\tportState               MASTER\n"
                               "\t\tlogMinDelayReqInterval  0\n";
    EXPECT_EQ (a3app::portStateOf (answer), std::optional<std::string> ("MASTER"));
    EXPECT_FALSE (a3app::portStateOf ("sending: GET PORT_DATA_SET\n"));   // nobody answered
}

TEST (ClockRole, TheLastLockedFrequencyIsKept)
{
    TempDir state;
    const auto file = state.dir.getChildFile ("a3-core/clock.json");
    EXPECT_FALSE (a3app::rememberedFrequency (file));
    ASSERT_TRUE (a3app::rememberFrequency (file, 14.1023));
    ASSERT_TRUE (a3app::rememberedFrequency (file));
    EXPECT_DOUBLE_EQ (*a3app::rememberedFrequency (file), 14.1023);
    file.replaceWithText ("{ broken");
    EXPECT_FALSE (a3app::rememberedFrequency (file));
}

TEST (ClockRole, OnlyOneInstanceSteersAClock)
{
    TempDir dir;
    const auto device = dir.dir.getChildFile ("ptp-stand-in");
    device.replaceWithText ("");
    a3app::LinuxPhc first (device.getFullPathName().toStdString());
    a3app::LinuxPhc second (device.getFullPathName().toStdString());
    EXPECT_TRUE (first.claim());
    EXPECT_FALSE (second.claim());
    EXPECT_NE (second.problem().find ("another clock role"), std::string::npos);
}

// ptp4l@'s unit runs with ProtectHome on the nucs: its reply never reaches a socket under /run/user. The
// pmc client's socket lives under /tmp (ptp4l's PrivateTmp is off by a3-core's drop-in), one per query.
TEST (ClockRole, ThePmcClientSocketIsUnderTmpAndNewEachQuery)
{
    const auto first = a3app::pmcClientSocket (4242, 1), second = a3app::pmcClientSocket (4242, 2);
    EXPECT_EQ (first.getParentDirectory().getFullPathName(), "/tmp");
    EXPECT_TRUE (first.getFileName().startsWith ("a3-clock-pmc."));
    EXPECT_TRUE (first.getFileName().contains ("4242"));
    EXPECT_NE (first, second);
}

// ptp4l's unit has no CAP_DAC_OVERRIDE: root as it is, it may only send to a socket that others may
// write. pmc binds with the umask it is started with, so it is started with umask 000.
TEST (ClockRole, PmcIsAskedWithASocketPtp4lMaySendTo)
{
    const auto argv = a3app::pmcCommand (juce::File ("/tmp/a3-clock-pmc.1.1"));
    ASSERT_GE (argv.size(), 4u);
    EXPECT_EQ (argv[0], "sh");
    EXPECT_EQ (argv[1], "-c");
    EXPECT_EQ (argv[2], "umask 000 && exec pmc \"$@\"");
    const std::vector<std::string> rest (argv.begin() + 3, argv.end());
    EXPECT_EQ (rest, (std::vector<std::string> { "pmc", "-u", "-b", "0", "-i", "/tmp/a3-clock-pmc.1.1", "-s", "/var/run/ptp4lro",
                                                 "GET PORT_DATA_SET" }));
}
