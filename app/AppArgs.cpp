// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppArgs.h"

namespace a3app
{
std::optional<AppArgs> parseAppArgs (const juce::StringArray& words, juce::String& error)
{
    AppArgs args;
    for (int i = 0; i < words.size(); ++i)
    {
        const auto& word = words[i];
        juce::String* value = word == "--config" ? &args.config
                            : word == "--spatial" ? &args.spatial
                            : word == "--truth" ? &args.truth : nullptr;
        if (value != nullptr)
        {
            if (i + 1 >= words.size())
            {
                error = word + " needs a value";
                return std::nullopt;
            }
            *value = words[++i];
        }
        else if (word == "--no-window")
            args.noWindow = true;
        else if (word == "--no-connect")
            args.noConnect = true;
        else if (word == "--service")
            args.service = true;
        else if (word == "--timing")
            args.timing = true;
        else if (word == "--check-config")
            args.checkConfig = true;
        else
        {
            error = "unknown argument " + word;
            return std::nullopt;
        }
    }
    return args;
}

std::optional<StartupData> loadStartupData (const AppArgs& args, const juce::File& home, juce::String& error)
{
    StartupData data;
    const auto truth = parseEngineTruth (juce::File (args.truth).loadFileAsString(), error);
    if (! truth)
    {
        error = args.truth + ": " + error;
        return std::nullopt;
    }
    data.truth = *truth;

    data.configFile = chooseConfigFile (args.config, home, [] (const juce::File& f) { return f.existsAsFile(); });
    const auto config = loadAppConfig (data.configFile, error);   // names the file itself
    if (! config)
        return std::nullopt;
    data.config = *config;
    if (args.spatial.isNotEmpty())
        data.config.spatialDirectory = juce::File (args.spatial);

    const auto spatial = a3::loadSpatialConfig (data.config.spatialDirectory, error);
    if (! spatial)
    {
        error = data.config.spatialDirectory.getFullPathName() + ": " + error;
        return std::nullopt;
    }
    data.spatial = *spatial;
    return data;
}

bool checkConfig (const AppArgs& args, const juce::File& home, juce::String& message)
{
    juce::String error;
    const bool good = loadStartupData (args, home, error).has_value();
    message = good ? juce::String ("ok") : error;
    return good;
}
}
