// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemPage.h"

#include "A3Assets.h"
#include <algorithm>

namespace a3app
{
namespace
{
const juce::String dot = juce::String::fromUTF8 (" · ");

juce::Colour lightColour (rig::Light light)
{
    switch (light)
    {
        case rig::Light::running: return palette::running;
        case rig::Light::starting:
        case rig::Light::degraded: return palette::caution;
        case rig::Light::failed: return palette::danger;
        case rig::Light::idle: return palette::text;   // not meant to run, and it does not: fine, no hue
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

}

// -- RIG ------------------------------------------------------------------------------------------------------------

struct SystemRigView::ProgramRow
{
    rig::Program program;
    rig::Row row;
    juce::Label name;
    juce::TextButton startStop { "Start" }, restart { "Restart" };
    juce::Rectangle<int> first, second, light;
    std::vector<juce::Rectangle<int>> serviceAreas;   // the a3-core row's: a compact line per service (open only)
    juce::Rectangle<int> summaryArea;                 // its collapsed line, the disclosure key's tap area
    bool servicesOpen = false;                        // kept for the window's session: rows live as long as the page
    bool shown = true;                                 // false: inside the a3-core card (the beat analyzer)
};

// ENTITIES' scrolled rows: each its light, name and state with its keys on the first line, where it runs and what it
// runs on a compact second line, muted; a hairline between two.
class SystemRigView::List final : public juce::Component
{
public:
    explicit List (const std::vector<std::unique_ptr<ProgramRow>>& r) : rows (r) {}
    std::function<void()> onToggle;

    // The disclosure key: a tap on a card's summary line opens or closes its services.
    void mouseUp (const juce::MouseEvent& e) override
    {
        for (auto& r : rows)
            if (r->shown && ! r->row.services.empty() && r->summaryArea.contains (e.getPosition()) && e.getDistanceFromDragStart() < 8)
            {
                r->servicesOpen = ! r->servicesOpen;
                if (onToggle)
                    onToggle();
                return;
            }
    }

    int layOut (int width, const layout::Tokens& t)
    {
        textHeight = t.text;
        int y = 0;
        bottoms.clear();
        for (auto& r : rows)
        {
            r->shown = ! r->row.insideCore;
            for (auto* part : std::initializer_list<juce::Component*> { &r->name })
                part->setVisible (r->shown);
            if (! r->shown)
            {
                r->startStop.setVisible (false);
                r->restart.setVisible (false);
                bottoms.push_back (-1);
                continue;
            }
            const int line = layout::compactLine (t);
            const bool hasServices = ! r->row.services.empty();
            const int services = hasServices && r->servicesOpen ? (int) r->row.services.size() : 0;
            auto area = juce::Rectangle<int> (0, y, width, t.row + line * (1 + (hasServices ? 1 : 0) + services));
            y = area.getBottom() + t.unit;
            bottoms.push_back (area.getBottom() + t.unit / 2);
            auto first = area.removeFromTop (t.row);
            r->second = area.removeFromTop (line);
            r->serviceAreas.clear();
            r->summaryArea = hasServices ? area.removeFromTop (line) : juce::Rectangle<int>();
            for (int i = 0; i < services; ++i)
                r->serviceAreas.push_back (area.removeFromTop (line));
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
            for (auto& serviceArea : r->serviceAreas)
                serviceArea = serviceArea.withTrimmedLeft (lightSide + t.unit);
            r->summaryArea = r->summaryArea.withTrimmedLeft (lightSide + t.unit);
        }
        return juce::jmax (0, y - t.unit);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (juce::FontOptions (textHeight));
        int lastShown = -1;
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i]->shown)
                lastShown = (int) i;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto& r = rows[i];
            if (! r->shown)
                continue;
            g.setColour (lightColour (r->row.light));
            g.fillEllipse (r->light.toFloat().withSizeKeepingCentre ((float) r->light.getWidth(), (float) r->light.getWidth()));
            const bool news = r->row.light == rig::Light::degraded || r->row.light == rig::Light::failed;
            g.setColour (news ? lightColour (r->row.light) : palette::text);
            g.drawFittedText (r->row.state, r->first, juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (palette::textMuted);
            g.drawFittedText (details (r->row), r->second, juce::Justification::centredLeft, 1, 0.7f);
            // The a3-core card's summary line with its disclosure key; red or grey by the worst unit, plain when all is well.
            if (! r->row.services.empty())
            {
                const auto summary = rig::summarize (r->row.services);
                const bool wrong = summary.worst != rig::Light::running;
                auto area = r->summaryArea;
                g.setColour (palette::textMuted);
                g.drawFittedText (r->servicesOpen ? juce::String::fromUTF8 ("▾") : juce::String::fromUTF8 ("▸"),
                                  area.removeFromLeft (area.getHeight()), juce::Justification::centred, 1, 0.7f);
                g.setColour (wrong ? lightColour (summary.worst) : palette::text);
                g.drawFittedText (summary.text, area, juce::Justification::centredLeft, 1, 0.7f);
            }
            // The a3-core card's services (open): a small light, the unit's name, its state.
            for (size_t s = 0; s < r->row.services.size() && s < r->serviceAreas.size(); ++s)
            {
                const auto& service = r->row.services[s];
                auto area = r->serviceAreas[s];
                const auto side = (float) area.getHeight() * 0.35f;
                g.setColour (lightColour (service.light));
                g.fillEllipse (area.removeFromLeft (area.getHeight()).toFloat().withSizeKeepingCentre (side, side));
                g.setColour (service.light == rig::Light::failed ? palette::danger : palette::text);
                g.drawFittedText (service.name, area.removeFromLeft (area.getWidth() / 2), juce::Justification::centredLeft, 1, 0.7f);
                g.setColour (lightColour (service.light) == palette::textMuted ? palette::textMuted : lightColour (service.light));
                g.drawFittedText (service.state, area, juce::Justification::centredLeft, 1, 0.7f);
            }
            if ((int) i != lastShown && i < bottoms.size() && bottoms[i] >= 0)
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
    list->onToggle = [this] { resized(); list->repaint(); };
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
    juce::String laidOut;   // what the list's layout depends on: the services' count, which rows sit inside a3-core
    for (auto& r : rows)
    {
        r->row = rig::describe (r->program, shown.facts);
        laidOut << (int) r->row.services.size() << (r->row.insideCore ? "i" : "-") << (r->servicesOpen ? "o" : "c");
        const bool acts = r->row.here && r->row.installed && ! r->row.insideCore;
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
    if (laidOut != listLayout)
    {
        listLayout = laidOut;
        resized();
    }
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

juce::StringArray SystemRigView::serviceLines() const
{
    juce::StringArray lines;
    for (const auto& r : rows)
        for (const auto& service : r->row.services)
            lines.add (service.name + "  " + service.state);
    return lines;
}

juce::String SystemRigView::serviceSummary() const
{
    for (const auto& r : rows)
        if (! r->row.services.empty())
            return rig::summarize (r->row.services).text;
    return {};
}

bool SystemRigView::servicesOpen() const
{
    for (const auto& r : rows)
        if (! r->row.services.empty())
            return r->servicesOpen;
    return false;
}

void SystemRigView::toggleServices()
{
    for (auto& r : rows)
        if (! r->row.services.empty())
        {
            r->servicesOpen = ! r->servicesOpen;
            resized();
            list->repaint();
            return;
        }
}

bool SystemRigView::hasOwnRow (const juce::String& label) const
{
    const auto* r = rowOf (label);
    return r != nullptr && ! r->row.insideCore;
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
    // The real logo, its colour the page's text colour; the big letters stand in when JUCE cannot read the SVG.
    logoImage = juce::Drawable::createFromImageData (a3assets::a3logo_svg, (size_t) a3assets::a3logo_svgSize);
    if (logoImage != nullptr)
        logoImage->replaceColour (juce::Colour (0xfffffff3), palette::text);
    setHeading (publishedHeading, "PUBLISHED");
    addAndMakeVisible (publishedHeading);
    for (const auto& link : about::links())
        addAndMakeVisible (links.add (makeLink (link.label, link.url)));
    refresh();
}

SystemAboutView::~SystemAboutView() = default;

// The tree of PUBLISHED: a repository per line, a3-motion's ui and the firmwares as the children of the repository they
// belong to ("a3-motion/ui", "a3-mixer firmware"), the connectors ahead of each name.
void SystemAboutView::makePublished()
{
    repositories.clear();
    tree.clear();
    const auto parentOf = [] (const juce::String& name)
    {
        return name.contains ("/") ? name.upToFirstOccurrenceOf ("/", false, false) : name.contains (" ") ? name.upToFirstOccurrenceOf (" ", false, false) : juce::String();
    };
    const auto childName = [] (const juce::String& name, const juce::String& parent) { return name.substring (parent.length() + 1); };
    std::vector<size_t> tops;
    for (size_t i = 0; i < pins.rows.size(); ++i)
    {
        const auto parent = parentOf (pins.rows[i].name);
        const bool orphan = parent.isNotEmpty() && std::none_of (pins.rows.begin(), pins.rows.end(), [&] (const auto& r) { return r.name == parent; });
        if (parent.isEmpty() || orphan)
            tops.push_back (i);
    }
    const juce::String branch = juce::String::fromUTF8 ("├─ "), corner = juce::String::fromUTF8 ("└─ "),
                       bar = juce::String::fromUTF8 ("│  "), blank = "   ";
    for (size_t k = 0; k < tops.size(); ++k)
    {
        const bool lastTop = k + 1 == tops.size();
        const auto& top = pins.rows[tops[k]];
        tree.push_back ({ top, top.name, (lastTop ? corner : branch) });
        std::vector<size_t> children;
        for (size_t i = 0; i < pins.rows.size(); ++i)
            if (i != tops[k] && parentOf (pins.rows[i].name) == top.name)
                children.push_back (i);
        for (size_t c = 0; c < children.size(); ++c)
            tree.push_back ({ pins.rows[children[c]], childName (pins.rows[children[c]].name, top.name),
                              (lastTop ? blank : bar) + (c + 1 == children.size() ? corner : branch) });
    }
    for (const auto& line : tree)
    {
        if (line.row.url.startsWith ("https://"))
            addAndMakeVisible (repositories.add (makeLink (line.name, line.row.url).release()));
        else
            repositories.add (nullptr);   // no url: its name is painted
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

// The page as the website has it (2026-10-31): the logo large and centred in the top third with its line under it; in
// the space between ONE card, PUBLISHED (the system version, the repositories as a tree, the licences last); at the
// bottom the links on one line and the copyright under them. The card keeps the grid's margin, padding and gap.
void SystemAboutView::resized()
{
    const auto t = layout::tokens (*this);
    const int line = layout::compactLine (t);
    const auto page = getLocalBounds().reduced (t.margin);
    const int third = getHeight() / 3;
    publishedHeading.setFont (juce::FontOptions (t.text));

    auto top = juce::Rectangle<int> (page.getX(), page.getY(), page.getWidth(), third - page.getY());
    taglineArea = top.removeFromBottom (line);
    logoArea = top.reduced (0, t.unit);

    auto bottom = page;
    contactArea = bottom.removeFromBottom (line);
    bottom.removeFromBottom (t.unit);
    auto linkLine = bottom.removeFromBottom (line);
    const int linksTop = linkLine.getY();
    const auto font = linkFont (t.text);
    std::vector<int> widths;
    int total = 0;
    for (auto* link : links)
    {
        widths.push_back (juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, link->getButtonText())) + t.unit);
        total += widths.back() + t.unit;
    }
    linkLine.removeFromLeft (juce::jmax (0, (linkLine.getWidth() - (total - t.unit)) / 2));
    for (int i = 0; i < links.size(); ++i)
    {
        links[i]->setColour (juce::HyperlinkButton::textColourId, palette::text);
        links[i]->setFont (font, false, juce::Justification::centred);
        links[i]->setBounds (linkLine.removeFromLeft (widths[(size_t) i]));
        linkLine.removeFromLeft (t.unit);
    }

    const auto middle = juce::Rectangle<int> (page.getX(), third, page.getWidth(), linksTop - third - t.gap);
    layout::Stack stack (middle, t, cards);
    const int rowsInCard = 1 + juce::jmax (1, (int) tree.size());   // the system, the tree (or its one message)
    auto area = stack.card (2 * t.padding + t.heading + rowsInCard * line).reduced (t.padding);
    publishedHeading.setBounds (area.removeFromTop (t.heading));
    versionArea = area.removeFromTop (line);
    for (auto& entry : tree)
    {
        entry.area = area.removeFromTop (line);
        entry.nameArea = {};
    }
    treeMessageArea = area.getHeight() >= line && tree.empty() ? area.removeFromTop (line) : juce::Rectangle<int>();

    // Three columns: the name with its connector, the version, the licences; the version and licence columns as wide as
    // their longest text, the licences from the card's right edge.
    const juce::FontOptions text (t.text);
    int versionWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (text, pins.version)),
        licenceWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (text, pins.licence));
    for (const auto& entry : tree)
    {
        versionWidth = juce::jmax (versionWidth, juce::roundToInt (juce::GlyphArrangement::getStringWidth (text, checkout::tagText (entry.row))));
        licenceWidth = juce::jmax (licenceWidth, juce::roundToInt (juce::GlyphArrangement::getStringWidth (text, entry.row.licences)));
    }
    licenceWidth += licenceWidth > 0 ? t.unit : 0;   // a little room, or drawText cuts the last glyph to an ellipsis
    versionWidth += t.unit;
    const int columnGap = 2 * t.unit;
    auto columns = versionArea;
    licenceColumn = columns.removeFromRight (licenceWidth);
    if (licenceWidth > 0)
        columns.removeFromRight (columnGap);
    versionColumn = columns.removeFromRight (versionWidth);
    for (auto& entry : tree)
    {
        entry.licenceArea = entry.area.withLeft (licenceColumn.getX()).withRight (licenceColumn.getRight());
        entry.versionArea = entry.area.withLeft (versionColumn.getX()).withRight (versionColumn.getRight());
    }

    for (size_t i = 0; i < tree.size() && i < (size_t) repositories.size(); ++i)
    {
        auto& entry = tree[i];
        const int prefixWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::FontOptions (t.text), entry.prefix));
        auto nameBox = entry.area.withTrimmedLeft (prefixWidth).withRight (versionColumn.getX() - columnGap);
        const int width = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, entry.name)) + t.unit;
        entry.nameArea = nameBox.withWidth (juce::jmin (width, nameBox.getWidth() / 2));
        if (auto* link = dynamic_cast<juce::HyperlinkButton*> (repositories[(int) i]))
        {
            link->setColour (juce::HyperlinkButton::textColourId, palette::text);
            link->setFont (font, false, juce::Justification::centredLeft);
            link->setBounds (entry.nameArea);
        }
    }
}

void SystemAboutView::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    const auto t = layout::tokens (*this);
    paintCards (g, *this, t);
    g.setFont (juce::FontOptions (t.text));

    if (logoImage != nullptr)
        logoImage->drawWithin (g, logoArea.toFloat(), juce::RectanglePlacement::centred, 1.0f);
    else
    {
        g.setColour (palette::text);
        g.setFont (juce::FontOptions ((float) logoArea.getHeight() * 0.8f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 ("A³"), logoArea, juce::Justification::centred, false);
        g.setFont (juce::FontOptions (t.text));
    }
    g.setColour (palette::text);
    g.drawFittedText (about::tagline(), taglineArea, juce::Justification::centred, 1, 0.8f);
    g.setColour (palette::textMuted);
    g.drawFittedText (about::copyrightLine() + dot + about::contactAddress(), contactArea, juce::Justification::centred, 1, 0.7f);

    g.setColour (palette::text);
    g.drawText (juce::String::fromUTF8 ("A³ System"), versionArea, juce::Justification::centredLeft, true);
    g.drawText (pins.version, versionColumn.withY (versionArea.getY()).withHeight (versionArea.getHeight()), juce::Justification::centredRight, true);
    g.setColour (palette::textMuted);
    g.drawText (pins.licence, licenceColumn.withY (versionArea.getY()).withHeight (versionArea.getHeight()), juce::Justification::centredLeft, true);
    if (! pins.found)
    {
        g.setColour (palette::textMuted);
        g.drawText (probe.snapshot().polled ? "no a3-system checkout on this machine" : "not read yet", treeMessageArea,
                    juce::Justification::centredLeft, true);
        return;
    }
    for (size_t i = 0; i < tree.size(); ++i)
    {
        const auto& entry = tree[i];
        g.setColour (palette::textMuted);
        g.drawText (entry.prefix, entry.area, juce::Justification::centredLeft, false);
        if (i < (size_t) repositories.size() && repositories[(int) i] == nullptr)
        {
            g.setColour (palette::textMuted);
            g.drawText (entry.name, entry.nameArea, juce::Justification::centredLeft, true);
        }
        g.setColour (entry.row.tag.isNotEmpty() ? palette::text : palette::textMuted);
        g.drawText (checkout::tagText (entry.row), entry.versionArea, juce::Justification::centredRight, true);
        g.setColour (palette::textMuted);
        g.drawText (entry.row.licences, entry.licenceArea, juce::Justification::centredLeft, true);
    }
}
}
