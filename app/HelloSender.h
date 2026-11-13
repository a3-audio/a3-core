// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <optional>
#include <vector>
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
    // Control thread. Each deck's audio state (index 0 = deck 1) goes out with every hello and,
    // for a deck whose state changed, at once; nothing goes before the first hello.
    void setDeckAudio (const std::vector<bool>& linked);
    // The same for each deck's mode: true while it sends one stereo pair (engine.deck-stereo).
    void setDeckStereo (const std::vector<bool>& stereo);

private:
    // One per-deck word: its pattern ("{deck}" still in it; empty: not sent) and the states last set.
    struct DeckReport
    {
        juce::String pattern;
        std::vector<bool> states;
    };

    void set (DeckReport& report, const std::vector<bool>& states);
    void sendDeck (const DeckReport& report, size_t index);

    juce::String address, id;
    DeckReport deckAudio, deckStereo;
    std::function<void (const juce::OSCMessage&)> send;
    std::optional<double> next;
};
}
