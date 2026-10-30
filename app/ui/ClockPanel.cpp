// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockPanel.h"

#include "Look.h"

namespace a3app
{
namespace
{
using a3clock::Health;

const juce::String arrow = juce::CharPointer_UTF8 ("\xe2\x86\x92");
const juce::String micro = juce::CharPointer_UTF8 ("\xc2\xb5");
const juce::String dot = juce::CharPointer_UTF8 ("  \xc2\xb7  ");

juce::String signedFixed (double value, int decimals) { return (value >= 0.0 ? "+" : "") + juce::String (value, decimals); }

juce::String duration (double seconds)
{
    const int minutes = (int) (seconds / 60.0);
    return minutes >= 60 ? juce::String (minutes / 60) + " h " + juce::String (minutes % 60) + " min" : juce::String (minutes) + " min";
}

juce::String errorText (const a3clock::ClockStatus& s) { return "phase error " + signedFixed (s.errorNs / 1000.0, 1) + " " + micro + "s"; }

juce::String stateText (const a3clock::ClockReport& report)
{
    const auto indicator = a3clock::clockIndicator (report);
    const auto word = juce::String (indicator.word);
    const auto& s = report.status;
    if (report.running && s.health == Health::locked)
        return word + dot + errorText (s) + dot + "for " + duration (s.lockedSeconds);
    if (report.running && s.health == Health::acquiring)
        return word + dot + errorText (s) + dot + juce::String::fromUTF8 (indicator.detail.c_str());
    return word + dot + juce::String::fromUTF8 (indicator.detail.c_str());
}

juce::String rateText (const a3clock::ClockReport& report)
{
    if (! report.running)
        return {};
    const auto& s = report.status;
    const auto now = s.ratePpm ? signedFixed (*s.ratePpm, 3) + " ppm" : juce::String ("not measured yet");
    if (s.health == Health::observing)
        return "PTP clock against the Scarlett " + now + " (not steered)";
    return "PTP clock " + juce::String (s.device) + " (" + juce::String (s.interface) + ") steered " + signedFixed (s.frequencyPpm, 2) + " ppm"
           + dot + "against the Scarlett now " + now;
}

juce::String countersText (const a3clock::ClockReport& report)
{
    if (! report.running)
        return {};
    const auto& s = report.status;
    return "corrections " + juce::String (s.counters.corrections) + dot + "re-anchors " + juce::String (s.counters.reanchors) + dot
           + "PTP steps " + juce::String (s.counters.phcSteps) + dot + "holdovers " + juce::String (s.counters.holdovers) + dot
           + "Scarlett breaks " + juce::String (s.scarlettBreaks);
}

}

juce::Colour toneColour (a3clock::ClockIndicator::Tone tone)
{
    switch (tone)
    {
        case a3clock::ClockIndicator::Tone::ok: return palette::running;
        case a3clock::ClockIndicator::Tone::caution: return palette::caution;
        case a3clock::ClockIndicator::Tone::danger: return palette::danger;
    }
    return palette::danger;
}

namespace
{
juce::Colour warningOf (const a3clock::ClockReport& report)
{
    const auto tone = a3clock::clockIndicator (report).tone;
    return tone == a3clock::ClockIndicator::Tone::ok ? juce::Colours::transparentBlack : toneColour (tone);
}

}

ClockPanel::ClockPanel (Mode m) : mode (m)
{
    title.setText ("CLOCK", juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, palette::textMuted);
    masterCaption.setText ("Master", juce::dontSendNotification);
    look::caption (masterCaption);
    master.addItem ("Scarlett " + arrow + " PTP grandmaster", 1);
    master.setSelectedId (1, juce::dontSendNotification);
    note.setText ("The only master in this version. An external PTP grandmaster would make the Core a PTP client and "
                  "need the Scarlett on word clock; not offered.",
                  juce::dontSendNotification);
    note.setColour (juce::Label::textColourId, palette::textMuted);
    for (auto* label : { &state, &role, &rate, &counters })
    {
        label->setColour (juce::Label::textColourId, palette::text);
        label->setJustificationType (juce::Justification::centredLeft);
        label->setMinimumHorizontalScale (1.0f);
    }
    rate.setColour (juce::Label::textColourId, palette::textMuted);
    counters.setColour (juce::Label::textColourId, palette::textMuted);
    addAndMakeVisible (title);
    addChildComponent (state);
    for (auto* child : std::initializer_list<juce::Component*> { &masterCaption, &master })
        addChildComponent (child);
    for (auto* child : std::initializer_list<juce::Component*> { &role, &rate, &counters, &note })
        addChildComponent (child);
    const bool choice = mode == Mode::choice;
    state.setVisible (! choice);   // AUDIO's card: the master only, the state word is in the footer
    masterCaption.setVisible (choice);
    master.setVisible (choice);
    for (auto* child : std::initializer_list<juce::Component*> { &role, &rate, &counters, &note })
        child->setVisible (! choice);
    show ({});
}

void ClockPanel::show (const a3clock::ClockReport& report)
{
    state.setText (mode == Mode::choice ? juce::String::fromUTF8 (a3clock::clockIndicator (report).word.c_str()) : stateText (report),
                   juce::dontSendNotification);
    const auto roleLine = a3clock::ptpRoleLine (report);
    role.setText (juce::String::fromUTF8 (roleLine.text.c_str()), juce::dontSendNotification);
    role.setColour (juce::Label::textColourId, roleLine.warns ? palette::danger : palette::textMuted);
    rate.setText (rateText (report), juce::dontSendNotification);
    counters.setText (countersText (report), juce::dontSendNotification);
    const auto warning = warningOf (report);
    state.setColour (juce::Label::textColourId, warning.isTransparent() ? palette::running : warning);
    if (warning != frame)
    {
        frame = warning;
        repaint();
    }
    resized();   // the texts' sizes follow their length
}

// The panel paints no ground: it sits in its page's card, and the page frames the card in frameColour().
void ClockPanel::paint (juce::Graphics&) {}

void ClockPanel::setTokens (const layout::Tokens& t)
{
    tokens = t;
    resized();
}

// The page's one text size; the choice is a heading and the master's line, the detail a heading and its lines.
void ClockPanel::resized()
{
    auto area = getLocalBounds();
    const auto place = [&] (juce::Label& label, juce::Rectangle<int> line)
    {
        label.setFont (juce::FontOptions (tokens.text > 0 ? tokens.text : (float) line.getHeight() * 0.5f));
        label.setMinimumHorizontalScale (0.5f);
        label.setBounds (line);
    };
    const int heading = tokens.heading > 0 ? tokens.heading : area.getHeight() / 4;
    place (title, area.removeFromTop (heading));
    if (mode == Mode::choice)
    {
        auto choice = area;
        place (masterCaption, choice.removeFromLeft (choice.getWidth() / 4));
        master.setBounds (tokens.row > 0 ? layout::control (choice, tokens) : choice);
        return;
    }
    // Compact lines, one after the other as STATUS' driver lines; a line with nothing to say is not shown.
    const int line = tokens.row > 0 ? layout::compactLine (tokens) : juce::jmax (1, area.getHeight() / mostDetailLines);
    for (auto* label : { &state, &role, &rate, &counters, &note })
    {
        label->setVisible (label->getText().isNotEmpty());
        if (label->isVisible())
            place (*label, area.removeFromTop (label == &note ? 2 * line : line));
    }
    note.setMinimumHorizontalScale (1.0f);
}

int ClockPanel::detailLines() const
{
    if (mode == Mode::choice)
        return 0;
    int lines = 0;
    for (const auto* label : { &state, &role, &rate, &counters })
        lines += label->getText().isNotEmpty() ? 1 : 0;
    return lines + (note.getText().isNotEmpty() ? 2 : 0);
}
}
