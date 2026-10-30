// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CalibrationPage.h"

namespace a3app
{
namespace
{
const char* zoneName (TrimZone zone)
{
    switch (zone)
    {
        case TrimZone::main: return "Main";
        case TrimZone::booth: return "Booth";
        case TrimZone::phones: return "Phones";
        case TrimZone::rec: return "Rec";
    }
    return "";
}

std::optional<float> trimOf (const AppConfig& config, TrimZone zone)
{
    const auto& trims = config.engine.trims;
    switch (zone)
    {
        case TrimZone::main: return trims.main;
        case TrimZone::booth: return trims.booth;
        case TrimZone::phones: return trims.phones;
        case TrimZone::rec: return trims.rec;
    }
    return std::nullopt;
}

// Inside the window: a desktop window of class a3-core would be tiled beside it by i3 (for_window [class="a3-core"]).
void askWithBox (juce::Component* window, const juce::String& message, std::function<void()> onYes)
{
    const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle ("Change trim").withMessage (message)
                             .withButton ("Change").withButton ("Cancel").withParentComponent (window);
    juce::AlertWindow::showAsync (options, [onYes = std::move (onYes)] (int result) { if (result == 1 && onYes) onYes(); });
}
}

juce::String CalibrationPage::describe (std::optional<float> db)
{
    return db ? juce::String (*db, 1) + " dB" : juce::String ("not set (zone stays closed)");
}

CalibrationPage::CalibrationPage (HostView& v, Confirm c)
    : view (v), confirm (c ? std::move (c) : Confirm ([this] (const juce::String& text, std::function<void()> onYes)
                                                             { askWithBox (look::dialogParent (*this), text, std::move (onYes)); })),
      filter (v)
{
    for (const auto zone : { TrimZone::main, TrimZone::booth, TrimZone::phones, TrimZone::rec })
        rows.push_back (std::make_unique<Row> (zone));
    look::caption (title);
    look::failure (message);
    title.setText ("CALIBRATION", juce::dontSendNotification);
    for (auto* child : std::initializer_list<juce::Component*> { &title, &message, &filter })
        addAndMakeVisible (child);
    for (auto& rowPtr : rows)
    {
        auto& row = *rowPtr;
        row.name.setText (zoneName (row.zone), juce::dontSendNotification);
        look::caption (row.name);
        look::value (row.current);
        row.proposal.setRange (minTrimDb, maxTrimDb, 0.1);
        row.proposal.setNumDecimalPlacesToDisplay (1);
        row.proposal.setTextValueSuffix (" dB");
        // Starts at what the zone has now (the lowest for a zone without a trim), so it never reads as a pending change.
        row.proposal.setValue (trimOf (view.config(), row.zone).value_or (minTrimDb), juce::dontSendNotification);
        row.set.onClick = [this, zone = row.zone] { proposeTrim (zone, (float) rows[(size_t) zone]->proposal.getValue()); };
        for (auto* child : std::initializer_list<juce::Component*> { &row.name, &row.current, &row.proposal, &row.set })
            addAndMakeVisible (child);
    }
    refresh();
}

CalibrationPage::~CalibrationPage() = default;

void CalibrationPage::proposeTrim (TrimZone zone, float db)
{
    const auto config = view.config();
    juce::String error;
    const auto edited = withTrim (config, zone, db, error);
    look::say (message, edited ? juce::String() : error);
    if (! edited)
        return;
    const auto text = juce::String (zoneName (zone)) + " trim: " + describe (trimOf (config, zone)) + "  ->  "
                      + juce::String (db, 1) + " dB.";
    confirm (text, [self = juce::Component::SafePointer<CalibrationPage> (this), zone, db]
    {
        if (self == nullptr)
            return;   // the window closed while the dialog was open
        auto* page = self.getComponent();
        auto& view = page->view;
        auto& message = page->message;
        juce::String problem;
        if (const auto next = withTrim (view.config(), zone, db, problem))
            view.edit (*next);
        else
            look::say (message, problem);
        page->rows[(size_t) zone]->proposal.setValue (db, juce::dontSendNotification);
        page->refresh();
    });
}

void CalibrationPage::refresh()
{
    const auto config = view.config();
    for (auto& row : rows)
        look::say (row->current, describe (trimOf (config, row->zone)));
    filter.refresh();
}

void CalibrationPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    paintCards (g, *this, layout::tokens (*this));
}

// Two cards (layout::Stack): the trims (heading, a line per zone, the message) and FILTER.
void CalibrationPage::resized()
{
    const auto t = layout::tokens (*this);
    layout::Stack stack (getLocalBounds().reduced (t.margin), t, cards);
    auto area = stack.card (layout::cardHeight (t, (int) rows.size() + 1)).reduced (t.padding);
    for (auto* label : { &title, &message })
        label->setFont (juce::FontOptions (t.text));
    title.setBounds (area.removeFromTop (t.heading));
    for (auto& rowPtr : rows)
    {
        auto& r = *rowPtr;
        auto line = area.removeFromTop (t.row);
        for (auto* label : { &r.name, &r.current })
            label->setFont (juce::FontOptions (t.text));
        r.name.setBounds (line.removeFromLeft (line.getWidth() / 8));
        r.current.setBounds (line.removeFromLeft (line.getWidth() * 2 / 7));
        r.set.setBounds (layout::control (line.removeFromRight (line.getWidth() / 5), t));
        line.removeFromRight (t.unit);
        r.proposal.setBounds (layout::control (line, t));
    }
    message.setBounds (area);
    filter.setTokens (t);
    filter.setBounds (stack.card (layout::cardHeight (t, FilterCard::lines)).reduced (t.padding));
}
}
