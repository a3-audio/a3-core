// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "AppArgs.h"
#include "AppTruth.h"
#include "EnginePorts.h"
#include "SpatialTestData.h"

namespace
{
std::optional<a3app::AppArgs> parse (std::initializer_list<const char*> words, juce::String& error)
{
    juce::StringArray list;
    for (auto* w : words)
        list.add (w);
    return a3app::parseAppArgs (list, error);
}

struct Home
{
    Home() : dir (juce::File::createTempFile ("a3-app-home")) { dir.createDirectory(); }
    ~Home() { dir.deleteRecursively(); }
    juce::File write (const juce::String& name, const juce::String& text) const
    {
        auto file = dir.getChildFile (name);
        file.replaceWithText (text);
        return file;
    }
    juce::String listing() const
    {
        juce::String all;
        for (const auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories, true))
            all += f.getFullPathName() + "|" + juce::String (f.getSize()) + "|" + f.getLastModificationTime().toString (true, true, true) + "\n";
        return all;
    }
    juce::File dir;
};

a3app::AppArgs checkArgs (const juce::File& config)
{
    a3app::AppArgs args;
    args.checkConfig = true;
    args.config = config.getFullPathName();
    args.truth = A3_TRUTH_COPY;
    return args;
}

juce::String spatialField()
{
    return "\"spatial\":" + juce::JSON::toString (juce::var (a3test::shippedSpatialDirectory.getFullPathName()));
}
}

TEST (AppArgs, Defaults)
{
    juce::String error;
    const auto args = parse ({}, error);
    ASSERT_TRUE (args.has_value()) << error;
    EXPECT_FALSE (args->noWindow || args->noConnect || args->service || args->timing || args->checkConfig);
    EXPECT_TRUE (args->config.isEmpty());
    EXPECT_TRUE (args->spatial.isEmpty());
    EXPECT_EQ (args->truth, "/usr/share/a3/a3-osc.json");
}

TEST (AppArgs, ValuesAreRead)
{
    juce::String error;
    const auto args = parse ({ "--config", "/x/e.json", "--spatial", "/x/sp", "--truth", "/x/t.json", "--no-connect", "--timing" }, error);
    ASSERT_TRUE (args.has_value()) << error;
    EXPECT_EQ (args->config, "/x/e.json");
    EXPECT_EQ (args->spatial, "/x/sp");
    EXPECT_EQ (args->truth, "/x/t.json");
    EXPECT_TRUE (args->noConnect && args->timing);
}

TEST (AppArgs, UnknownFlagIsRefused)
{
    juce::String error;
    EXPECT_FALSE (parse ({ "--no-window", "--bogus" }, error).has_value());
    EXPECT_TRUE (error.contains ("--bogus")) << error;
    error.clear();
    EXPECT_FALSE (parse ({ "--config" }, error).has_value());   // a value flag without its value
    EXPECT_TRUE (error.contains ("--config")) << error;
}

TEST (AppArgs, ServiceAndNoWindowCombine)
{
    juce::String error;
    const auto args = parse ({ "--service", "--no-window" }, error);
    ASSERT_TRUE (args.has_value()) << error;
    EXPECT_TRUE (args->service && args->noWindow);
}

TEST (AppArgs, CheckConfigOkForTheShippedData)
{
    Home home;
    const auto config = home.write ("engine.json", "{" + spatialField() + "}");
    juce::String message;
    EXPECT_TRUE (a3app::checkConfig (checkArgs (config), home.dir, message)) << message;
    EXPECT_EQ (message, "ok");
}

TEST (AppArgs, CheckConfigPassesWithRetiredDeckPorts)
{
    Home home;
    const auto config = home.write ("old.json", "{\"engine\":{\"decks\":2},\"io\":{\"jack\":{\"deckA_cue_L\":[\"x:y\"],\"deckB_aux_R\":[\"p:q\"]}},"
                                    + spatialField() + "}");
    juce::String message;
    EXPECT_TRUE (a3app::checkConfig (checkArgs (config), home.dir, message)) << message;
    EXPECT_TRUE (message.contains ("dropped retired link deckA_cue_L -> x:y")) << message;
    EXPECT_TRUE (message.contains ("dropped retired link deckB_aux_R -> p:q")) << message;
    EXPECT_TRUE (message.endsWith ("ok")) << message;
}

// A8: the deck count comes from one place. A config that names it keeps it (the rig's "decks": 2 stays 2,
// whatever the truth says); one that names none takes the truth's deck range, 4 or more.
TEST (AppArgs, TheDeckCountIsTheConfigsElseTheTruths)
{
    Home home;
    juce::String error;
    const auto named = home.write ("named.json", "{\"engine\":{\"decks\":2}," + spatialField() + "}");
    const auto unnamed = home.write ("unnamed.json", "{" + spatialField() + "}");
    const auto wideTruth = home.write ("truth-6.json", a3apptest::truthWithDeckRange (6));

    const auto rig = a3app::loadStartupData (checkArgs (named), home.dir, error);
    ASSERT_TRUE (rig.has_value()) << error;
    EXPECT_EQ (rig->config.engine.decks, 2);
    EXPECT_TRUE (rig->config.engine.decksNamed);

    const auto four = a3app::loadStartupData (checkArgs (unnamed), home.dir, error);
    ASSERT_TRUE (four.has_value()) << error;
    EXPECT_EQ (four->config.engine.decks, 4);
    EXPECT_FALSE (four->config.engine.decksNamed);
    EXPECT_EQ (a3app::enginePortNames (four->config.engine, four->spatial).inputs.size(), 176u);   // 12 + 4 x 41

    auto args = checkArgs (unnamed);
    args.truth = wideTruth.getFullPathName();
    const auto six = a3app::loadStartupData (args, home.dir, error);
    ASSERT_TRUE (six.has_value()) << error;
    EXPECT_EQ (six->config.engine.decks, 6);   // no cap
    args = checkArgs (named);
    args.truth = wideTruth.getFullPathName();
    EXPECT_EQ (a3app::loadStartupData (args, home.dir, error)->config.engine.decks, 2);   // never raised
    EXPECT_EQ (named.loadFileAsString(), "{\"engine\":{\"decks\":2}," + spatialField() + "}");   // never rewritten
}

// The rig's saved "decks": 2 under a truth of 4: --check-config names the hidden slots once and still passes.
TEST (AppArgs, CheckConfigSaysWhichDecksTheSavedCountHides)
{
    Home home;
    const auto named = home.write ("named.json", "{\"engine\":{\"decks\":2}," + spatialField() + "}");
    juce::String message;
    ASSERT_TRUE (a3app::checkConfig (checkArgs (named), home.dir, message)) << message;
    EXPECT_TRUE (message.contains ("decks C, D have no slot")) << message;
    EXPECT_EQ (message.indexOf ("no slot"), message.lastIndexOf ("no slot")) << message;
    EXPECT_TRUE (message.endsWith ("ok")) << message;
}

// A slot beyond the truth's /vu deck meters (deck_a ... deck_d) has none, and --check-config says so once.
TEST (AppArgs, CheckConfigSaysWhichDeckHasNoMeter)
{
    Home home;
    auto args = checkArgs (home.write ("unnamed.json", "{" + spatialField() + "}"));
    args.truth = home.write ("truth-5.json", a3apptest::truthWithDeckRange (5)).getFullPathName();
    juce::String message;
    ASSERT_TRUE (a3app::checkConfig (args, home.dir, message)) << message;
    EXPECT_TRUE (message.contains ("deck E")) << message;
    EXPECT_TRUE (message.endsWith ("ok")) << message;
    EXPECT_EQ (message.indexOf ("deck E"), message.lastIndexOf ("deck E")) << message;   // once

    ASSERT_TRUE (a3app::checkConfig (checkArgs (home.write ("four.json", "{" + spatialField() + "}")), home.dir, message)) << message;
    EXPECT_EQ (message, "ok");
}

// R15: a saved spatial directory whose fx the engine overrides (REAPER's warp, its pitch) and an
// engine.json modulating what the engine owns: --check-config says so once and still passes.
TEST (AppArgs, CheckConfigSaysWhatTheDelayOverrides)
{
    Home home;
    const auto booth = juce::JSON::toString (juce::var (a3test::reaperBoothSpatialDirectory.getFullPathName()));
    const auto config = home.write ("reaper.json", "{\"engine\":{\"delayModulation\":[{\"parameter\":\"pitchL\",\"base\":0,"
                                                   "\"depth\":18,\"rateHz\":0.1,\"shape\":\"random\"}]},\"spatial\":" + booth + "}");
    juce::String message;
    ASSERT_TRUE (a3app::checkConfig (checkArgs (config), home.dir, message)) << message;
    EXPECT_TRUE (message.contains ("transformModeL 1")) << message;
    EXPECT_TRUE (message.contains ("delayModulation")) << message;
    EXPECT_EQ (message.indexOf ("transformModeL"), message.lastIndexOf ("transformModeL")) << message;   // once
    EXPECT_TRUE (message.endsWith ("ok")) << message;
}

TEST (AppArgs, CheckConfigNamesFileAndReason)
{
    Home home;
    juce::String message;

    const auto broken = home.write ("broken.json", "{ \"engine\": ");
    EXPECT_FALSE (a3app::checkConfig (checkArgs (broken), home.dir, message));
    EXPECT_TRUE (message.startsWith (broken.getFullPathName())) << message;

    const auto noChannels = home.write ("n0.json", "{\"engine\":{\"channels\":0}," + spatialField() + "}");
    EXPECT_FALSE (a3app::checkConfig (checkArgs (noChannels), home.dir, message));
    EXPECT_TRUE (message.startsWith (noChannels.getFullPathName()) && message.contains ("channels")) << message;

    const auto nowhere = home.dir.getChildFile ("nowhere");
    const auto noSpatial = home.write ("nospatial.json", "{\"spatial\":" + juce::JSON::toString (juce::var (nowhere.getFullPathName())) + "}");
    EXPECT_FALSE (a3app::checkConfig (checkArgs (noSpatial), home.dir, message));
    EXPECT_TRUE (message.startsWith (noSpatial.getFullPathName()) && message.contains (nowhere.getFullPathName())) << message;

    auto badOverride = checkArgs (home.write ("ok2.json", "{" + spatialField() + "}"));
    badOverride.spatial = nowhere.getFullPathName();
    EXPECT_FALSE (a3app::checkConfig (badOverride, home.dir, message));
    EXPECT_TRUE (message.startsWith (nowhere.getFullPathName())) << message;

    auto badTruth = checkArgs (home.write ("ok.json", "{" + spatialField() + "}"));
    badTruth.truth = home.write ("truth.json", "not json").getFullPathName();
    EXPECT_FALSE (a3app::checkConfig (badTruth, home.dir, message));
    EXPECT_TRUE (message.startsWith (badTruth.truth)) << message;
}


TEST (AppArgs, SpatialArgumentReplacesTheConfigsDirectory)
{
    Home home;
    const auto config = home.write ("engine.json", "{" + spatialField() + "}");
    auto args = checkArgs (config);
    juce::String message;
    args.spatial = a3test::shippedSpatialDirectory.getParentDirectory().getChildFile ("spatial-reaper-booth-6.1").getFullPathName();
    const auto loaded = a3app::loadStartupData (args, home.dir, message);
    ASSERT_TRUE (loaded.has_value()) << message;
    EXPECT_EQ (loaded->config.spatialDirectory.getFullPathName(), args.spatial);
}

TEST (AppArgs, CheckConfigOpensNothing)
{
    // The check is pure reading: the home and the config file stay byte- and time-identical,
    // and no file appears. (It links no audio code: the app lib has no JACK, no device, no socket.)
    Home home;
    const auto config = home.write ("engine.json", "{" + spatialField() + "}");
    const auto before = home.listing();
    juce::String message;
    ASSERT_TRUE (a3app::checkConfig (checkArgs (config), home.dir, message)) << message;
    EXPECT_EQ (home.listing(), before);
}

TEST (AppArgs, SaveOnAFreshHomeWritesTheUserFile)
{
    // The rig after the install: no user file, the package's file (spatial relative to itself) is read.
    Home home;
    const auto package = home.dir.getChildFile ("usr/share/a3-engine/engine.json");
    ASSERT_TRUE (package.getParentDirectory().createDirectory());
    ASSERT_TRUE (juce::File (A3_DEFAULT_ENGINE_CONFIG).copyFileTo (package));
    ASSERT_TRUE (a3test::shippedSpatialDirectory.copyDirectoryTo (package.getSiblingFile ("spatial")));
    const auto packageText = package.loadFileAsString();

    a3app::AppArgs args;
    args.truth = A3_TRUTH_COPY;
    args.packageConfig = package;
    juce::String error;
    const auto data = a3app::loadStartupData (args, home.dir, error);
    ASSERT_TRUE (data.has_value()) << error;
    EXPECT_EQ (data->configFile, package);

    const auto user = home.dir.getChildFile (".config/a3/engine/engine.json");
    EXPECT_EQ (data->saveFile, user);
    ASSERT_TRUE (a3app::saveAppConfig (data->config, data->saveFile).wasOk());
    EXPECT_EQ (package.loadFileAsString(), packageText);

    // The saved file is the one the next start reads, and it passes the unit's check.
    const auto again = a3app::loadStartupData (args, home.dir, error);
    ASSERT_TRUE (again.has_value()) << error;
    EXPECT_EQ (again->configFile, user);
    EXPECT_EQ (again->config.spatialDirectory, package.getSiblingFile ("spatial"));
    EXPECT_TRUE (a3app::checkConfig (args, home.dir, error)) << error;
}

TEST (AppArgs, SaveWritesTheFileConfigNamed)
{
    Home home;
    const auto config = home.write ("mine.json", "{" + spatialField() + "}");
    juce::String error;
    const auto data = a3app::loadStartupData (checkArgs (config), home.dir, error);
    ASSERT_TRUE (data.has_value()) << error;
    EXPECT_EQ (data->saveFile, config);
}

TEST (AppArgs, CheckConfigRefusesWhatTheEngineRefuses)
{
    // The loaders check only the shape of the spatial files; IEM refuses an unknown parameter id.
    Home home;
    a3test::ScratchDirectory spatial;
    const auto fx = spatial.root().getChildFile ("fx.parameters.json");
    auto parameters = juce::JSON::parse (fx.loadFileAsString());
    ASSERT_NE (parameters.getDynamicObject(), nullptr);
    parameters.getDynamicObject()->setProperty ("noSuchParameter", 1.0);
    ASSERT_TRUE (fx.replaceWithText (juce::JSON::toString (parameters)));

    const auto config = home.write ("engine.json", "{\"spatial\":" + juce::JSON::toString (juce::var (spatial.root().getFullPathName())) + "}");
    juce::String message;
    EXPECT_FALSE (a3app::checkConfig (checkArgs (config), home.dir, message)) << message;
    EXPECT_TRUE (message.startsWith (config.getFullPathName()) && message.contains ("fx.parameters.json")) << message;
}
