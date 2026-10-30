// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AudioBackend.h"
#include "ClockStatus.h"
#include "ConfigEdits.h"
#include "ConfigFile.h"
#include <string>
#include <vector>

namespace a3app
{
// The speaker map's test noise in one zone: a burst on one decoder output channel, or (ambisonic) one
// from a direction into the zone's decoder (Engine::playNoise, playAmbisonicNoise).
struct NoiseRequest
{
    a3::SpeakerZone zone = a3::SpeakerZone::main;
    bool ambisonic = false;
    int channel = 0;                          // 1-based decoder output channel (a channel burst)
    float azimuth = 0.0f, elevation = 0.0f;   // degrees (an ambisonic burst)
};

// What the pages see of the running engine: read-only state, the pending edit (config()) beside
// what runs (running()), and the three ways to change things: edit() stores a pending edit,
// setFilterKind() acts at once, apply() rebuilds after Save has written the file. The window
// never touches the host or the backend itself; the app implements this over its Session, the
// tests with a fake.
class HostView
{
public:
    virtual ~HostView() = default;
    virtual BackendStatus status() const = 0;
    virtual std::vector<a3::Engine::OutputMeter> meters() = 0;     // takes the peaks: one reader
    virtual std::vector<std::string> outputNames() const = 0;      // one per meter, in the same order
    virtual bool coreHeard() const = 0;                            // Core has sent something since the start
    virtual double secondsSinceCore() const = 0;                   // meaningless until coreHeard()
    virtual bool gateOpen() const = 0;                             // Core has opened the output gate
    virtual a3::GateLevels gateLevels() const = 0;                 // the zones' actual gain (main, booth, phones)
    virtual AppConfig config() const = 0;                          // the pending edit (what the AUDIO page edits)
    virtual AppConfig running() const = 0;                         // what runs now: the config the Session was built from
    // Empty while an engine runs; else why none does (a Save whose config and fallback both failed to open).
    virtual juce::String engineProblem() const { return {}; }
    // Links of retired ports the loaded file named and the load left out ("<port> -> <peer>").
    virtual std::vector<std::string> droppedLinks() const { return {}; }
    // Retired config keys the loaded file named and the load ignored ("engine.<key>").
    virtual std::vector<std::string> ignoredKeys() const { return {}; }
    // Routes the loaded file had on the round-trip loop's channels and the load left out (AppConfig::loopDropped).
    virtual std::vector<std::string> loopDropped() const { return {}; }
    // Non-empty when the truth in use lacks a key the engine would send with (an older a3-osc.json).
    virtual juce::String truthNote() const { return {}; }
    // The clock role's report (ClockStatus.h): the one clock's lock, or why nobody reports it.
    virtual a3clock::ClockReport clock() const { return {}; }
    virtual juce::File configFile() const = 0;                     // where Save writes
    virtual int truthDecks() const = 0;                            // EngineTruth::decks: the count of a config that names none
    virtual LiveLinks liveLinks() const = 0;                       // the engine ports' live JACK links, foreign ones included
    virtual void edit (const AppConfig&) = 0;                      // stores the pending edit; nothing runs differently yet
    // After Save has written the file: makes the edit the running config. Re-creates the whole
    // session (new boot id; Core replays through the hello); on failure the old one is back.
    virtual juce::Result apply (const AppConfig&) = 0;
    virtual void setFilterKind (a3::FilterKind) = 0;               // live, on the running engine; also the pending edit and what runs
    virtual void playNoise (const NoiseRequest&) = 0;              // live, on the running engine; nothing when none runs
    virtual void measureRoundTrip() {}                             // over the loop, on the running engine; the result comes with status()
    virtual void measureAnalogChain() {}                           // over the test cable and the loop at once; only ever on request
    // PipeWire (JACK) only: the block, live; kept in the config file at once, no Save. The outcome comes with status().timing.
    virtual void applyTiming (const AudioTiming&) {}
};
}
