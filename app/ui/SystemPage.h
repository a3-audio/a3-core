// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// RIG (a page of its own since 2026-10-31) and STATUS' ABOUT (2026-10-30, the maintainer: "all content from the Canvas goes
// to the Engine"): what the Canvas showed, in the window's cards. RIG -- MAIN (Start all, Stop all, the plan's word) and
// ENTITIES: a row per program of the rig with its state, where it runs, its software, firmware and audio address,
// Start/Stop and Restart for the ones on this machine, scrolled by a finger inside the card. ABOUT -- the project, its
// links, the contact, Support A³, PUBLISHED (each repository of the umbrella's pins with its link and tag) and the
// licences last. No explanation lines.
#pragma once
#include "Look.h"
#include "SystemProbe.h"
#include <functional>
#include <memory>

namespace a3app
{
// Asks the person; calls onYes if they agree. The default is a card inside the window; tests pass their own.
using SystemConfirm = std::function<void (const juce::String& question, const juce::String& detail, std::function<void()> onYes)>;

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
    // The programs' rows inside ENTITIES, scrolled (the list's children are their names and keys).
    int programRows() const { return (int) rows.size(); }
    juce::Component& programList();
    juce::Rectangle<int> entitiesView() const { return scroller.getBounds(); }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct ProgramRow;
    class List;
    void ask (const juce::String& question, const juce::String& detail, std::function<void()> then);
    const ProgramRow* rowOf (const juce::String& label) const;
    std::vector<rig::Program> programsHere() const;

    SystemProbe& probe;
    SystemConfirm confirm;
    std::uint64_t seen = 0;
    SystemSnapshot shown;
    juce::Label heading, entitiesHeading;
    juce::TextButton startAll { "Start all" }, stopAll { "Stop all" };
    std::vector<std::unique_ptr<ProgramRow>> rows;
    std::unique_ptr<List> list;
    juce::Viewport scroller;
    juce::Rectangle<int> planArea;
};

class SystemAboutView final : public juce::Component, public CardPage
{
public:
    explicit SystemAboutView (SystemProbe&);
    ~SystemAboutView() override;
    void refresh();
    const checkout::Facts& checkoutShown() const { return pins; }
    int lineHeight() const { return layout::compactLine (layout::tokens (*this)); }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void makePublished();

    SystemProbe& probe;
    std::uint64_t seen = 0;
    checkout::Facts pins;
    juce::Label aboutHeading, supportHeading, publishedHeading, licenceHeading;
    juce::OwnedArray<juce::HyperlinkButton> links, supportLinks;
    juce::OwnedArray<juce::Component> repositories;   // a link per repository with a url, its name as text without one
    juce::Rectangle<int> taglineArea, contactArea, bitcoinArea, versionArea, licenceArea;
    std::vector<juce::Rectangle<int>> publishedAreas;
};
}
