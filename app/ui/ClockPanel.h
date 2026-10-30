// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include "Dropdown.h"
#include "ClockStatus.h"

namespace a3app
{
// The colour of a clock indicator's tone (the panel's frame, the footer's word).
juce::Colour toneColour (a3clock::ClockIndicator::Tone);

// The AUDIO page's CLOCK section: the one clock of the A³ system (src/a3-clock/ClockSteering.h). The
// clock master (one choice in this version, and why), the lock state with its phase error, the
// frequency the PTP clock is steered by and how far it runs from the Scarlett now, and the counters
// the soak test reads. Anything but a lock is framed in a warning colour: caution while it acquires or
// only observes, danger in holdover, at a fault or when the clock role is not running.
// The deck hosts' own PTP offsets are not shown: Core does not know them yet.
class ClockPanel final : public juce::Component
{
public:
    ClockPanel();

    void show (const a3clock::ClockReport&);
    // Every line of the panel at this height (the AUDIO page's one size); 0: a share of the panel's rows.
    void setTextHeight (float height);

    bool warns() const { return ! frame.isTransparent(); }
    const juce::Label& stateLabel() const { return state; }
    const juce::Label& roleLabel() const { return role; }   // the PTP role, a second line
    const juce::Label& rateLabel() const { return rate; }
    const juce::Label& countersLabel() const { return counters; }
    const juce::Label& noteLabel() const { return note; }
    const juce::ComboBox& masterBox() const { return master; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Label title, masterCaption, state, role, rate, counters, note;
    Dropdown master;
    juce::Colour frame = juce::Colours::transparentBlack;
    float textHeight = 0.0f;
};
}
