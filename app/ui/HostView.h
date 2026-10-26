// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AudioBackend.h"
#include "ConfigFile.h"
#include <string>
#include <vector>

namespace a3app
{
// What the pages see of the running engine: read-only state, plus apply() for an edited
// configuration. The window never touches the host or the backend itself; the app implements
// this over its Session, the tests with a fake.
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
    virtual AppConfig config() const = 0;
    virtual juce::Result apply (const AppConfig&) = 0;             // stores an edit; Save and rebuild are the window's later step
};
}
