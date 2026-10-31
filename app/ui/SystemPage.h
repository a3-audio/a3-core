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
    // The a3-core card's service lines as painted ("a3-engine.service  active · enabled"), and whether a program has a
    // row of its own (the beat analyzer sits inside the a3-core card when it is one of the Core's units).
    juce::StringArray serviceLines() const;
    bool hasOwnRow (const juce::String& label) const;
    // The a3-core card's collapsed line as painted ("9 services · all running"), whether the list is open, and the tap on the
    // disclosure key (closed at the start; kept while the window lives).
    juce::String serviceSummary() const;
    bool servicesOpen() const;
    void toggleServices();
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
    juce::String listLayout;
};

class SystemAboutView final : public juce::Component, public CardPage
{
public:
    explicit SystemAboutView (SystemProbe&);
    ~SystemAboutView() override;
    void refresh();
    const checkout::Facts& checkoutShown() const { return pins; }
    juce::Rectangle<int> taglineBounds() const { return taglineArea; }   // where "Spatial DJ setup" is drawn
    int treeBottom() const { return tree.empty() ? 0 : tree.back().area.getBottom(); }   // the last tree row's lower edge
    int lineHeight() const { return layout::compactLine (layout::tokens (*this)); }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void makePublished();

    SystemProbe& probe;
    std::uint64_t seen = 0;
    checkout::Facts pins;
    struct TreeLine
    {
        checkout::Row row;
        juce::String name, prefix;   // shown name, the connectors ahead of it
        juce::Rectangle<int> area, nameArea, versionArea, licenceArea;
    };
    std::unique_ptr<juce::Drawable> logoImage;   // the SVG logo; null: big letters
    juce::Label publishedHeading;
    juce::OwnedArray<juce::HyperlinkButton> links;   // Documentation, Website, Source, Support A³
    juce::OwnedArray<juce::Component> repositories;   // per tree line: its link, or nullptr when it has no url
    std::vector<TreeLine> tree;
    juce::Rectangle<int> logoArea, taglineArea, contactArea, versionArea, versionColumn, licenceColumn, treeMessageArea;
};
}
