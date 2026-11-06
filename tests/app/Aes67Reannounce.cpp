// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// A saved layout re-announces the Core's AES67 streams: the config is rendered again and a3-aes67 restarted, through
// a runner that records instead of running. Nothing here touches the machine's own home or its services.
#include <gtest/gtest.h>

#include "Aes67Reannounce.h"
#include "Render.h"

namespace
{
using a3app::Command;
using a3core::StreamSpec;

struct FakeRunner final : a3app::CommandRunner
{
    Result run (const Command& command) override
    {
        ran.push_back (command);
        return { exitCode, {} };
    }
    bool start (const Command& command) override
    {
        started.push_back (command);
        return true;
    }
    std::optional<int> finished() override { return std::nullopt; }
    std::vector<Command> ran, started;
    int exitCode = 0;
};

struct TempHome
{
    juce::File dir = juce::File::createTempFile ("a3-aes67-reannounce");
    TempHome() { dir.createDirectory(); }
    ~TempHome() { dir.deleteRecursively(); }
};

const std::string hostname = "a3nuc1";

const a3core::Truth& packageTruth()
{
    static const auto truth = std::get<a3core::Truth> (a3core::Truth::load (juce::File (A3_PACKAGE_TRUTH), std::nullopt));
    return truth;
}

juce::File packageTemplate() { return juce::File (A3_PACKAGE_SHARE).getChildFile ("pipewire/pipewire-aes67.conf.in"); }

a3::SpatialConfig loadedSpatial (const char* directory)
{
    juce::String error;
    const auto spatial = a3::loadSpatialConfig (juce::File (directory), error);
    EXPECT_TRUE (spatial.has_value()) << error;
    return spatial.value_or (a3::SpatialConfig {});
}

// The package's 4.1, and a 6.1 in both zones (the booth of the engine tests' REAPER copy): two layouts whose
// streams differ in their channels.
std::vector<StreamSpec> fourOneStreams() { return a3core::resolveTxStreams (packageTruth(), loadedSpatial (A3_SPATIAL_DATA), hostname); }
std::vector<StreamSpec> sixOneStreams()
{
    auto spatial = loadedSpatial (A3_SPATIAL_REAPER_BOOTH_DATA);
    spatial.main.layout = spatial.booth.layout;
    return a3core::resolveTxStreams (packageTruth(), spatial, hostname);
}

a3app::Aes67Render renderAt (const juce::File& home) { return { packageTruth(), home, packageTemplate(), hostname }; }

juce::File configIn (const juce::File& home) { return home.getChildFile (a3core::aes67ConfPath); }
juce::File signatureIn (const juce::File& home) { return home.getChildFile (a3core::aes67SignaturePath); }

std::string bytesOf (const juce::File& file)
{
    juce::MemoryBlock block;
    file.loadFileAsData (block);
    return block.toString().toStdString();
}

void writeBytes (const juce::File& file, const std::string& text)
{
    file.getParentDirectory().createDirectory();
    file.replaceWithData (text.data(), text.size());
}

std::string renderedFor (const std::vector<StreamSpec>& streams)
{
    return a3core::aes67Conf (packageTruth(), hostname, packageTemplate(), streams).value_or ("");
}
}

TEST (Aes67Streams, StreamsChangeIffTheirSignatureDoes)
{
    EXPECT_FALSE (a3core::streamsChanged (a3core::streamsSignature (sixOneStreams()), sixOneStreams()));
    EXPECT_TRUE (a3core::streamsChanged (a3core::streamsSignature (fourOneStreams()), sixOneStreams()));
    EXPECT_TRUE (a3core::streamsChanged ("", sixOneStreams()));
}

// The config was rendered for the 4.1; the 6.1 is saved: rendered again for it, a3-aes67 restarted once. The next
// Save of the same layout restarts nothing.
TEST (Aes67Streams, AChangedSignatureRerendersAndRestartsTheSenderUnit)
{
    TempHome home;
    a3core::writeAes67Conf (packageTruth(), home.dir, hostname, packageTemplate(), fourOneStreams());
    FakeRunner runner;

    const auto said = a3app::reannounceStreams (renderAt (home.dir), sixOneStreams(), runner);

    EXPECT_EQ (bytesOf (configIn (home.dir)), renderedFor (sixOneStreams()));
    ASSERT_EQ (runner.ran.size(), 1u);
    EXPECT_EQ (runner.ran[0], (Command { "systemctl", "--user", "restart", "a3-aes67.service" }));
    EXPECT_TRUE (runner.started.empty());
    EXPECT_NE (said.find ("a3-aes67 restarted"), std::string::npos) << said;

    a3app::reannounceStreams (renderAt (home.dir), sixOneStreams(), runner);
    EXPECT_EQ (runner.ran.size(), 1u) << "the same streams again restart nothing";
}

// No render (a line added below the header survives), no restart, nothing said.
TEST (Aes67Streams, AnUnchangedSignatureDoesNothing)
{
    TempHome home;
    a3core::writeAes67Conf (packageTruth(), home.dir, hostname, packageTemplate(), sixOneStreams());
    const auto marked = bytesOf (configIn (home.dir)) + "# marked by the test\n";
    writeBytes (configIn (home.dir), marked);
    FakeRunner runner;

    const auto said = a3app::reannounceStreams (renderAt (home.dir), sixOneStreams(), runner);

    EXPECT_EQ (said, "");
    EXPECT_EQ (bytesOf (configIn (home.dir)), marked);
    EXPECT_TRUE (runner.ran.empty());
    EXPECT_TRUE (runner.started.empty());
}

TEST (Aes67Streams, TheSignatureIsStoredAfterTheRender)
{
    TempHome home;
    FakeRunner runner;

    a3app::reannounceStreams (renderAt (home.dir), sixOneStreams(), runner);

    EXPECT_EQ (bytesOf (signatureIn (home.dir)), a3core::streamsSignature (sixOneStreams()));
    EXPECT_EQ (a3core::renderedAes67Signature (home.dir), a3core::streamsSignature (sixOneStreams()));
    EXPECT_EQ (runner.ran.size(), 1u) << "no config before: the streams were not announced";
}

// The signature file outlives a removed config: alone it says nothing, so the streams count as changed.
TEST (Aes67Streams, ASignatureWithoutItsConfigCountsAsChanged)
{
    TempHome home;
    writeBytes (signatureIn (home.dir), a3core::streamsSignature (sixOneStreams()));
    EXPECT_FALSE (a3core::renderedAes67Signature (home.dir).has_value());
    FakeRunner runner;

    a3app::reannounceStreams (renderAt (home.dir), sixOneStreams(), runner);

    EXPECT_EQ (bytesOf (configIn (home.dir)), renderedFor (sixOneStreams()));
    EXPECT_EQ (runner.ran.size(), 1u);
}

// A config written by hand is kept, whatever signature lies beside it, and its unit is not ours to restart.
TEST (Aes67Streams, AConfigWrittenByHandIsKeptAndNotRestarted)
{
    TempHome home;
    writeBytes (configIn (home.dir), "# mine\n");
    writeBytes (signatureIn (home.dir), a3core::streamsSignature (sixOneStreams()));
    EXPECT_FALSE (a3core::renderedAes67Signature (home.dir).has_value());
    FakeRunner runner;

    const auto said = a3app::reannounceStreams (renderAt (home.dir), sixOneStreams(), runner);

    EXPECT_EQ (bytesOf (configIn (home.dir)), "# mine\n");
    EXPECT_TRUE (runner.ran.empty());
    EXPECT_TRUE (runner.started.empty());
    EXPECT_NE (said.find ("kept"), std::string::npos) << said;
}

TEST (Aes67Streams, AFailedRestartIsSaid)
{
    TempHome home;
    FakeRunner runner;
    runner.exitCode = 5;

    const auto said = a3app::reannounceStreams (renderAt (home.dir), sixOneStreams(), runner);

    EXPECT_EQ (runner.ran.size(), 1u);
    EXPECT_NE (said.find ("a3-aes67 restart failed (exit 5)"), std::string::npos) << said;
}

// A restart that failed (or timed out: -1) leaves no signature, so the next Save of the same layout renders and
// restarts again instead of trusting a config the senders never read.
TEST (Aes67Streams, AFailedRestartIsRetriedOnTheNextSave)
{
    for (const int failedExit : { 1, -1 })
    {
        SCOPED_TRACE ("exit " + std::to_string (failedExit));
        TempHome home;
        FakeRunner runner;
        runner.exitCode = failedExit;

        a3app::reannounceStreams (renderAt (home.dir), sixOneStreams(), runner);
        EXPECT_FALSE (signatureIn (home.dir).exists());

        runner.exitCode = 0;
        writeBytes (configIn (home.dir), bytesOf (configIn (home.dir)) + "# marked by the test\n");
        const auto said = a3app::reannounceStreams (renderAt (home.dir), sixOneStreams(), runner);
        EXPECT_EQ (runner.ran.size(), 2u);
        EXPECT_EQ (bytesOf (configIn (home.dir)), renderedFor (sixOneStreams())) << "rendered again";
        EXPECT_NE (said.find ("a3-aes67 restarted"), std::string::npos) << said;
        EXPECT_EQ (bytesOf (signatureIn (home.dir)), a3core::streamsSignature (sixOneStreams())) << "a restart that worked keeps it";
    }
}
