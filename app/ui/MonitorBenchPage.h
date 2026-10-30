// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Dropdown.h"
#include "CoreClient.h"
#include "Look.h"
#include "MonitorTable.h"

namespace a3app
{
// BENCH: send one OSC message through Core (to self, mixer or motion), or ask Core for its state again
// (the truth's state.recall); the answer, or Core's refusal, below.
class MonitorBenchPage final : public juce::Component
{
public:
    explicit MonitorBenchPage (CoreClient&);
    ~MonitorBenchPage() override;

    void refresh() {}   // shows only what it was answered
    void setTo (const juce::String&);
    void setAddress (const juce::String&);
    void setValue (const juce::String&);
    void sendNow();
    void recallNow();
    juce::String answerText() const { return answer.getText(); }
    bool answerIsRefusal() const { return refused; }
    juce::String recallLabel() const { return recallButton.getButtonText(); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void show (const juce::Result&);

    CoreClient& client;
    Dropdown to;
    juce::TextEditor address, value;
    juce::TextButton sendButton { "SEND" }, recallButton;
    juce::Label toCaption, addressCaption, valueCaption, answer;
    bool refused = false;
};
}
