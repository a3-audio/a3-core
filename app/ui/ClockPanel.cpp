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

void fitText (juce::Label& label, juce::Rectangle<int> line, float share)
{
    if (line.getHeight() <= 0 || line.getWidth() <= 0)   // a page too short for the section: nothing to size
    {
        label.setBounds (line);
        return;
    }
    label.setFont (juce::FontOptions (fittedTextHeight (label.getText(), (float) line.getHeight() * share, (float) line.getWidth() * 0.95f)));
    label.setBounds (line);
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
    for (auto* child : std::initializer_list<juce::Component*> { &title, &state })
        addAndMakeVisible (child);
    for (auto* child : std::initializer_list<juce::Component*> { &masterCaption, &master })
        addChildComponent (child);
    for (auto* child : std::initializer_list<juce::Component*> { &role, &rate, &counters, &note })
        addChildComponent (child);
    const bool choice = mode == Mode::choice;
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

// A card as the page's others (look::card), framed in the warning's colour when not locked.
void ClockPanel::paint (juce::Graphics& g)
{
    const int pageWidth = getParentComponent() != nullptr ? getParentComponent()->getWidth() : getWidth();
    look::card (g, getLocalBounds(), pageWidth);
    if (! warns())
        return;
    g.setColour (frame);
    const auto thickness = (float) juce::jmax (2, getHeight() / 80);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (thickness * 0.5f), (float) juce::jmax (2, pageWidth / 60), thickness);
}

void ClockPanel::setTextHeight (float height)
{
    if (height == textHeight)
        return;
    textHeight = height;
    resized();
}

// One size for every line when the page gives one (squeezed across, never smaller); else each fitted to its row.
void ClockPanel::resized()
{
    const int pageWidth = getParentComponent() != nullptr ? getParentComponent()->getWidth() : getWidth();
    auto area = getLocalBounds().reduced (juce::jmax (2, pageWidth / 60));
    const int row = area.getHeight() / (mode == Mode::choice ? 3 : 8);
    const auto place = [&] (juce::Label& label, juce::Rectangle<int> line, float share)
    {
        if (textHeight <= 0.0f)
            return fitText (label, line, share);
        label.setFont (juce::FontOptions (textHeight));
        label.setMinimumHorizontalScale (0.5f);
        label.setBounds (line);
    };
    place (title, area.removeFromTop (row), 0.5f);
    if (mode == Mode::choice)
    {
        auto choice = area.removeFromTop (row);
        place (masterCaption, choice.removeFromLeft (choice.getWidth() / 4), 0.4f);
        master.setBounds (choice.reduced (2));
        auto stateLine = area.removeFromTop (row);
        stateLine.removeFromLeft (stateLine.getWidth() / 4);   // under the master: the dropdowns' column
        place (state, stateLine, 0.45f);
        return;
    }
    place (state, area.removeFromTop (row), 0.45f);
    place (role, area.removeFromTop (row), 0.4f);
    place (rate, area.removeFromTop (row), 0.4f);
    place (counters, area.removeFromTop (row), 0.4f);
    place (note, area, 0.36f);   // the rest: two lines at the page's size
    if (textHeight > 0.0f)
        note.setMinimumHorizontalScale (1.0f);
}
}
