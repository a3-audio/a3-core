// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// A layout saved in the window changes the Core's AES67 streams (Aes67Streams.h): the engine, which re-creates its
// session on that Save (EngineRole::restart), renders the config again and restarts the sender unit, so the
// streams are announced with the channels the new layout has. Control thread only.
#pragma once
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "Aes67Streams.h"
#include "AudioTiming.h"
#include "CommandRunner.h"
#include "Truth.h"

namespace a3app
{
// What the render needs that the engine does not otherwise hold: the Core's truth and the machine's places.
struct Aes67Render
{
    a3core::Truth truth;
    juce::File home;
    juce::File aes67Template;
    std::string hostname;
};

Command restartAes67Command();   // systemctl --user restart a3-aes67.service

// Nothing (an empty line) when the rendered config already announces `streams`. Else the config is rendered
// again and, if it is ours after that (not kept by hand, not removed), a3-aes67 is restarted through the runner
// once. The line says what happened.
std::string reannounceStreams (const Aes67Render&, const std::vector<a3core::StreamSpec>&, CommandRunner&);
}
