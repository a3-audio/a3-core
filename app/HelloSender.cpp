// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HelloSender.h"
#include <utility>

namespace a3app
{
HelloSender::HelloSender (const EngineTruth& truth, std::function<void (const juce::OSCMessage&)> s)
    : address (truth.helloAddress), id (juce::Uuid().toString()), deckAudio { truth.deckAudioPattern, {} },
      deckStereo { truth.deckStereoPattern, {} }, send (std::move (s))
{
}

void HelloSender::tick (double nowSeconds)
{
    if (next && nowSeconds < *next)
        return;
    next = nowSeconds + everySeconds;
    if (send)
        send (juce::OSCMessage (juce::OSCAddressPattern (address), id));
    for (const auto* report : { &deckAudio, &deckStereo })
        for (size_t i = 0; i < report->states.size(); ++i)
            sendDeck (*report, i);
}

void HelloSender::setDeckAudio (const std::vector<bool>& linked) { set (deckAudio, linked); }

void HelloSender::setDeckStereo (const std::vector<bool>& stereo) { set (deckStereo, stereo); }

void HelloSender::set (DeckReport& report, const std::vector<bool>& states)
{
    const auto before = std::exchange (report.states, states);
    if (! next)   // the first hello sends them all
        return;
    for (size_t i = 0; i < states.size(); ++i)
        if (i >= before.size() || before[i] != states[i])
            sendDeck (report, i);
}

void HelloSender::sendDeck (const DeckReport& report, size_t index)
{
    if (! send || report.pattern.isEmpty())
        return;
    const auto path = report.pattern.replace ("{deck}", juce::String ((int) index + 1));
    send (juce::OSCMessage (juce::OSCAddressPattern (path), (juce::int32) (report.states[index] ? 1 : 0)));
}

juce::String HelloSender::bootId() const { return id; }
}
