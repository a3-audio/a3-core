// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "AppArgs.h"
#include "AppTruth.h"
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
