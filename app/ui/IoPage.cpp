// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IoPage.h"
#include <sstream>

namespace a3app
{
namespace
{
constexpr int reloadEveryTicks = 10;   // the window ticks at 20 Hz: the live links are re-read twice a second

juce::String joined (const std::vector<std::string>& names)
{
    juce::StringArray list;
    for (const auto& name : names)
        list.add (juce::String (name));
    return list.joinIntoString (", ");
}

juce::String mappedText (const IoRow& row, bool device)
{
    if (device)
        return row.deviceChannel ? "channel " + juce::String (*row.deviceChannel + 1) : juce::String ("-");
    return joined (row.mapped);
}

juce::String foreignText (const IoRow& row, bool device)
{
    return device || row.foreign.empty() ? juce::String() : "foreign: " + joined (row.foreign);
}

std::vector<std::string> splitPeers (const juce::String& text)
{
    std::vector<std::string> peers;
    for (const auto& piece : juce::StringArray::fromTokens (text, ",\n", ""))
        if (piece.trim().isNotEmpty())
            peers.push_back (piece.trim().toStdString());
    return peers;
}
}

class IoPage::Model final : public juce::ListBoxModel
{
public:
    explicit Model (IoPage& p) : page (p) {}
    int getNumRows() override { return (int) page.portRows.size(); }

    void paintListBoxItem (int index, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (index < 0 || index >= getNumRows())
            return;
        const auto& row = page.portRows[(size_t) index];
        const bool device = page.deviceMode();
        if (selected)
            g.fillAll (palette::lifted);
        g.setFont ((float) height * 0.5f);
        g.setColour (palette::text);
        g.drawText (juce::String (row.port) + (row.input ? "  in" : "  out"), width / 60, 0, width / 4, height, juce::Justification::centredLeft);
        const auto mapped = mappedText (row, device);
        g.setColour (mapped == "-" ? palette::textMuted : palette::text);
        g.drawText (mapped, width / 3, 0, width / 3, height, juce::Justification::centredLeft);
        g.setColour (palette::textMuted);   // foreign: informative, not ours
        g.drawText (foreignText (row, device), width * 2 / 3, 0, width / 3, height, juce::Justification::centredLeft);
    }

    void selectedRowsChanged (int row) override { page.selectPort (row); }

private:
    IoPage& page;
};

IoPage::IoPage (HostView& v)
    : view (v), shown (v.config()), model (std::make_unique<Model> (*this)), list ("io", model.get())
{
    look::caption (title);
    look::caption (portCaption);
    look::caption (linksCaption);
    look::failure (message);
    title.setText ("I/O", juce::dontSendNotification);
    linksCaption.setText ("Direct links the engine makes (one per line: from -> to)", juce::dontSendNotification);
    for (auto* editor : { &portEditor, &linksEditor })
    {
        editor->setColour (juce::TextEditor::backgroundColourId, palette::face);
        editor->setColour (juce::TextEditor::textColourId, palette::text);
        editor->setColour (juce::TextEditor::outlineColourId, palette::lifted);
        editor->setColour (juce::TextEditor::focusedOutlineColourId, palette::textMuted);
    }
    linksEditor.setMultiLine (true, false);
    linksEditor.setReturnKeyStartsNewLine (true);
    portEditor.onReturnKey = [this] { commitEditor(); };
    portEditor.onFocusLost = [this] { commitEditor(); };
    linksEditor.onFocusLost = [this] { setDirectLinks (linksEditor.getText()); };
    for (auto* child : std::initializer_list<juce::Component*> { &title, &portCaption, &linksCaption, &message, &list, &portEditor, &linksEditor })
        addAndMakeVisible (child);
    refresh (true);
}

IoPage::~IoPage() { list.setModel (nullptr); }

bool IoPage::deviceMode() const { return shown.backend.type == BackendSettings::Type::device; }

juce::String IoPage::rowText (int index) const
{
    const auto& row = portRows.at ((size_t) index);
    const auto foreign = foreignText (row, deviceMode());
    return juce::String (row.port) + "   " + mappedText (row, deviceMode()) + (foreign.isEmpty() ? juce::String() : "   " + foreign);
}

juce::String IoPage::editableText (const std::string& port) const
{
    for (const auto& row : portRows)
        if (row.port == port)
            return deviceMode() ? (row.deviceChannel ? juce::String (*row.deviceChannel + 1) : juce::String()) : joined (row.mapped);
    return {};
}

void IoPage::edit (const AppConfig& config)
{
    view.edit (config);
    refresh (true);
}

void IoPage::setPeers (const std::string& port, const juce::String& text)
{
    auto config = view.config();
    const auto peers = splitPeers (text);
    if (peers.empty())
        config.io.jack.erase (port);
    else
        config.io.jack[port] = peers;
    look::say (message, {});
    edit (config);
}

void IoPage::setDeviceChannel (const std::string& port, int channel)
{
    auto config = view.config();
    if (channel < 0)
        config.io.deviceChannels.erase (port);
    else
        config.io.deviceChannels[port] = channel;
    look::say (message, {});
    edit (config);
}

void IoPage::setDirectLinks (const juce::String& text)
{
    std::vector<std::pair<std::string, std::string>> links;
    for (const auto& line : juce::StringArray::fromLines (text))
    {
        if (line.trim().isEmpty())
            continue;
        const auto arrow = line.indexOf ("->");
        const auto from = line.substring (0, juce::jmax (0, arrow)).trim();
        const auto to = arrow < 0 ? juce::String() : line.substring (arrow + 2).trim();
        if (from.isEmpty() || to.isEmpty())
        {
            look::say (message, "not a link (want  from -> to): " + line.trim());
            return;
        }
        links.emplace_back (from.toStdString(), to.toStdString());
    }
    auto config = view.config();
    look::say (message, {});
    if (config.io.directLinks == links)
        return;
    config.io.directLinks = std::move (links);
    edit (config);
}

void IoPage::selectPort (int row)
{
    if (row < 0 || row >= (int) portRows.size())
        return;
    const auto& port = portRows[(size_t) row].port;
    portCaption.setText (juce::String (port) + (deviceMode() ? "  -  device channel (1-based, empty = unmapped)"
                                                              : "  -  JACK peers, comma separated"), juce::dontSendNotification);
    portEditor.setText (editableText (port), juce::dontSendNotification);
}

void IoPage::commitEditor()
{
    const auto row = list.getSelectedRow();
    if (row < 0 || row >= (int) portRows.size())
        return;
    const auto port = portRows[(size_t) row].port;
    const auto text = portEditor.getText().trim();
    if (editableText (port) == text)
        return;
    if (! deviceMode())
        return setPeers (port, text);
    if (text.isEmpty())
        return setDeviceChannel (port, -1);
    if (! text.containsOnly ("0123456789") || text.getIntValue() < 1)
        return look::say (message, "a device channel is a whole number from 1");
    setDeviceChannel (port, text.getIntValue() - 1);
}

void IoPage::refresh (bool force)
{
    if (! force && ++ticks < reloadEveryTicks)
        return;
    ticks = 0;
    reload();
}

void IoPage::reload()
{
    shown = view.config();
    lastLinks = view.liveLinks();
    juce::String error;
    const auto ports = portNamesFor (shown, error);
    if (! ports)
        return look::say (message, error);
    portRows = ioRows (shown, *ports, lastLinks);
    list.updateContent();
    list.repaint();
    if (! linksEditor.hasKeyboardFocus (true))
    {
        juce::StringArray lines;
        for (const auto& [from, to] : shown.io.directLinks)
            lines.add (juce::String (from) + " -> " + to);
        linksEditor.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
    }
    if (! portEditor.hasKeyboardFocus (true))
        selectPort (list.getSelectedRow());
}

void IoPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void IoPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    const int row = look::rowHeight (*this);
    list.setRowHeight (juce::jmax (22, row * 2 / 3));
    title.setBounds (area.removeFromTop (row));
    auto bottom = area.removeFromBottom (row * 3 + row);
    message.setBounds (bottom.removeFromBottom (row));
    linksCaption.setBounds (bottom.removeFromTop (row));
    linksEditor.setBounds (bottom);
    auto editRow = area.removeFromBottom (row);
    portCaption.setBounds (editRow.removeFromLeft (editRow.getWidth() / 2));
    portEditor.setBounds (editRow);
    list.setBounds (area);
}
}
