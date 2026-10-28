// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <set>
#include <gtest/gtest.h>
#include "ConfigFile.h"
#include "EngineFixture.h"
#include "EnginePorts.h"

namespace
{
struct Scratch
{
    Scratch() : dir (juce::File::createTempFile ("a3-app-config")) { dir.createDirectory(); }
    ~Scratch() { dir.deleteRecursively(); }
    juce::File write (const juce::String& name, const juce::String& text) const
    {
        auto file = dir.getChildFile (name);
        file.getParentDirectory().createDirectory();
        file.replaceWithText (text);
        return file;
    }
    juce::File dir;
};

juce::String withSpatial (const juce::String& body)
{
    const auto spatial = juce::JSON::toString (juce::var (a3test::shippedSpatialDirectory.getFullPathName()));
    return "{" + body + (body.isEmpty() ? "" : ",") + "\"spatial\":" + spatial + "}";
}

a3app::AppConfig sample()
{
    a3app::AppConfig config;
    config.engine = a3test::engineConfig (4, 2);
    config.backend.type = a3app::BackendSettings::Type::device;
    config.backend.deviceType = "ALSA";
    config.backend.inputDevice = "hw:1";
    config.backend.outputDevice = "hw:2";
    config.backend.sampleRate = 48000;
    config.backend.blockSize = 256;
    config.io.jack["main_top_1"] = { "system:playback_1", "other:in" };
    config.io.jack["analog_1"] = { "system:capture_1" };
    config.io.directLinks = { { "system:capture_9", "analyzer:in_1" } };
    config.io.deviceChannels["analog_1"] = 0;
    config.io.deviceChannels["main_top_1"] = 3;
    config.spatialDirectory = a3test::shippedSpatialDirectory;
    return config;
}
}

TEST (ConfigFile, ChoosesArgumentThenHomeThenPackage)
{
    const juce::File home ("/home/x");
    const auto homeFile = home.getChildFile (".config/a3/engine/engine.json");
    const juce::File package ("/usr/share/a3-engine/engine.json");
    const auto everything = [] (const juce::File&) { return true; };
    const auto nothingButPackage = [&] (const juce::File& f) { return f == package; };

    EXPECT_EQ (a3app::chooseConfigFile ("/tmp/mine.json", home, everything), juce::File ("/tmp/mine.json"));
    EXPECT_EQ (a3app::chooseConfigFile ("", home, everything), homeFile);
    EXPECT_EQ (a3app::chooseConfigFile ("", home, nothingButPackage), package);
    EXPECT_EQ (a3app::chooseConfigFile ("", home, [] (const juce::File&) { return false; }), package);
}

TEST (ConfigFile, RoundTrips)
{
    Scratch scratch;
    const auto file = scratch.dir.getChildFile ("engine.json");
    const auto saved = sample();
    ASSERT_TRUE (a3app::saveAppConfig (saved, file).wasOk());

    juce::String error;
    const auto loaded = a3app::loadAppConfig (file, error);
    ASSERT_TRUE (loaded.has_value()) << error;
    EXPECT_EQ (loaded->engine, saved.engine);
    EXPECT_EQ (loaded->backend.type, saved.backend.type);
    EXPECT_EQ (loaded->backend.deviceType, saved.backend.deviceType);
    EXPECT_EQ (loaded->backend.inputDevice, saved.backend.inputDevice);
    EXPECT_EQ (loaded->backend.outputDevice, saved.backend.outputDevice);
    EXPECT_EQ (loaded->backend.sampleRate, 48000);
    EXPECT_EQ (loaded->backend.blockSize, 256);
    EXPECT_EQ (loaded->io.jack, saved.io.jack);
    EXPECT_EQ (loaded->io.directLinks, saved.io.directLinks);
    EXPECT_EQ (loaded->io.deviceChannels, saved.io.deviceChannels);
    EXPECT_EQ (loaded->spatialDirectory, saved.spatialDirectory);
}

TEST (ConfigFile, MissingSectionsTakeTheDefaults)
{
    Scratch scratch;
    juce::String error;
    const auto loaded = a3app::loadAppConfig (scratch.write ("engine.json", withSpatial ("")), error);
    ASSERT_TRUE (loaded.has_value()) << error;
    EXPECT_EQ (loaded->engine, a3::EngineConfig {});
    EXPECT_EQ (loaded->backend.type, a3app::BackendSettings::Type::jack);
    EXPECT_EQ (loaded->backend.blockSize, 128);
    EXPECT_EQ (loaded->backend.sampleRate, 44100);
    EXPECT_TRUE (loaded->io.jack.empty());
}

TEST (ConfigFile, BadJsonIsRefusedWithFileAndReason)
{
    Scratch scratch;
    juce::String error;
    const auto broken = scratch.write ("broken.json", "{ \"engine\": ");
    EXPECT_FALSE (a3app::loadAppConfig (broken, error).has_value());
    EXPECT_TRUE (error.contains (broken.getFullPathName())) << error;
    EXPECT_TRUE (error.containsIgnoreCase ("json")) << error;

    const auto missing = scratch.dir.getChildFile ("nothing.json");
    EXPECT_FALSE (a3app::loadAppConfig (missing, error).has_value());
    EXPECT_TRUE (error.contains (missing.getFullPathName())) << error;

    const auto notObject = scratch.write ("list.json", "[1,2]");
    EXPECT_FALSE (a3app::loadAppConfig (notObject, error).has_value());
    EXPECT_TRUE (error.contains (notObject.getFullPathName())) << error;

    const auto badEngine = scratch.write ("engine-bad.json", withSpatial ("\"engine\":{\"channels\":0}"));
    EXPECT_FALSE (a3app::loadAppConfig (badEngine, error).has_value());
    EXPECT_TRUE (error.contains (badEngine.getFullPathName()) && error.contains ("channels")) << error;

    const auto badSpatial = scratch.write ("spatial-bad.json", "{\"spatial\":\"" + scratch.dir.getFullPathName() + "/nowhere\"}");
    EXPECT_FALSE (a3app::loadAppConfig (badSpatial, error).has_value());
    EXPECT_TRUE (error.contains (badSpatial.getFullPathName())) << error;

    for (const char* section : { "\"backend\":{\"type\":\"pulse\"}", "\"backend\":{\"blockSize\":32}",
                                 "\"backend\":{\"blockSize\":4096}", "\"backend\":{\"sampleRate\":0}",
                                 "\"backend\":{\"mystery\":1}", "\"io\":{\"mystery\":1}", "\"mystery\":1" })
    {
        const auto file = scratch.write ("section.json", withSpatial (section));
        EXPECT_FALSE (a3app::loadAppConfig (file, error).has_value()) << section;
        EXPECT_TRUE (error.contains (file.getFullPathName())) << section << ": " << error;
    }
}

TEST (ConfigFile, MapNamingAnUnknownEnginePortIsRefused)
{
    Scratch scratch;
    juce::String error;
    const auto unknownJack = scratch.write ("a.json", withSpatial ("\"io\":{\"jack\":{\"main_top_9\":[\"x:y\"]}}"));
    EXPECT_FALSE (a3app::loadAppConfig (unknownJack, error).has_value());
    EXPECT_TRUE (error.contains ("main_top_9") && error.contains (unknownJack.getFullPathName())) << error;

    const auto unknownDevice = scratch.write ("b.json", withSpatial ("\"io\":{\"deviceChannels\":{\"nope\":1}}"));
    EXPECT_FALSE (a3app::loadAppConfig (unknownDevice, error).has_value());
    EXPECT_TRUE (error.contains ("nope")) << error;

    // A deck port that the config's deck count does not have.
    const auto deckBeyond = scratch.write ("c.json", withSpatial ("\"engine\":{\"decks\":1},\"io\":{\"jack\":{\"deckB_1\":[\"x:y\"]}}"));
    EXPECT_FALSE (a3app::loadAppConfig (deckBeyond, error).has_value());

    const auto known = scratch.write ("d.json", withSpatial ("\"engine\":{\"decks\":2},\"io\":{\"jack\":{\"deckB_1\":[\"x:y\"]},\"deviceChannels\":{\"rec_L\":0}}"));
    EXPECT_TRUE (a3app::loadAppConfig (known, error).has_value()) << error;

    const auto badChannel = scratch.write ("e.json", withSpatial ("\"io\":{\"deviceChannels\":{\"rec_L\":-1}}"));
    EXPECT_FALSE (a3app::loadAppConfig (badChannel, error).has_value());
    const auto badPeer = scratch.write ("f.json", withSpatial ("\"io\":{\"jack\":{\"rec_L\":[3]}}"));
    EXPECT_FALSE (a3app::loadAppConfig (badPeer, error).has_value());
}

TEST (ConfigFile, RetiredDeckPortsAreDroppedNotRefused)
{
    Scratch scratch;
    juce::String error;
    const auto file = scratch.write ("old.json", withSpatial (
        "\"engine\":{\"decks\":2},\"io\":{\"jack\":{"
        "\"deckA_cue_L\":[\"x:y\"],\"deckB_aux_R\":[\"p:q\"],\"deckA_1\":[\"keep:1\"],\"main_top_1\":[\"system:playback_1\"]},"
        "\"deviceChannels\":{\"deckA_cue_R\":4,\"rec_L\":0}}"));
    const auto config = a3app::loadAppConfig (file, error);
    ASSERT_TRUE (config.has_value()) << error;
    EXPECT_EQ (config->droppedLinks, (std::vector<std::string> { "deckA_cue_L -> x:y", "deckB_aux_R -> p:q", "deckA_cue_R -> device channel 4" }));
    EXPECT_EQ (config->io.jack.count ("deckA_cue_L"), 0u);
    EXPECT_EQ (config->io.jack.count ("deckB_aux_R"), 0u);
    EXPECT_EQ (config->io.jack.at ("deckA_1"), (std::vector<std::string> { "keep:1" }));
    EXPECT_EQ (config->io.jack.at ("main_top_1"), (std::vector<std::string> { "system:playback_1" }));
    EXPECT_EQ (config->io.deviceChannels.count ("deckA_cue_R"), 0u);
    EXPECT_EQ (config->io.deviceChannels.at ("rec_L"), 0);

    const auto clean = scratch.write ("new.json", withSpatial ("\"io\":{\"jack\":{\"main_top_1\":[\"x:y\"]}}"));
    const auto loaded = a3app::loadAppConfig (clean, error);
    ASSERT_TRUE (loaded.has_value()) << error;
    EXPECT_TRUE (loaded->droppedLinks.empty());
}

// P47: a file saved while the analog return existed names engine.returnAnalogPair. It loads, the key
// is ignored and listed beside the dropped links, and a save no longer writes it.
TEST (ConfigFile, TheRetiredReturnPairIsIgnoredNotRefused)
{
    Scratch scratch;
    juce::String error;
    const auto file = scratch.write ("old.json", withSpatial ("\"engine\":{\"decks\":2,\"returnAnalogPair\":6}"));
    const auto config = a3app::loadAppConfig (file, error);
    ASSERT_TRUE (config.has_value()) << error;
    EXPECT_EQ (config->ignoredKeys, (std::vector<std::string> { "engine.returnAnalogPair" }));
    EXPECT_TRUE (a3app::droppedLinkLines (*config).contains ("ignored retired key engine.returnAnalogPair"));
    EXPECT_FALSE (a3app::toJson (*config)["engine"].getDynamicObject()->hasProperty ("returnAnalogPair"));

    const auto clean = scratch.write ("new.json", withSpatial ("\"engine\":{\"decks\":2}"));
    const auto loaded = a3app::loadAppConfig (clean, error);
    ASSERT_TRUE (loaded.has_value()) << error;
    EXPECT_TRUE (loaded->ignoredKeys.empty());
}

TEST (ConfigFile, UnknownNamesAreStillRefused)
{
    Scratch scratch;
    juce::String error;
    const auto file = scratch.write ("bad.json", withSpatial ("\"engine\":{\"decks\":2},\"io\":{\"jack\":{\"deckA_cue_10\":[\"x:y\"]}}"));
    EXPECT_FALSE (a3app::loadAppConfig (file, error).has_value());
    EXPECT_TRUE (error.contains ("deckA_cue_10")) << error;
}

TEST (ConfigFile, TrimsOutsideNinetyToPlusSixAreRefusedNotClamped)
{
    Scratch scratch;
    juce::String error;
    for (const char* trim : { "\"main\":6.5", "\"booth\":-90.5", "\"phones\":100", "\"rec\":-200", "\"main\":1e999" })
    {
        const auto file = scratch.write ("trim.json", withSpatial (juce::String ("\"engine\":{\"trimsDb\":{") + trim + "}}"));
        EXPECT_FALSE (a3app::loadAppConfig (file, error).has_value()) << trim;
        EXPECT_TRUE (error.contains (file.getFullPathName())) << trim << ": " << error;
    }
    const auto edges = scratch.write ("edges.json", withSpatial ("\"engine\":{\"trimsDb\":{\"main\":6,\"booth\":-90,\"phones\":0,\"rec\":-3}}"));
    const auto loaded = a3app::loadAppConfig (edges, error);
    ASSERT_TRUE (loaded.has_value()) << error;
    EXPECT_EQ (*loaded->engine.trims.main, 6.0f);
    EXPECT_EQ (*loaded->engine.trims.booth, -90.0f);
}

TEST (ConfigFile, SaveNeverWritesWithoutBeingAsked)
{
    Scratch scratch;
    const auto savedHome = juce::SystemStats::getEnvironmentVariable ("HOME", {});
    ::setenv ("HOME", scratch.dir.getFullPathName().toRawUTF8(), 1);
    const auto source = scratch.write ("elsewhere/engine.json", withSpatial (""));

    juce::String error;
    EXPECT_TRUE (a3app::loadAppConfig (source, error).has_value()) << error;
    a3app::chooseConfigFile ("", juce::File::getSpecialLocation (juce::File::userHomeDirectory), [] (const juce::File&) { return true; });

    EXPECT_FALSE (scratch.dir.getChildFile (".config").exists());

    // A failed save leaves the old file as it was.
    const auto before = source.loadFileAsString();
    EXPECT_FALSE (a3app::saveAppConfig (sample(), source.getChildFile ("not-a-directory/engine.json")).wasOk());
    EXPECT_EQ (source.loadFileAsString(), before);
    ::setenv ("HOME", savedHome.toRawUTF8(), 1);
}
