// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppArgs.h"
#include "DelayModulation.h"
#include "Engine.h"

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

    data.configFile = chooseConfigFile (args.config, home, [] (const juce::File& f) { return f.existsAsFile(); }, args.packageConfig);
    data.saveFile = chooseSaveFile (args.config, home);
    const auto config = loadAppConfig (data.configFile, error, data.truth.decks);   // names the file itself
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
    const auto data = loadStartupData (args, home, message);
    if (! data)
        return false;
    // The loaders check only the files' shape; IEM refuses parameter sets, decoders and encoder
    // layouts only when the engine is built. Built here and dropped: never prepared, no audio.
    juce::String refusal;
    if (a3::Engine::create (data->config.engine, data->spatial, refusal) == nullptr)
    {
        message = data->configFile.getFullPathName() + ": the engine refuses it (spatial "
                  + data->config.spatialDirectory.getFullPathName() + "): " + refusal;
        return false;
    }
    auto lines = droppedLinkLines (data->config);
    if (const auto note = unmeteredDecksNote (data->config.engine.decks, data->truth.vuMeters); note.isNotEmpty())
        lines.add (note);
    if (const auto note = hiddenDecksNote (data->config.engine.decks, data->config.engine.decksNamed, data->truth.decks);
        note.isNotEmpty())
        lines.add (note);
    if (const auto note = a3::delayOverrideNote (data->spatial.fxParameters, data->config.engine.delayModulation);
        note.isNotEmpty())
        lines.add (note);
    lines.add ("ok");
    message = lines.joinIntoString ("\n");
    return true;
}
}
