// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IoPage.h"
#include "ConfigEdits.h"
#include <sstream>

namespace a3app
{
namespace
{
constexpr int localDeckId = 1, aes67DeckId = 2, incompleteDeckId = 3;   // the chooser's items
constexpr int reloadEveryTicks = 10;   // the window ticks at 20 Hz: the live links are re-read twice a second
constexpr int noneItem = 1;            // a route dropdown's first item; the choices follow from 2
const juce::String noneText = juce::String::fromUTF8 ("\xe2\x80\x94 none \xe2\x80\x94");

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
    for (auto* child : std::initializer_list<juce::Component*> { &title, &portCaption, &linksCaption, &message, &list, &portEditor, &linksEditor,
                                                                 &inputKey, &outputKey, &inputView, &outputView })
        addAndMakeVisible (child);
    for (auto* key : { &inputKey, &outputKey })
    {
        key->setRadioGroupId (3);
        key->setClickingTogglesState (false);
    }
    inputKey.onClick = [this] { showSub (Sub::input); };
    outputKey.onClick = [this] { showSub (Sub::output); };
    look::help ("INPUT: what feeds each channel's L and R, the decks' sources and the direct links. OUTPUT: where each "
                "zone's channels go, and every engine port with its live links. A port another channel already uses is "
                "marked in the list, not refused. Every edit applies after Save.", { &title, &inputKey, &outputKey });
    look::caption (decksCaption);
    decksCaption.setText ("DECKS", juce::dontSendNotification);
    inputContent.addAndMakeVisible (decksCaption);
    inputView.setViewedComponent (&inputContent, false);
    outputView.setViewedComponent (&outputContent, false);
    for (auto* scroller : { &inputView, &outputView })
        scroller->setScrollBarsShown (true, false);
    refresh (true);
    showSub (Sub::input);
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

juce::ComboBox* IoPage::deckSourceBox (int slot)
{
    return slot >= 1 && slot <= (int) deckChoosers.size() ? &deckChoosers[(size_t) slot - 1]->box : nullptr;
}

void IoPage::chooseDeckSource (int slot)
{
    const auto* box = deckSourceBox (slot);
    if (box == nullptr)
        return;
    if (box->getSelectedId() == incompleteDeckId)   // only a display state; the user picks a real source
        return;
    const auto source = box->getSelectedId() == aes67DeckId ? DeckSource::aes67 : DeckSource::local;
    auto config = view.config();
    setDeckSource (config.io, slot, source);
    look::say (message, {});
    edit (config);
}

void IoPage::rebuildDeckChoosers()
{
    if ((int) deckChoosers.size() == shown.engine.decks)
        return;
    deckChoosers.clear();
    for (int slot = 1; slot <= shown.engine.decks; ++slot)
    {
        auto chooser = std::make_unique<DeckChooser>();
        look::caption (chooser->label);
        chooser->label.setText ("Deck " + juce::String (deckLetters (slot)), juce::dontSendNotification);
        chooser->box.addItem ("Local StemDeck", localDeckId);
        chooser->box.addItem ("AES67 stream", aes67DeckId);
        chooser->box.addItem ("Incomplete - pick a source", incompleteDeckId);
        chooser->box.onChange = [this, slot] { chooseDeckSource (slot); };
        inputContent.addAndMakeVisible (chooser->label);
        inputContent.addAndMakeVisible (chooser->box);
        deckChoosers.push_back (std::move (chooser));
    }
    resized();
}

void IoPage::showDeckSources()
{
    for (int slot = 1; slot <= (int) deckChoosers.size(); ++slot)
    {
        const auto source = deckSourceOf (shown.io, slot);
        const int id = ! source ? incompleteDeckId : *source == DeckSource::aes67 ? aes67DeckId : localDeckId;
        deckChoosers[(size_t) slot - 1]->box.setSelectedId (id, juce::dontSendNotification);
    }
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
    offered = view.status();
    rebuildRoutes();
    fillRoutes();
    rebuildDeckChoosers();
    showDeckSources();
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

void IoPage::showSub (Sub wanted)
{
    sub = wanted;
    const bool input = sub == Sub::input;
    inputKey.setToggleState (input, juce::dontSendNotification);
    outputKey.setToggleState (! input, juce::dontSendNotification);
    for (auto* child : std::initializer_list<juce::Component*> { &inputView, &linksCaption, &linksEditor })
        child->setVisible (input);
    for (auto* child : std::initializer_list<juce::Component*> { &outputView, &list, &portCaption, &portEditor })
        child->setVisible (! input);
}

juce::String IoPage::foreignPeers (const std::string& port) const
{
    for (const auto& row : portRows)
        if (row.port == port)
            return joined (row.foreign);
    return {};
}

// JACK: the hardware's capture (input) or playback ports; a device: its channels, "channel 1" on.
juce::StringArray IoPage::routeChoices (bool input) const
{
    if (! deviceMode())
        return input ? offered.capturePorts : offered.playbackPorts;
    juce::StringArray channels;
    for (int channel = 1; channel <= (input ? offered.deviceInputs : offered.deviceOutputs); ++channel)
        channels.add ("channel " + juce::String (channel));
    return channels;
}

// The dropdowns for the ports this config has: per channel L and R (INPUT), per zone its outputs (OUTPUT).
void IoPage::rebuildRoutes()
{
    juce::String error;
    const auto ports = portNamesFor (shown, error);
    if (! ports)
        return;
    juce::String key;
    for (const auto& port : ports->outputs)
        key << port << ",";
    for (const auto& channel : channelInputPorts (shown.engine))
        key << channel.left.value_or ("-") << channel.right.value_or ("-") << ",";
    if (key == routeKey)
        return;
    routeKey = key;
    inputGroups.clear();
    outputGroups.clear();
    const auto addRoute = [this] (Group& group, juce::Component& content, const std::string& port, bool input, const juce::String& caption)
    {
        auto route = std::make_unique<Route>();
        route->port = port;
        route->input = input;
        look::caption (route->caption);
        route->caption.setText (caption, juce::dontSendNotification);
        look::caption (route->foreign);
        route->box.onChange = [this, port, box = &route->box] { chooseRoute (port, box->getSelectedItemIndex()); };
        for (auto* child : std::initializer_list<juce::Component*> { &route->caption, &route->foreign, &route->box })
            content.addAndMakeVisible (child);
        group.routes.push_back (std::move (route));
    };
    const auto addGroup = [] (std::vector<std::unique_ptr<Group>>& groups, juce::Component& content, const juce::String& name) -> Group&
    {
        groups.push_back (std::make_unique<Group>());
        look::caption (groups.back()->caption);
        groups.back()->caption.setText (name, juce::dontSendNotification);
        content.addAndMakeVisible (groups.back()->caption);
        return *groups.back();
    };
    for (const auto& channel : channelInputPorts (shown.engine))
    {
        auto& group = addGroup (inputGroups, inputContent, "CH " + juce::String (channel.channel)
                                                               + (channel.left ? juce::String() : "   deck only: no analog input"));
        if (channel.left)
            addRoute (group, inputContent, *channel.left, true, "L   " + juce::String (*channel.left));
        if (channel.right)
            addRoute (group, inputContent, *channel.right, true, "R   " + juce::String (*channel.right));
    }
    for (const auto& zone : zoneOutputGroups (*ports))
    {
        auto& group = addGroup (outputGroups, outputContent, zone.zone);
        for (const auto& port : zone.ports)
            addRoute (group, outputContent, port, false, juce::String (port));
    }
    resized();
}

// Each dropdown's items (a choice another port uses says which) and its selection, from the config.
void IoPage::fillRoutes()
{
    for (auto* groups : { &inputGroups, &outputGroups })
        for (auto& group : *groups)
            for (auto& route : group->routes)
            {
                const auto choices = routeChoices (route->input);
                juce::StringArray items { noneText };
                for (int i = 0; i < choices.size(); ++i)
                {
                    const auto peer = deviceMode() ? "#" + std::to_string (i) : choices[i].toStdString();
                    juce::StringArray users;
                    for (const auto& other : portsRoutedTo (shown, peer, route->port))
                        users.add (juce::String (other));
                    items.add (choices[i] + (users.isEmpty() ? juce::String() : "   (also " + users.joinIntoString (", ") + ")"));
                }
                int selectedIndex = 0;
                if (deviceMode())
                {
                    if (const auto it = shown.io.deviceChannels.find (route->port); it != shown.io.deviceChannels.end())
                        selectedIndex = it->second + 1 < items.size() ? it->second + 1 : 0;
                }
                else if (const auto it = shown.io.jack.find (route->port); it != shown.io.jack.end() && ! it->second.empty())
                {
                    const int found = choices.indexOf (juce::String (it->second.front()));
                    if (found < 0)
                        items.add (juce::String (it->second.front()) + "   (not hardware)");   // what the map holds, kept
                    selectedIndex = found >= 0 ? found + 1 : items.size() - 1;
                }
                juce::StringArray current;
                for (int i = 0; i < route->box.getNumItems(); ++i)
                    current.add (route->box.getItemText (i));
                if (current != items)
                {
                    route->box.clear (juce::dontSendNotification);
                    for (int i = 0; i < items.size(); ++i)
                        route->box.addItem (items[i], noneItem + i);
                }
                route->box.setSelectedItemIndex (selectedIndex, juce::dontSendNotification);
                look::say (route->foreign, deviceMode() ? juce::String() : foreignPeers (route->port));
            }
}

juce::ComboBox* IoPage::routeBox (const std::string& port)
{
    for (auto* groups : { &inputGroups, &outputGroups })
        for (auto& group : *groups)
            for (auto& route : group->routes)
                if (route->port == port)
                    return &route->box;
    return nullptr;
}

void IoPage::chooseRoute (const std::string& port, int item)
{
    const bool input = isEngineInputPort (port);
    const auto choices = routeChoices (input);
    if (item < 0 || item > choices.size())
        return;   // the "not hardware" item: what the map holds stays
    look::say (message, {});
    if (deviceMode())
        return edit (withDeviceRoute (view.config(), port, item == 0 ? std::nullopt : std::optional<int> (item - 1)));
    edit (withJackRoute (view.config(), port, item == 0 ? std::string() : choices[item - 1].toStdString()));
}

void IoPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void IoPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    const int row = look::rowHeight (*this);
    auto top = area.removeFromTop (row);
    title.setBounds (top.removeFromLeft (top.getWidth() / 4));
    inputKey.setBounds (top.removeFromLeft (top.getWidth() / 2).reduced (row / 16));
    outputKey.setBounds (top.reduced (row / 16));
    message.setBounds (area.removeFromBottom (row));
    layoutInput (area, row);
    layoutOutput (area, row);
}

namespace
{
// Groups of dropdown rows in a scrolled content: a caption row per group, then one row per route.
template <class Group>
int layoutGroups (std::vector<std::unique_ptr<Group>>& groups, juce::Rectangle<int>& content, int line)
{
    int used = 0;
    for (auto& group : groups)
    {
        group->caption.setBounds (content.removeFromTop (line));
        used += line;
        for (auto& route : group->routes)
        {
            auto slice = content.removeFromTop (line).reduced (0, line / 12);
            route->caption.setBounds (slice.removeFromLeft (slice.getWidth() / 4));
            route->foreign.setBounds (slice.removeFromRight (slice.getWidth() / 4));
            route->box.setBounds (slice);
            used += line;
        }
    }
    return used;
}
}

// INPUT: the channels' L/R dropdowns and the deck sources scrolled together; the direct links below.
void IoPage::layoutInput (juce::Rectangle<int> area, int row)
{
    auto links = area.removeFromBottom (row * 3);
    linksCaption.setBounds (links.removeFromTop (row));
    linksEditor.setBounds (links);
    inputView.setBounds (area);
    const int line = row * 3 / 4;
    const int width = area.getWidth() - inputView.getScrollBarThickness();
    auto content = juce::Rectangle<int> (0, 0, width, 1 << 20);
    int used = layoutGroups (inputGroups, content, line);
    decksCaption.setBounds (content.removeFromTop (line));
    used += line;
    for (auto& chooser : deckChoosers)
    {
        auto slice = content.removeFromTop (line).reduced (0, line / 12);
        chooser->label.setBounds (slice.removeFromLeft (slice.getWidth() / 4));
        chooser->box.setBounds (slice);
        used += line;
    }
    inputContent.setSize (width, used);
}

// OUTPUT: the zones' dropdowns scrolled, then every port with its live links and the editor for one.
void IoPage::layoutOutput (juce::Rectangle<int> area, int row)
{
    list.setRowHeight (juce::jmax (22, row * 2 / 3));
    auto lower = area.removeFromBottom (area.getHeight() / 3);
    auto editRow = lower.removeFromBottom (row);
    portCaption.setBounds (editRow.removeFromLeft (editRow.getWidth() / 2));
    portEditor.setBounds (editRow);
    list.setBounds (lower);
    outputView.setBounds (area);
    const int width = area.getWidth() - outputView.getScrollBarThickness();
    auto content = juce::Rectangle<int> (0, 0, width, 1 << 20);
    outputContent.setSize (width, layoutGroups (outputGroups, content, row * 3 / 4));
}
}
