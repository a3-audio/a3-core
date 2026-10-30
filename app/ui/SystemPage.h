// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// SYSTEM and ABOUT, STATUS' sub-pages (2026-10-30, the maintainer: "all content from the Canvas goes to the Engine"): what
// the Canvas showed, in the window's cards. SYSTEM -- every program of the rig with its state, where it runs, its software,
// firmware and audio address, Start/Stop and Restart for the ones on this machine, Start all and Stop all; ABOUT -- the
// project, its links, the contact, Support A³, the A³ System card (the umbrella's pins) and the licences last. Not a key of
// their own: a seventh key in the row made the key text smaller than a quarter of the key.
#pragma once
#include "Look.h"
#include "SystemProbe.h"
#include <functional>
#include <memory>

namespace a3app
{
// Asks the person; calls onYes if they agree. The default is a card inside the window; tests pass their own.
using SystemConfirm = std::function<void (const juce::String& question, const juce::String& detail, std::function<void()> onYes)>;

// The row both sub-pages use: the page's, or less so that the rig's lines fit (one text size on SYSTEM and ABOUT).
int systemRow (const juce::Component& view, int programs);

class SystemRigView final : public juce::Component, public CardPage
{
public:
    SystemRigView (SystemProbe&, SystemConfirm);
    ~SystemRigView() override;
    void refresh();
    // The row's text as painted: its state and its details line ("local · … · sw … · fw …").
    juce::String stateLine (const juce::String& label) const;
    juce::String detailLine (const juce::String& label) const;
    juce::String planLine() const { return shown.planLine; }
    // The row's key: "Start" or "Stop", and "Restart"; nullptr for a program of another machine.
    juce::TextButton* startStopKey (const juce::String& label) const;
    juce::TextButton* restartKey (const juce::String& label) const;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct ProgramRow;
    void ask (const juce::String& question, const juce::String& detail, std::function<void()> then);
    const ProgramRow* rowOf (const juce::String& label) const;
    std::vector<rig::Program> programsHere() const;

    SystemProbe& probe;
    SystemConfirm confirm;
    std::uint64_t seen = 0;
    SystemSnapshot shown;
    juce::Label heading;
    juce::TextButton startAll { "Start all" }, stopAll { "Stop all" };
    std::vector<std::unique_ptr<ProgramRow>> rows;
    juce::Rectangle<int> planArea;
};

class SystemAboutView final : public juce::Component, public CardPage
{
public:
    explicit SystemAboutView (SystemProbe&);
    void refresh();
    const checkout::Facts& checkoutShown() const { return pins; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    SystemProbe& probe;
    std::uint64_t seen = 0;
    checkout::Facts pins;
    juce::Label aboutHeading, supportHeading, systemHeading, licenceHeading;
    juce::OwnedArray<juce::HyperlinkButton> links, supportLinks;
    juce::Rectangle<int> taglineArea, contactArea, bitcoinArea, versionArea, licenceArea;
    std::vector<juce::Rectangle<int>> systemAreas;
};
}
