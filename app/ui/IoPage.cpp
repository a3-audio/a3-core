// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IoPage.h"
#include "ConfigEdits.h"
#include <algorithm>
#include <functional>

namespace a3app
{
namespace
{
constexpr int localDeckId = 1, aes67DeckId = 2, incompleteDeckId = 3;   // the chooser's items
constexpr int reloadEveryTicks = 10;   // the window ticks at 20 Hz: the live links are re-read twice a second
constexpr int noneItem = 1;            // a route dropdown's first item; the choices follow from 2
const juce::String noneText = juce::String::fromUTF8 ("\xe2\x80\x94 none \xe2\x80\x94");

}

IoPage::IoPage (HostView& v)
    : view (v), shown (v.config())
{
    look::caption (title);
    look::failure (message);
    look::caption (notice);
    notice.setMinimumHorizontalScale (0.5f);
    analogOutput.onChange = [this] { chooseAnalogPair (false, analogOutput.getSelectedId() - 1); };
    analogInput.onChange = [this] { chooseAnalogPair (true, analogInput.getSelectedId() - 1); };
    digitalOutput.onChange = [this] { chooseDigitalLoop (false, digitalOutput.getSelectedId() - 1); };
    digitalInput.onChange = [this] { chooseDigitalLoop (true, digitalInput.getSelectedId() - 1); };
    const auto addMeasureRows = [] (MeasureRows& rows, juce::Component& content, juce::ComboBox& digital, juce::ComboBox& analog)
    {
        for (auto* label : { &rows.caption, &rows.digital, &rows.analog })
        {
            look::caption (*label);
            content.addAndMakeVisible (*label);
        }
        rows.caption.setText ("MEASURE", juce::dontSendNotification);
        rows.digital.setText ("digital loop", juce::dontSendNotification);
        rows.analog.setText ("analog test cable", juce::dontSendNotification);
        content.addAndMakeVisible (digital);
        content.addAndMakeVisible (analog);
    };
    addMeasureRows (outputMeasure, outputContent, digitalOutput, analogOutput);
    addMeasureRows (inputMeasure, inputContent, digitalInput, analogInput);
    title.setText ("I/O", juce::dontSendNotification);
    for (auto* child : std::initializer_list<juce::Component*> { &title, &message, &notice, &inputKey, &outputKey, &inputView, &outputView })
        addAndMakeVisible (child);
    for (auto* key : { &inputKey, &outputKey })
    {
        key->setRadioGroupId (3);
        key->setClickingTogglesState (false);
    }
    departmentsButton.setRadioGroupId (3);
    departmentsButton.setClickingTogglesState (false);
    departmentsButton.onClick = [this] { showSub (Sub::departments); };
    inputKey.onClick = [this] { showSub (Sub::input); };
    outputKey.onClick = [this] { showSub (Sub::output); };
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

IoPage::~IoPage() = default;

bool IoPage::deviceMode() const { return shown.backend.type == BackendSettings::Type::device; }

void IoPage::edit (const AppConfig& config)
{
    view.edit (config);
    refresh (true);
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

void IoPage::refresh (bool force)
{
    if (sub == Sub::departments && refreshDepartments && isShowing())
        refreshDepartments();   // it asks Core: only while shown
    if (! force && ++ticks < reloadEveryTicks)
        return;
    ticks = 0;
    reload();
}

void IoPage::reload()
{
    shown = view.config();
    juce::String error;
    if (! portNamesFor (shown, error))
        return look::say (message, error);
    offered = view.status();
    look::say (notice, loopNotice (shown, view.loopDropped()));
    fillAnalogPair();
    fillDigitalLoop();
    rebuildRoutes();
    fillRoutes();
    rebuildDeckChoosers();
    showDeckSources();
}

void IoPage::addDepartments (juce::Component& page, std::function<void()> refreshWhileShown)
{
    departments = &page;
    refreshDepartments = std::move (refreshWhileShown);
    addAndMakeVisible (departmentsButton);
    addChildComponent (page);
    resized();
}

void IoPage::showSub (Sub wanted)
{
    sub = wanted == Sub::departments && departments == nullptr ? Sub::input : wanted;
    inputKey.setToggleState (sub == Sub::input, juce::dontSendNotification);
    outputKey.setToggleState (sub == Sub::output, juce::dontSendNotification);
    departmentsButton.setToggleState (sub == Sub::departments, juce::dontSendNotification);
    inputView.setVisible (sub == Sub::input);
    outputView.setVisible (sub == Sub::output);
    for (auto* line : { &message, &notice })
        line->setVisible (sub != Sub::departments);
    if (departments != nullptr)
        departments->setVisible (sub == Sub::departments);
    if (sub == Sub::departments && refreshDepartments)
        refreshDepartments();
}

// JACK: the hardware's capture (input) or playback ports; a device: its channels, "channel 1" on. Never the loop's.
std::vector<RouteChoice> IoPage::choicesFor (bool input) const
{
    return routeChoices (shown, input ? offered.capturePorts : offered.playbackPorts,
                         input ? offered.deviceInputs : offered.deviceOutputs, input, offered.deviceName);
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
        route->box.onChange = [this, port, box = &route->box] { chooseRoute (port, box->getSelectedItemIndex()); };
        for (auto* child : std::initializer_list<juce::Component*> { &route->caption, &route->box })
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
                const auto choices = choicesFor (route->input);
                juce::StringArray items { noneText };
                for (const auto& choice : choices)
                {
                    const auto peer = choice.channel ? "#" + std::to_string (*choice.channel) : choice.peer;
                    juce::StringArray users;
                    for (const auto& other : portsRoutedTo (shown, peer, route->port))
                        users.add (juce::String (other));
                    items.add (choice.label + (users.isEmpty() ? juce::String() : "   (also " + users.joinIntoString (", ") + ")"));
                }
                const auto indexOf = [&choices] (auto matches)
                {
                    const auto it = std::find_if (choices.begin(), choices.end(), matches);
                    return it == choices.end() ? -1 : (int) (it - choices.begin());
                };
                int selectedIndex = 0;
                if (deviceMode())
                {
                    if (const auto it = shown.io.deviceChannels.find (route->port); it != shown.io.deviceChannels.end())
                        selectedIndex = indexOf ([&] (const RouteChoice& c) { return c.channel == it->second; }) + 1;
                }
                else if (const auto it = shown.io.jack.find (route->port); it != shown.io.jack.end() && ! it->second.empty())
                {
                    const auto& mapped = it->second.front();
                    const int found = indexOf ([&] (const RouteChoice& c) { return c.peer == mapped; });
                    if (found < 0)   // what the map holds, kept: not offered now (another client, or the device is away)
                        items.add (peerLabel (mapped, offered.deviceName) + "   (not offered)");
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
            }
}

namespace
{
using BoxItems = std::vector<std::pair<juce::String, int>>;

// Rebuilt only when the items changed (an open list stays open between reloads), then the chosen one selected.
void showItems (juce::ComboBox& box, const BoxItems& items, int chosenId)
{
    BoxItems current;
    for (int i = 0; i < box.getNumItems(); ++i)
        current.emplace_back (box.getItemText (i), box.getItemId (i));
    if (current != items)
    {
        box.clear (juce::dontSendNotification);
        for (const auto& [text, id] : items)
            box.addItem (text, id);
    }
    box.setSelectedId (chosenId, juce::dontSendNotification);
}

// Item id = channel + 1: "none" is 1. A chosen channel not offered stays visible as such.
BoxItems channelItems (const std::vector<int>& channels, int chosen, const std::function<juce::String (int)>& label)
{
    BoxItems items { { noneText, 1 } };
    for (const int channel : channels)
        items.emplace_back (label (channel), channel + 1);
    if (chosen > 0 && std::find (channels.begin(), channels.end(), chosen) == channels.end())
        items.emplace_back (label (chosen) + "   (not offered)", chosen + 1);
    return items;
}
}

void IoPage::fillAnalogPair()
{
    const auto& loop = shown.io.analogLoop;
    for (const bool input : { false, true })
    {
        const auto channels = analogTestChannels (shown, input ? offered.capturePorts : offered.playbackPorts,
                                                  input ? offered.deviceInputs : offered.deviceOutputs, input);
        const juce::String side = input ? "input " : "output ";
        const int chosen = loop ? (input ? loop->input : loop->output) : 0;
        showItems (input ? analogInput : analogOutput, channelItems (channels, chosen, [&] (int c) { return side + juce::String (c); }),
                   chosen + 1);
    }
}

// Each item names the channel the loop would reserve ("output 11").
void IoPage::fillDigitalLoop()
{
    const auto& loop = shown.io.roundTripLoop;
    for (const bool input : { false, true })
    {
        const auto channels = digitalLoopChannels (shown, input ? offered.capturePorts : offered.playbackPorts,
                                                   input ? offered.deviceInputs : offered.deviceOutputs, input);
        const int chosen = loop ? (input ? loop->input : loop->output) : 0;
        const auto label = [&] (int c) { return loopRangeText ({ c, c }, input); };
        showItems (input ? digitalInput : digitalOutput, channelItems (channels, chosen, label), chosen + 1);
    }
}

void IoPage::chooseDigitalLoop (bool input, int channel)
{
    look::say (message, {});
    juce::String error;
    if (const auto edited = withDigitalLoopChannel (view.config(), input, channel, error))
        return edit (*edited);
    look::say (message, error);
    refresh (true);   // the box goes back to what the config holds
}

void IoPage::chooseAnalogPair (bool input, int channel)
{
    look::say (message, {});
    edit (withAnalogTestChannel (view.config(), input, channel));
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
    const auto choices = choicesFor (input);
    if (item < 0 || item > (int) choices.size())
        return;   // the "not offered" item: what the map holds stays
    look::say (message, {});
    const auto* choice = item == 0 ? nullptr : &choices[(size_t) item - 1];
    if (deviceMode())
        return edit (withDeviceRoute (view.config(), port, choice != nullptr ? choice->channel : std::nullopt));
    edit (withJackRoute (view.config(), port, choice != nullptr ? choice->peer : std::string()));
}

void IoPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void IoPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    const int row = look::rowHeight (*this);
    auto top = area.removeFromTop (row);
    title.setBounds (top.removeFromLeft (top.getWidth() / 4));
    const int keys = departments != nullptr ? 3 : 2;
    const int keyWidth = top.getWidth() / keys;
    inputKey.setBounds (top.removeFromLeft (keyWidth).reduced (row / 16));
    outputKey.setBounds (top.removeFromLeft (keyWidth).reduced (row / 16));
    if (departments != nullptr)
    {
        departmentsButton.setBounds (top.reduced (row / 16));
        departments->setBounds (getLocalBounds().withTrimmedTop (area.getY()));
    }
    message.setBounds (area.removeFromBottom (row));
    notice.setBounds (area.removeFromBottom (shown.io.roundTripLoop ? row : 0));
    layoutInput (area, row);
    layoutOutput (area, row);
}

namespace
{
// One row of a scrolled list: the caption on the first quarter, the dropdown on the rest.
int layoutRow (juce::Label& caption, juce::Component& box, juce::Rectangle<int>& content, int line)
{
    auto slice = content.removeFromTop (line).reduced (0, line / 12);
    caption.setBounds (slice.removeFromLeft (slice.getWidth() / 4));
    box.setBounds (slice);
    return line;
}

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
            used += layoutRow (route->caption, route->box, content, line);
    }
    return used;
}

// The MEASURE group that ends a list: its caption, the digital loop's row and the analog test cable's.
template <class Rows>
int layoutMeasure (Rows& rows, juce::Component& digital, juce::Component& analog, juce::Rectangle<int>& content, int line)
{
    rows.caption.setBounds (content.removeFromTop (line));
    return line + layoutRow (rows.digital, digital, content, line) + layoutRow (rows.analog, analog, content, line);
}
}

// INPUT: the channels' L/R dropdowns, the deck sources and the measurement rows, scrolled together.
void IoPage::layoutInput (juce::Rectangle<int> area, int row)
{
    inputView.setBounds (area);
    const int line = row * 3 / 4;
    const int width = area.getWidth() - inputView.getScrollBarThickness();
    auto content = juce::Rectangle<int> (0, 0, width, 1 << 20);
    int used = layoutGroups (inputGroups, content, line);
    decksCaption.setBounds (content.removeFromTop (line));
    used += line;
    for (auto& chooser : deckChoosers)
        used += layoutRow (chooser->label, chooser->box, content, line);
    used += layoutMeasure (inputMeasure, digitalInput, analogInput, content, line);
    inputContent.setSize (width, used);
}

// OUTPUT: the zones' dropdowns scrolled, the measurement pairs' outputs last.
void IoPage::layoutOutput (juce::Rectangle<int> area, int row)
{
    outputView.setBounds (area);
    const int line = row * 3 / 4;
    const int width = area.getWidth() - outputView.getScrollBarThickness();
    auto content = juce::Rectangle<int> (0, 0, width, 1 << 20);
    const int used = layoutGroups (outputGroups, content, line);
    outputContent.setSize (width, used + layoutMeasure (outputMeasure, digitalOutput, analogOutput, content, line));
}
}
