// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// a3-engine: the engine app. For now headless only (--no-window): the host, the JACK
// backend and the message loop with the 50 Hz control timer. The window, the OSC link
// to Core and the device backend come with the tasks that build them.
#include "ConfigFile.h"
#include "EngineHost.h"
#include "JackBackend.h"
#include "OscVocabulary.h"
#include <csignal>
#include <cstdio>
#include <optional>

namespace
{
volatile std::sig_atomic_t stopRequested = 0;
void onSignal (int) { stopRequested = 1; }

struct Args
{
    juce::String config, truth = "/usr/share/a3/a3-osc.json";
    bool noConnect = false, noWindow = false;
};

std::optional<Args> parseArgs (int argc, char** argv)
{
    Args args;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--config" && i + 1 < argc)
            args.config = argv[++i];
        else if (a == "--truth" && i + 1 < argc)
            args.truth = argv[++i];
        else if (a == "--no-connect")
            args.noConnect = true;
        else if (a == "--no-window")
            args.noWindow = true;
        else
            return std::nullopt;
    }
    return args;
}

int fail (const juce::String& message)
{
    std::fprintf (stderr, "a3-engine: %s\n", message.toRawUTF8());
    return 1;
}

double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

// The message thread's beat: the host's control tick (modulation, meters, swaps, hello)
// and the backend's share (rebuilds, connections). Ends the loop on SIGINT/SIGTERM or
// when the audio server is gone.
class ControlTimer final : public juce::Timer
{
public:
    ControlTimer (a3app::EngineHost& h, a3app::JackBackend& b) : host (h), backend (b) {}

    void timerCallback() override
    {
        const auto now = nowSeconds();
        host.controlTick (now);
        backend.service (now);
        if (stopRequested == 0 && ! backend.lost())
            return;
        stopTimer();
        juce::MessageManager::getInstance()->stopDispatchLoop();
    }

private:
    a3app::EngineHost& host;
    a3app::JackBackend& backend;
};

int run (const Args& args)
{
    juce::String error;
    const auto truth = a3app::parseEngineTruth (juce::File (args.truth).loadFileAsString(), error);
    if (! truth)
        return fail (args.truth + ": " + error);

    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto configFile = a3app::chooseConfigFile (args.config, home, [] (const juce::File& f) { return f.existsAsFile(); });
    const auto config = a3app::loadAppConfig (configFile, error);
    if (! config)
        return fail (error);
    if (config->backend.type != a3app::BackendSettings::Type::jack)
        return fail (configFile.getFullPathName() + ": this build has only the JACK backend");
    const auto spatial = a3::loadSpatialConfig (config->spatialDirectory, error);
    if (! spatial)
        return fail (config->spatialDirectory.getFullPathName() + ": " + error);

    a3app::EngineHost host (config->engine, *spatial, *truth);
    a3app::JackBackend backend (! args.noConnect);
    const auto opened = backend.open (host, *config);
    if (opened.failed())
        return fail (opened.getErrorMessage());
    const auto status = backend.status();
    std::fprintf (stderr, "a3-engine: %s, %.0f Hz, %d frames, %s\n", configFile.getFullPathName().toRawUTF8(),
                  status.sampleRate, status.blockSize, status.connecting ? "connecting" : "--no-connect: no links");

    ControlTimer timer (host, backend);
    timer.startTimerHz (50);
    juce::MessageManager::getInstance()->runDispatchLoop();
    const bool lost = backend.lost();
    backend.close();
    return lost ? fail ("the JACK server went away") : 0;
}
}

int main (int argc, char** argv)
{
    const auto args = parseArgs (argc, argv);
    if (! args)
    {
        std::fprintf (stderr, "usage: a3-engine [--config FILE] [--truth FILE] [--no-connect] [--no-window]\n"
                              "  --config: default ~/.config/a3/engine/engine.json, else /usr/share/a3-engine/engine.json\n"
                              "  --no-connect: make no JACK link (a run beside REAPER)\n"
                              "  --no-window: run headless (the only mode of this build)\n");
        return 2;
    }
    std::signal (SIGINT, onSignal);
    std::signal (SIGTERM, onSignal);
    juce::ScopedJuceInitialiser_GUI messageLoop;   // juce_events' message thread; no window
    return run (*args);
}
