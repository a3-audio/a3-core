// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EngineConfig.h"
#include "IoMap.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <optional>
#include <string>
#include <vector>

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
    // Entries of the loaded file's io section that named a retired deck port ("<port> -> <peer>"),
    // left out of `io`. Only a load fills it; a save writes the map without them.
    std::vector<std::string> droppedLinks;
    // Retired keys the loaded file named ("engine.<key>"), ignored; a save never writes them.
    std::vector<std::string> ignoredKeys;
};

constexpr float minTrimDb = -90.0f, maxTrimDb = 6.0f;

// Sections "engine", "backend", "io", "spatial"; a missing section takes its defaults.
// Refuses (never clamps) a broken file, naming file and reason in `error`. Links of retired
// deck ports (EnginePorts.h) are dropped, not refused, and listed in droppedLinks. truthDecks is the
// deck count of a file that names none (EngineTruth::decks). Every slot whose ports the file links
// nowhere links to its local StemDeck (A9); parseAppConfig does not fill (A10: a posted body is kept).
std::optional<AppConfig> loadAppConfig (const juce::File&, juce::String& error, int truthDecks);
// "dropped retired link <entry>" per entry of AppConfig::droppedLinks, then "ignored retired key <key>"
// per entry of AppConfig::ignoredKeys.
juce::StringArray droppedLinkLines (const AppConfig&);
// The four sections as saveAppConfig writes them, and back (an absolute "spatial" ignores `base`, a relative
// one is read from it). The control API carries configs in this form. `error` is the reason alone.
juce::var toJson (const AppConfig&);
std::optional<AppConfig> parseAppConfig (const juce::var&, const juce::File& base, juce::String& error, int truthDecks);
// Atomic replace. Called only from the window's Save, never by a load.
juce::Result saveAppConfig (const AppConfig&, const juce::File&);
inline const juce::File packageConfigFile { "/usr/share/a3-engine/engine.json" };

// The spatial directories the ZONES page may offer, in order: the user's (<configDir>/engine/spatial), the
// package's default (AppConfig's), and the package's REAPER booth variant beside it. Existing or not.
std::vector<juce::File> spatialCandidates (const juce::File& configDir);
// Those of them that hold a main.decoder.json, then the user's variants (<configDir>/engine/spatial/<name>,
// the SPEAKERS page's saves) that do, by name.
std::vector<juce::File> spatialChoices (const juce::File& configDir);

// ~/.config/a3/engine/engine.json under `home`.
juce::File userConfigFile (const juce::File& home);
// --config FILE; else ~/.config/a3/engine/engine.json; else the package's file.
juce::File chooseConfigFile (const juce::String& argument, const juce::File& home, std::function<bool (const juce::File&)> exists,
                             const juce::File& packageConfig = packageConfigFile);
// Where Save writes: --config FILE, else the user's file, even when the package's file was read
// (the package's directory is not writable, and the saved spatial path is absolute).
juce::File chooseSaveFile (const juce::String& argument, const juce::File& home);
}
