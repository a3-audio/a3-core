// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EngineConfig.h"
#include "IoMap.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <optional>

namespace a3app
{
struct BackendSettings
{
    enum class Type { jack, device } type = Type::jack;
    juce::String deviceType, inputDevice, outputDevice;
    double sampleRate = 44100;
    int blockSize = 128;
};

struct AppConfig
{
    a3::EngineConfig engine;
    BackendSettings backend;
    IoMap io;
    juce::File spatialDirectory { "/usr/share/a3-engine/spatial" };
};

constexpr float minTrimDb = -90.0f, maxTrimDb = 6.0f;

// Sections "engine", "backend", "io", "spatial"; a missing section takes its defaults.
// Refuses (never clamps) a broken file, naming file and reason in `error`.
std::optional<AppConfig> loadAppConfig (const juce::File&, juce::String& error);
// Atomic replace. Called only from the window's Save, never by a load.
juce::Result saveAppConfig (const AppConfig&, const juce::File&);
// --config FILE; else ~/.config/a3/engine/engine.json; else /usr/share/a3-engine/engine.json.
juce::File chooseConfigFile (const juce::String& argument, const juce::File& home, std::function<bool (const juce::File&)> exists);
}
