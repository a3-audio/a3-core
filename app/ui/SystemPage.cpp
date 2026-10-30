// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemPage.h"

namespace a3app
{
namespace
{
const juce::String dot = juce::String::fromUTF8 (" · ");
constexpr int publishedColumns = 2;   // PUBLISHED's repositories run in two columns

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
        link->setBounds (line.removeFromLeft (width));
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

int publishedLines (const checkout::Facts& pins)
{
    return pins.found ? ((int) pins.rows.size() + publishedColumns - 1) / publishedColumns : 1;
}

int supportLines (const about::Support& support) { return (support.paypal.isNotEmpty() ? 1 : 0) + (support.kofi.isNotEmpty() ? 1 : 0) + (support.bitcoin.isNotEmpty() ? 1 : 0); }
}

// -- RIG ------------------------------------------------------------------------------------------------------------

struct SystemRigView::ProgramRow
{
    rig::Program program;
    rig::Row row;
    juce::Label name;
    juce::TextButton startStop { "Start" }, restart { "Restart" };
    juce::Rectangle<int> first, second, light;
};

// ENTITIES' scrolled rows: each its light, name and state with its keys on the first line, where it runs and what it
// runs on a compact second line, muted; a hairline between two.
class SystemRigView::List final : public juce::Component
{
public:
    explicit List (const std::vector<std::unique_ptr<ProgramRow>>& r) : rows (r) {}

    int layOut (int width, const layout::Tokens& t)
    {
        textHeight = t.text;
        int y = 0;
        bottoms.clear();
        for (auto& r : rows)
        {
            auto area = juce::Rectangle<int> (0, y, width, t.row + layout::compactLine (t));
            y = area.getBottom() + t.unit;
            bottoms.push_back (area.getBottom() + t.unit / 2);
            auto first = area.removeFromTop (t.row);
            r->second = area;
            const int keyWidth = width / 6;
            r->restart.setBounds (layout::control (first.removeFromRight (keyWidth), t));
            first.removeFromRight (t.unit);
            r->startStop.setBounds (layout::control (first.removeFromRight (keyWidth), t));
            first.removeFromRight (t.unit);
            const int lightSide = juce::roundToInt (t.text * 0.6f);
            r->light = first.withWidth (lightSide);
            const auto font = juce::Font (juce::FontOptions (t.text));
            const int nameWidth = lightSide + t.unit + juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, r->program.label)) + t.unit;
            r->name.setFont (font);
            r->name.setBorderSize ({ 0, lightSide + t.unit, 0, 0 });   // the light sits in the name's room, left of the word
            r->name.setBounds (first.removeFromLeft (nameWidth));
            r->first = first.withTrimmedLeft (t.unit);
            r->second = r->second.withTrimmedLeft (lightSide + t.unit);
        }
        return juce::jmax (0, y - t.unit);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (juce::FontOptions (textHeight));
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto& r = rows[i];
            g.setColour (lightColour (r->row.light));
            g.fillEllipse (r->light.toFloat().withSizeKeepingCentre ((float) r->light.getWidth(), (float) r->light.getWidth()));
            const bool news = r->row.light == rig::Light::degraded || r->row.light == rig::Light::failed;
            g.setColour (news ? lightColour (r->row.light) : palette::text);
            g.drawFittedText (r->row.state, r->first, juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (palette::textMuted);
            g.drawFittedText (details (r->row), r->second, juce::Justification::centredLeft, 1, 0.7f);
            if (i + 1 < rows.size() && i < bottoms.size())
            {
                g.setColour (palette::face);
                g.fillRect (0, bottoms[i], getWidth(), 1);
            }
        }
    }

private:
    const std::vector<std::unique_ptr<ProgramRow>>& rows;
    std::vector<int> bottoms;
    float textHeight = 12.0f;
};

SystemRigView::SystemRigView (SystemProbe& p, SystemConfirm c)
    : probe (p), confirm (c ? std::move (c) : SystemConfirm ([this] (const juce::String& q, const juce::String& d, std::function<void()> yes)
                                                               { askInTheWindow (*this, q, d, std::move (yes)); }))
{
    setHeading (heading, "MAIN" + dot + juce::SystemStats::getComputerName());
    setHeading (entitiesHeading, "ENTITIES");
    addAndMakeVisible (heading);
    addAndMakeVisible (entitiesHeading);
    list = std::make_unique<List> (rows);
    for (const auto& program : probe.programs())
    {
        auto made = std::make_unique<ProgramRow>();
        made->program = program;
        look::value (made->name);
        look::say (made->name, program.label);
        auto* raw = made.get();
        made->startStop.onClick = [this, raw] {
            const auto& program = raw->program;
            if (! raw->row.up)
                return void (probe.run ("Start " + program.label, rig::start (program)));
            ask ("Stop " + program.label + "?", {},
                 [this, program] { probe.run ("Stop " + program.label, rig::stop (program)); });
        };
        made->restart.onClick = [this, raw] {
            const auto program = raw->program;
            ask ("Restart " + program.label + "?", {},
                 [this, program] { probe.run ("Restart " + program.label, rig::restart (program)); });
        };
        list->addAndMakeVisible (made->name);
        list->addChildComponent (made->startStop);
        list->addChildComponent (made->restart);
        rows.push_back (std::move (made));
    }
    startAll.onClick = [this] { probe.run ("Start all", rig::startAll (programsHere())); };
    stopAll.onClick = [this] {
        ask ("Stop all?", {},
             [this] { probe.run ("Stop all", rig::stopAll (programsHere())); });
    };
    addAndMakeVisible (startAll);
    addAndMakeVisible (stopAll);
    scroller.setViewedComponent (list.get(), false);
    scroller.setScrollBarsShown (false, false);   // a finger scrolls it
    addAndMakeVisible (scroller);
    refresh();
}

SystemRigView::~SystemRigView()
{
    scroller.setViewedComponent (nullptr, false);
}

juce::Component& SystemRigView::programList() { return *list; }

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
        r->name.setColour (juce::Label::textColourId, r->row.light == rig::Light::missing ? palette::textMuted : palette::text);
    }
    const bool any = ! programsHere().empty();
    startAll.setEnabled (any && ! shown.busy);
    stopAll.setEnabled (any && ! shown.busy);
    repaint();
    list->repaint();
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

// Two cards (layout::Stack): MAIN (its heading, Start all and Stop all with the plan's word beside them) and ENTITIES in
// the rest (its heading, then the programs' rows, scrolled inside the card).
void SystemRigView::resized()
{
    const auto t = layout::tokens (*this);
    layout::Stack stack (getLocalBounds().reduced (t.margin), t, cards);
    auto area = stack.card (layout::cardHeight (t, 1)).reduced (t.padding);
    for (auto* label : { &heading, &entitiesHeading })
        label->setFont (juce::FontOptions (t.text));
    heading.setBounds (area.removeFromTop (t.heading));
    const int keyWidth = area.getWidth() / 6;
    startAll.setBounds (layout::control (area.removeFromLeft (keyWidth), t));
    area.removeFromLeft (t.unit);
    stopAll.setBounds (layout::control (area.removeFromLeft (keyWidth), t));
    area.removeFromLeft (2 * t.unit);
    planArea = area;
    area = stack.rest().reduced (t.padding);
    entitiesHeading.setBounds (area.removeFromTop (t.heading));
    scroller.setBounds (area);
    list->setSize (area.getWidth(), list->layOut (area.getWidth(), t));
}

void SystemRigView::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    const auto t = layout::tokens (*this);
    paintCards (g, *this, t);
    g.setFont (juce::FontOptions (t.text));
    g.setColour (shown.planLine.contains ("failed") || shown.planLine.contains ("skipped") ? palette::caution : palette::textMuted);
    g.drawFittedText (shown.planLine, planArea, juce::Justification::centredLeft, 1, 0.8f);
}

// -- ABOUT ----------------------------------------------------------------------------------------------------------

SystemAboutView::SystemAboutView (SystemProbe& p) : probe (p)
{
    setHeading (aboutHeading, juce::String::fromUTF8 ("A³"));
    setHeading (supportHeading, juce::String::fromUTF8 ("SUPPORT A³"));
    setHeading (publishedHeading, "PUBLISHED");
    setHeading (licenceHeading, "LICENCES");
    for (auto* label : { &aboutHeading, &publishedHeading, &licenceHeading })
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

SystemAboutView::~SystemAboutView() = default;

// A link per repository that names where it is published; its name as text where none is known.
void SystemAboutView::makePublished()
{
    repositories.clear();
    for (const auto& row : pins.rows)
    {
        if (row.url.startsWith ("https://"))
            addAndMakeVisible (repositories.add (makeLink (row.name, row.url).release()));
        else
        {
            auto* name = new juce::Label();
            look::caption (*name);
            look::say (*name, row.name);
            addAndMakeVisible (repositories.add (name));
        }
    }
}

void SystemAboutView::refresh()
{
    const auto snapshot = probe.snapshot();
    if (snapshot.generation == seen && seen != 0)
        return;
    seen = snapshot.generation;
    if (snapshot.checkout == pins)
        return;
    pins = snapshot.checkout;
    makePublished();
    resized();
    repaint();
}

// Cards (layout::Stack), every line compact and none empty: the project (its line, its links, the copyright and the
// contact), Support A³ when it leads somewhere, PUBLISHED (the umbrella's version, then each repository in two columns,
// its name a link, its tag beside it), the licences last.
void SystemAboutView::resized()
{
    const auto t = layout::tokens (*this);
    const int line = layout::compactLine (t);
    const auto height = [&t, line] (int lines) { return 2 * t.padding + t.heading + lines * line; };
    layout::Stack stack (getLocalBounds().reduced (t.margin), t, cards);
    for (auto* label : { &aboutHeading, &supportHeading, &publishedHeading, &licenceHeading })
        label->setFont (juce::FontOptions (t.text));

    auto area = stack.card (height (3)).reduced (t.padding);
    aboutHeading.setBounds (area.removeFromTop (t.heading));
    taglineArea = area.removeFromTop (line);
    placeLinks (links, area.removeFromTop (line), t);
    contactArea = area.removeFromTop (line);

    const auto& support = probe.support();
    bitcoinArea = {};
    if (! support.isEmpty())
    {
        area = stack.card (height (supportLines (support))).reduced (t.padding);
        supportHeading.setBounds (area.removeFromTop (t.heading));
        for (auto* link : supportLinks)
        {
            juce::OwnedArray<juce::HyperlinkButton> one;
            one.add (link);
            placeLinks (one, area.removeFromTop (line), t);
            one.clearQuick (false);
        }
        if (support.bitcoin.isNotEmpty())
            bitcoinArea = area.removeFromTop (line);
    }

    area = stack.card (height (publishedLines (pins))).reduced (t.padding);
    auto headingLine = area.removeFromTop (t.heading);
    versionArea = headingLine.removeFromRight (headingLine.getWidth() / 2);
    publishedHeading.setBounds (headingLine);
    publishedAreas.clear();
    const int lines = publishedLines (pins);
    const int columnGap = 2 * t.unit;
    const int columnWidth = (area.getWidth() - (publishedColumns - 1) * columnGap) / publishedColumns;
    for (int i = 0; i < (pins.found ? (int) pins.rows.size() : 1); ++i)
    {
        const int column = i / lines, row = i % lines;
        publishedAreas.push_back ({ area.getX() + column * (columnWidth + columnGap), area.getY() + row * line, columnWidth, line });
    }
    const auto font = linkFont (t.text);
    for (int i = 0; i < repositories.size() && i < (int) publishedAreas.size(); ++i)
    {
        auto* name = repositories[i];
        const auto text = pins.rows[(size_t) i].name;
        if (auto* link = dynamic_cast<juce::HyperlinkButton*> (name))
        {
            link->setColour (juce::HyperlinkButton::textColourId, palette::text);
            link->setFont (font, false, juce::Justification::centredLeft);
        }
        else if (auto* label = dynamic_cast<juce::Label*> (name))
        {
            label->setFont (juce::FontOptions (t.text));
            label->setBorderSize ({});
        }
        const int width = juce::jmin (publishedAreas[(size_t) i].getWidth() / 2,
                                      juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, text)) + t.unit);
        name->setBounds (publishedAreas[(size_t) i].withWidth (width));
    }

    area = stack.card (height (1)).reduced (t.padding);
    licenceHeading.setBounds (area.removeFromTop (t.heading));
    licenceArea = area.removeFromTop (line);
}

void SystemAboutView::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    const auto t = layout::tokens (*this);
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
        if (! publishedAreas.empty())
            g.drawText (probe.snapshot().polled ? "no a3-system checkout on this machine" : "not read yet", publishedAreas.front(),
                        juce::Justification::centredLeft, true);
        return;
    }
    for (size_t i = 0; i < pins.rows.size() && i < publishedAreas.size(); ++i)
    {
        const auto& row = pins.rows[i];
        g.setColour (row.tag.isNotEmpty() ? palette::text : palette::textMuted);
        g.drawText (checkout::tagText (row), publishedAreas[i], juce::Justification::centredRight, true);
    }
}
}
