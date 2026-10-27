// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ConfigFile.h"
#include "OscVocabulary.h"
#include "SpatialConfig.h"
#include <juce_core/juce_core.h>
#include <optional>

namespace a3app
{
struct AppArgs
{
    bool noWindow = false, noConnect = false, service = false, timing = false, checkConfig = false;
    juce::String config, spatial, truth = "/usr/share/a3/a3-osc.json";
    juce::File packageConfig = packageConfigFile;   // not a flag: the tests point it at a copy
};

// Refuses an unknown flag and a value flag without its value, naming it in `error`.
std::optional<AppArgs> parseAppArgs (const juce::StringArray&, juce::String& error);

// Everything the engine reads at start: the truth, engine.json and the spatial data.
struct StartupData
{
    EngineTruth truth;
    juce::File configFile;   // where the config was read from
    juce::File saveFile;     // where the window's Save writes
    AppConfig config;
    a3::SpatialConfig spatial;
};

// Reads those three exactly as the start does; touches nothing else. On a refusal `error` is
// "<file>: <reason>". --spatial replaces the config's spatial directory.
std::optional<StartupData> loadStartupData (const AppArgs&, const juce::File& home, juce::String& error);

// --check-config: true with message "ok", or false with "<file>: <reason>". Reads as the start
// does and builds the engine from it (never prepared); opens no audio, no JACK client, no window,
// no socket, and writes nothing.
bool checkConfig (const AppArgs&, const juce::File& home, juce::String& message);
}
