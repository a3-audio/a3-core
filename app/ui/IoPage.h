// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"
#include <memory>
#include <vector>

namespace a3app
{
// Two sub-pages, chosen by INPUT and OUTPUT at the top. INPUT: per channel a dropdown for L and R (the capture
// port or device channel feeding its analog pair), the deck slots' sources, and the direct links the engine makes
// between two foreign ports. OUTPUT: per zone (MAIN, BOOTH, PHONES, REC) a dropdown per channel (the playback
// port or device channel it goes to; one another channel uses is marked, not refused), and below it every engine
// port as the I/O map names its peers (editable, comma separated) beside the live ones it does not (foreign:
// shown in muted text, never edited). Every edit is pending until Save.
class IoPage final : public juce::Component
{
public:
    explicit IoPage (HostView&);
    ~IoPage() override;

    enum class Sub { input, output };
    void showSub (Sub);
    Sub shownSub() const { return sub; }
    // A dropdown's port: its box (nullptr when the page has none for it), and a choice (0 = none, then the
    // offered capture/playback ports or device channels in order).
    juce::ComboBox* routeBox (const std::string& port);
    void chooseRoute (const std::string& port, int item);
    juce::String foreignPeers (const std::string& port) const;   // the live peers the map does not name

    const std::vector<IoRow>& rows() const { return portRows; }
    juce::String rowText (int row) const;               // mapped and foreign, as the row shows them
    juce::String editableText (const std::string& port) const;   // what the editor holds: the config's, never the foreign

    void setPeers (const std::string& port, const juce::String& commaSeparated);
    void setDeviceChannel (const std::string& port, int channel0OrMinusOne);
    void setDirectLinks (const juce::String& linesOfFromArrowTo);
    void refresh (bool force = false);   // re-reads the config and the live links (throttled unless forced)
    const juce::Label& messageLabel() const { return message; }
    // The per-deck source chooser (1 = local StemDeck, 2 = AES67, 3 = the slot holds neither complete set); nullptr past the config's decks.
    juce::ComboBox* deckSourceBox (int slotOneBased);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Model;
    bool deviceMode() const;
    void reload();
    void selectPort (int row);
    void commitEditor();
    void edit (const AppConfig&);
    void chooseDeckSource (int slotOneBased);
    void showDeckSources();
    void rebuildDeckChoosers();
    void rebuildRoutes();
    void fillRoutes();
    juce::StringArray routeChoices (bool input) const;
    void layoutInput (juce::Rectangle<int> area, int row);
    void layoutOutput (juce::Rectangle<int> area, int row);

    HostView& view;
    std::vector<IoRow> portRows;
    LiveLinks lastLinks;
    AppConfig shown;
    int ticks = 0;
    juce::Label title, portCaption, linksCaption, message;
    std::unique_ptr<Model> model;
    juce::ListBox list;
    juce::TextEditor portEditor, linksEditor;
    struct DeckChooser { juce::Label label; juce::ComboBox box; };
    std::vector<std::unique_ptr<DeckChooser>> deckChoosers;

    // One dropdown: its port, the caption beside it, and the live foreign peers after it.
    struct Route { std::string port; bool input = false; juce::Label caption, foreign; juce::ComboBox box; };
    struct Group { juce::Label caption; std::vector<std::unique_ptr<Route>> routes; };
    std::vector<std::unique_ptr<Group>> inputGroups, outputGroups;
    juce::String routeKey;   // the ports and choices the dropdowns were built for
    BackendStatus offered;   // the status the choices came from
    Sub sub = Sub::input;
    juce::TextButton inputKey { "INPUT" }, outputKey { "OUTPUT" };
    juce::Label decksCaption;
    juce::Component inputContent, outputContent;   // the scrolled parts of the sub-pages
    juce::Viewport inputView, outputView;
};
}
