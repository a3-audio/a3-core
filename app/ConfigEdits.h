// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ConfigFile.h"
#include "EnginePorts.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace a3app
{
// The pages' edits, pure: each returns a config that loadAppConfig would accept, or nullopt
// with the reason. An edit never leaves an I/O entry naming a port the new config lacks.

// The engine's ports for this config (loads its spatial directory).
std::optional<PortNames> portNamesFor (const AppConfig&, juce::String& error);
// The map without the entries that name a port `ports` lacks. Direct links are foreign names
// on both ends and stay.
IoMap prunedTo (const IoMap&, const PortNames& ports);

constexpr int maxChannelsOnPage = 128;
// N channels: the analog pair list follows (a new channel takes the next free pair below the
// return, else it is deck-only).
std::optional<AppConfig> withChannels (const AppConfig&, int channels, juce::String& error);
std::optional<AppConfig> withSpatialDirectory (const AppConfig&, const juce::File& directory, juce::String& error);

enum class TrimZone { main, booth, phones, rec };
std::optional<AppConfig> withTrim (const AppConfig&, TrimZone, float db, juce::String& error);   // refuses outside -90..+6 dB

// "off" or one of a3::headphoneEqModels(); anything else is refused naming it.
std::optional<AppConfig> withHeadphoneEq (const AppConfig&, const juce::String& model, juce::String& error);

bool sameConfig (const AppConfig&, const AppConfig&);

// One engine port as the I/O page shows it. `mapped` is what the config holds (editable);
// `foreign` are live peers the config does not hold (shown, never edited).
struct IoRow
{
    std::string port;
    bool input = false;
    std::vector<std::string> mapped, foreign;
    std::optional<int> deviceChannel;
};
using LiveLinks = std::vector<std::pair<std::string, std::string>>;   // (from, to), as AudioBackend::liveLinks
std::vector<IoRow> ioRows (const AppConfig&, const PortNames&, const LiveLinks&);
}
