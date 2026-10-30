// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemPage.h"

namespace a3app
{
namespace
{
const juce::String dot = juce::String::fromUTF8 (" · ");
constexpr int minimumRow = 16;
constexpr int systemColumns = 2;   // the A³ System card's rows run in two columns

juce::Colour lightColour (rig::Light light)
{
    switch (light)
    {
        case rig::Light::running: return palette::running;
        case rig::Light::starting:
        case rig::Light::degraded: return palette::caution;
        case rig::Light::failed: return palette::danger;
        case rig::Light::missing:
        case rig::Light::stopped:
        case rig::Light::elsewhere:
        case rig::Light::unknown: return palette::textMuted;
    }
    return palette::textMuted;
}

juce::String details (const rig::Row& row)
{
    juce::StringArray parts;
    if (row.where.isNotEmpty())
        parts.add (row.where);
    if (row.audio.isNotEmpty())
        parts.add ("audio " + row.audio);
    parts.add ("sw " + row.software);
    if (row.firmware.isNotEmpty())
        parts.add ("fw " + row.firmware);
    return parts.joinIntoString (dot);
}

void askInTheWindow (juce::Component& page, const juce::String& question, const juce::String& detail, std::function<void()> onYes)
{
    const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::NoIcon)
                             .withTitle (question).withMessage (detail)
                             .withButton ("Yes").withButton ("Cancel").withParentComponent (look::dialogParent (page));
    juce::AlertWindow::showAsync (options, [onYes = std::move (onYes)] (int result) { if (result == 1 && onYes) onYes(); });
}

void setHeading (juce::Label& label, const juce::String& text)
{
    look::caption (label);
    look::say (label, text);
}

juce::Font linkFont (float text) { return juce::Font (juce::FontOptions (text)).withStyle (juce::Font::underlined); }

// Links side by side from the line's left, each as wide as its word and a unit's space after it.
void placeLinks (juce::OwnedArray<juce::HyperlinkButton>& row, juce::Rectangle<int> line, const layout::Tokens& t)
{
    for (auto* link : row)
    {
        link->setColour (juce::HyperlinkButton::textColourId, palette::text);
        link->setFont (linkFont (t.text), false, juce::Justification::centredLeft);
        const auto width = juce::roundToInt (juce::GlyphArrangement::getStringWidth (linkFont (t.text), link->getButtonText())) + 2 * t.unit;
        link->setBounds (layout::control (line.removeFromLeft (width), t));
        line.removeFromLeft (t.unit);
    }
}

// JUCE's link says its URL as a tip; the window has none (2026-10-30).
std::unique_ptr<juce::HyperlinkButton> makeLink (const juce::String& label, const juce::String& url)
{
    auto link = std::make_unique<juce::HyperlinkButton> (label, juce::URL (url));
    link->setTooltip ({});
    return link;
}

int systemLines (const checkout::Facts& pins)
{
    return pins.found ? ((int) pins.rows.size() + systemColumns - 1) / systemColumns : 1;
}

int supportLines (const about::Support& support) { return (support.paypal.isNotEmpty() ? 1 : 0) + (support.kofi.isNotEmpty() ? 1 : 0) + (support.bitcoin.isNotEmpty() ? 1 : 0); }
}

int systemRow (const juce::Component& view, int programs)
{
    const auto t = layout::tokens (view);
    const int lines = 1 + 2 * programs;   // Start all / Stop all, then two lines a program
    const float room = (float) (view.getHeight() - 2 * t.margin - 2 * t.padding);
    const int fitted = (int) (room / ((float) lines + 0.6f));   // and the card's heading (3/5 of a row)
    return juce::jmax (minimumRow, juce::jmin (look::rowHeight (view), fitted));
}

// -- RIG ------------------------------------------------------------------------------------------------------------

struct SystemRigView::ProgramRow
{
    rig::Program program;
    rig::Row row;
    juce::TextButton startStop { "Start" }, restart { "Restart" };
    juce::Rectangle<int> first, second;
};

SystemRigView::SystemRigView (SystemProbe& p, SystemConfirm c)
    : probe (p), confirm (c ? std::move (c) : SystemConfirm ([this] (const juce::String& q, const juce::String& d, std::function<void()> yes)
                                                               { askInTheWindow (*this, q, d, std::move (yes)); }))
{
    setHeading (heading, "RIG" + dot + juce::SystemStats::getComputerName());
    addAndMakeVisible (this->heading);
    for (const auto& program : probe.programs())
    {
        auto made = std::make_unique<ProgramRow>();
        made->program = program;
        auto* raw = made.get();
        made->startStop.onClick = [this, raw] {
            const auto& program = raw->program;
            if (! raw->row.up)
                return void (probe.run ("Start " + program.label, rig::start (program)));
            ask ("Stop " + program.label + "?", "It stops now; what it plays stops with it.",
                 [this, program] { probe.run ("Stop " + program.label, rig::stop (program)); });
        };
        made->restart.onClick = [this, raw] {
            const auto program = raw->program;
            ask ("Restart " + program.label + "?", "It stops and starts again; what it plays stops with it.",
                 [this, program] { probe.run ("Restart " + program.label, rig::restart (program)); });
        };
        addChildComponent (made->startStop);
        addChildComponent (made->restart);
        rows.push_back (std::move (made));
    }
    startAll.onClick = [this] { probe.run ("Start all", rig::startAll (programsHere())); };
    stopAll.onClick = [this] {
        ask ("Stop the whole rig?", "Every program of this machine stops: the decks, the desk, Motion, the beat analyzer, Core "
                                    "and the engine. The sound stops.",
             [this] { probe.run ("Stop all", rig::stopAll (programsHere())); });
    };
    addAndMakeVisible (startAll);
    addAndMakeVisible (stopAll);
    refresh();
}

SystemRigView::~SystemRigView() = default;

void SystemRigView::ask (const juce::String& question, const juce::String& detail, std::function<void()> then)
{
    confirm (question, detail, std::move (then));
}

std::vector<rig::Program> SystemRigView::programsHere() const
{
    std::vector<rig::Program> here;
    for (const auto& row : rows)
        if (row->row.here && row->row.installed)
            here.push_back (row->program);
    return here;
}

void SystemRigView::refresh()
{
    const auto snapshot = probe.snapshot();
    if (snapshot.generation == seen && seen != 0)
        return;
    seen = snapshot.generation;
    shown = snapshot;
    for (auto& r : rows)
    {
        r->row = rig::describe (r->program, shown.facts);
        const bool acts = r->row.here && r->row.installed;
        r->startStop.setVisible (acts);
        r->restart.setVisible (acts);
        r->startStop.setButtonText (r->row.up ? "Stop" : "Start");
        r->startStop.setEnabled (! shown.busy);
        r->restart.setEnabled (! shown.busy && r->row.light != rig::Light::stopped);
    }
    const bool any = ! programsHere().empty();
    startAll.setEnabled (any && ! shown.busy);
    stopAll.setEnabled (any && ! shown.busy);
    repaint();
}

const SystemRigView::ProgramRow* SystemRigView::rowOf (const juce::String& label) const
{
    for (const auto& r : rows)
        if (r->program.label == label)
            return r.get();
    return nullptr;
}

juce::String SystemRigView::stateLine (const juce::String& label) const
{
    const auto* r = rowOf (label);
    return r != nullptr ? r->row.state : juce::String();
}

juce::String SystemRigView::detailLine (const juce::String& label) const
{
    const auto* r = rowOf (label);
    return r != nullptr ? details (r->row) : juce::String();
}

juce::TextButton* SystemRigView::startStopKey (const juce::String& label) const
{
    const auto* r = rowOf (label);
    return r != nullptr && r->startStop.isVisible() ? const_cast<juce::TextButton*> (&r->startStop) : nullptr;
}

juce::TextButton* SystemRigView::restartKey (const juce::String& label) const
{
    const auto* r = rowOf (label);
    return r != nullptr && r->restart.isVisible() ? const_cast<juce::TextButton*> (&r->restart) : nullptr;
}

// One card (layout::Stack): the heading, Start all and Stop all with the plan's word beside them, then two lines a
// program: its light, name and state with its keys on the right; where it runs and what it runs below, muted.
void SystemRigView::resized()
{
    const auto t = layout::tokens (*this, systemRow (*this, (int) rows.size()));
    layout::Stack stack (getLocalBounds().reduced (t.margin), t, cards);
    auto area = stack.card (layout::cardHeight (t, 1 + 2 * (int) rows.size())).reduced (t.padding);
    heading.setFont (juce::FontOptions (t.text));
    heading.setBounds (area.removeFromTop (t.heading));
    auto keys = area.removeFromTop (t.row);
    const int keyWidth = keys.getWidth() / 6;
    startAll.setBounds (layout::control (keys.removeFromLeft (keyWidth), t));
    keys.removeFromLeft (t.unit);
    stopAll.setBounds (layout::control (keys.removeFromLeft (keyWidth), t));
    keys.removeFromLeft (2 * t.unit);
    planArea = keys;
    for (auto& r : rows)
    {
        r->first = area.removeFromTop (t.row);
        r->second = area.removeFromTop (t.row);
        auto line = r->first;
        r->restart.setBounds (layout::control (line.removeFromRight (keyWidth), t));
        line.removeFromRight (t.unit);
        r->startStop.setBounds (layout::control (line.removeFromRight (keyWidth), t));
        r->first = line.withTrimmedRight (t.unit);
    }
}

void SystemRigView::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    const auto t = layout::tokens (*this, systemRow (*this, (int) rows.size()));
    paintCards (g, *this, t);
    g.setFont (juce::FontOptions (t.text));
    g.setColour (shown.planLine.contains ("failed") || shown.planLine.contains ("skipped") ? palette::caution : palette::textMuted);
    g.drawFittedText (shown.planLine, planArea, juce::Justification::centredLeft, 1, 0.8f);
    const juce::Font font { juce::FontOptions (t.text) };
    for (const auto& r : rows)
    {
        auto line = r->first;
        const auto lightSide = t.text * 0.6f;
        g.setColour (lightColour (r->row.light));
        g.fillEllipse (line.removeFromLeft (juce::roundToInt (lightSide)).toFloat().withSizeKeepingCentre (lightSide, lightSide));
        line.removeFromLeft (t.unit);
        g.setColour (r->row.light == rig::Light::missing ? palette::textMuted : palette::text);
        g.drawText (r->program.label, line, juce::Justification::centredLeft, true);
        line.removeFromLeft (juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, r->program.label)) + 2 * t.unit);
        const bool news = r->row.light == rig::Light::degraded || r->row.light == rig::Light::failed;
        g.setColour (news ? lightColour (r->row.light) : palette::text);
        g.drawFittedText (r->row.state, line, juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (palette::textMuted);
        g.drawFittedText (details (r->row), r->second.withTrimmedLeft (juce::roundToInt (lightSide) + t.unit), juce::Justification::centredLeft, 1, 0.7f);
    }
}

// -- ABOUT ----------------------------------------------------------------------------------------------------------

SystemAboutView::SystemAboutView (SystemProbe& p) : probe (p)
{
    setHeading (aboutHeading, juce::String::fromUTF8 ("A³"));
    setHeading (supportHeading, juce::String::fromUTF8 ("SUPPORT A³"));
    setHeading (systemHeading, juce::String::fromUTF8 ("A³ SYSTEM"));
    setHeading (licenceHeading, "LICENCES");
    for (auto* label : { &aboutHeading, &systemHeading, &licenceHeading })
        addAndMakeVisible (label);
    for (const auto& link : about::links())
        addAndMakeVisible (links.add (makeLink (link.label, link.url)));
    const auto& support = probe.support();
    if (support.paypal.isNotEmpty())
        addAndMakeVisible (supportLinks.add (makeLink ("PayPal", support.paypal)));
    if (support.kofi.isNotEmpty())
        addAndMakeVisible (supportLinks.add (makeLink ("Ko-fi", support.kofi)));
    if (! support.isEmpty())
        addAndMakeVisible (supportHeading);
    refresh();
}

void SystemAboutView::refresh()
{
    const auto snapshot = probe.snapshot();
    if (snapshot.generation == seen && seen != 0)
        return;
    seen = snapshot.generation;
    if (snapshot.checkout == pins)
        return;
    const bool resize = systemLines (snapshot.checkout) != systemLines (pins);
    pins = snapshot.checkout;
    if (resize)
        resized();
    repaint();
}

// Cards (layout::Stack): the project (its line, its links, the copyright and the contact), Support A³ when it leads
// somewhere, the A³ System card (the umbrella's version, then each pin in two columns), the licences last.
void SystemAboutView::resized()
{
    const auto t = layout::tokens (*this, systemRow (*this, (int) probe.programs().size()));
    layout::Stack stack (getLocalBounds().reduced (t.margin), t, cards);
    for (auto* label : { &aboutHeading, &supportHeading, &systemHeading, &licenceHeading })
        label->setFont (juce::FontOptions (t.text));

    auto area = stack.card (layout::cardHeight (t, 3)).reduced (t.padding);
    aboutHeading.setBounds (area.removeFromTop (t.heading));
    taglineArea = area.removeFromTop (t.row);
    placeLinks (links, area.removeFromTop (t.row), t);
    contactArea = area.removeFromTop (t.row);

    const auto& support = probe.support();
    bitcoinArea = {};
    if (! support.isEmpty())
    {
        area = stack.card (layout::cardHeight (t, supportLines (support))).reduced (t.padding);
        supportHeading.setBounds (area.removeFromTop (t.heading));
        for (auto* link : supportLinks)
        {
            juce::OwnedArray<juce::HyperlinkButton> one;
            one.add (link);
            placeLinks (one, area.removeFromTop (t.row), t);
            one.clearQuick (false);
        }
        if (support.bitcoin.isNotEmpty())
            bitcoinArea = area.removeFromTop (t.row);
    }

    area = stack.card (layout::cardHeight (t, systemLines (pins))).reduced (t.padding);
    auto headingLine = area.removeFromTop (t.heading);
    versionArea = headingLine.removeFromRight (headingLine.getWidth() / 2);
    systemHeading.setBounds (headingLine);
    systemAreas.clear();
    const int lines = systemLines (pins);
    const int columnWidth = (area.getWidth() - (systemColumns - 1) * 2 * t.unit) / systemColumns;
    for (int i = 0; i < (pins.found ? (int) pins.rows.size() : 1); ++i)
    {
        const int column = i / lines, line = i % lines;
        systemAreas.push_back ({ area.getX() + column * (columnWidth + 2 * t.unit), area.getY() + line * t.row, columnWidth, t.row });
    }

    area = stack.card (layout::cardHeight (t, 1)).reduced (t.padding);
    licenceHeading.setBounds (area.removeFromTop (t.heading));
    licenceArea = area.removeFromTop (t.row);
}

void SystemAboutView::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    const auto t = layout::tokens (*this, systemRow (*this, (int) probe.programs().size()));
    paintCards (g, *this, t);
    g.setFont (juce::FontOptions (t.text));
    g.setColour (palette::text);
    g.drawFittedText (about::tagline(), taglineArea, juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (palette::textMuted);
    g.drawFittedText (about::copyrightLine() + dot + about::contactAddress(), contactArea, juce::Justification::centredLeft, 1, 0.7f);
    g.drawFittedText (about::licenceLine(), licenceArea, juce::Justification::centredLeft, 1, 0.6f);
    if (! bitcoinArea.isEmpty())
        g.drawFittedText ("Bitcoin " + probe.support().bitcoin, bitcoinArea, juce::Justification::centredLeft, 1, 0.6f);

    g.setColour (palette::text);
    g.drawText (pins.version, versionArea, juce::Justification::centredRight, true);
    if (! pins.found)
    {
        g.setColour (palette::textMuted);
        if (! systemAreas.empty())
            g.drawText (probe.snapshot().polled ? "no a3-system checkout on this machine" : "not read yet", systemAreas.front(),
                        juce::Justification::centredLeft, true);
        return;
    }
    for (size_t i = 0; i < pins.rows.size() && i < systemAreas.size(); ++i)
    {
        const auto& row = pins.rows[i];
        g.setColour (palette::textMuted);
        g.drawText (row.name, systemAreas[i], juce::Justification::centredLeft, true);
        g.setColour (row.tag.isNotEmpty() ? palette::text : palette::textMuted);
        g.drawText (checkout::tagText (row), systemAreas[i], juce::Justification::centredRight, true);
    }
}
}
