// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HelloSender.h"

namespace a3app
{
HelloSender::HelloSender (const EngineTruth& truth, std::function<void (const juce::OSCMessage&)> s)
    : address (truth.helloAddress), id (juce::Uuid().toString()), send (std::move (s))
{
}

void HelloSender::tick (double nowSeconds)
{
    if (next && nowSeconds < *next)
        return;
    next = nowSeconds + everySeconds;
    if (send)
        send (juce::OSCMessage (juce::OSCAddressPattern (address), id));
}

juce::String HelloSender::bootId() const { return id; }
}
