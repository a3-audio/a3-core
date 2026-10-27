// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <juce_osc/juce_osc.h>
#include "OscVocabulary.h"

namespace a3app
{
// The engine's hello to Core (ruling R8): "/engine/hello" <boot id> on the first tick
// and every 2 s after. The boot id is made once, at construction, so a restarted
// engine says a new one and Core replays its state into it.
class HelloSender
{
public:
    static constexpr double everySeconds = 2.0;

    HelloSender (const EngineTruth&, std::function<void (const juce::OSCMessage&)> send);
    void tick (double nowSeconds);
    juce::String bootId() const;

private:
    juce::String address, id;
    std::function<void (const juce::OSCMessage&)> send;
    std::optional<double> next;
};
}
