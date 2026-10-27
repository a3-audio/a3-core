// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HelloSender.h"
#include <utility>

namespace a3app
{
HelloSender::HelloSender (const EngineTruth& truth, std::function<void (const juce::OSCMessage&)> s)
    
    : address (truth.helloAddress), deckAudioPattern (truth.deckAudioPattern), id (juce::Uuid().toString()), send (std::move (s))
{
}

void HelloSender::tick (double nowSeconds)
{
    if (next && nowSeconds < *next)
        return;
    next = nowSeconds + everySeconds;
    if (send)
        send (juce::OSCMessage (juce::OSCAddressPattern (address), id));
    for (size_t i = 0; i < deckAudio.size(); ++i)
        sendDeckAudio (i);
}

void HelloSender::setDeckAudio (const std::vector<bool>& linked)
{
    const auto before = std::exchange (deckAudio, linked);
    if (! next)   // the first hello sends them all
        return;
    for (size_t i = 0; i < linked.size(); ++i)
        if (i >= before.size() || before[i] != linked[i])
            sendDeckAudio (i);
}

void HelloSender::sendDeckAudio (size_t index)
{
    if (! send)
        return;
    const auto path = deckAudioPattern.replace ("{deck}", juce::String ((int) index + 1));
    send (juce::OSCMessage (juce::OSCAddressPattern (path), (juce::int32) (deckAudio[index] ? 1 : 0)));
}

juce::String HelloSender::bootId() const { return id; }
}
