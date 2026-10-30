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
// port or device channel it goes to; one another channel uses is marked, not refused). REC is empty by default:
// it is for an analog recorder (the beat-analyzer and the AES67 sender are built-in links). A dropdown names the
// device's port as the user means it ("Scarlett 18i20 USB out 1", ConfigEdits.h peerLabel) and nothing stands
// beside it: since 2026-10-30 the engine links exactly the plan and removes any other link on its ports
// (LinkPlan.h), so there is no list of foreign links and no free-text port field. Each list ends with the two
// measurement pairs, its side of them: the digital loop (the round trip) and the analog test cable. The digital
// loop's channels are offered in no route; a notice names them and the routes a load left out because they were
// on them. Every edit is pending until Save.
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

    void setDirectLinks (const juce::String& linesOfFromArrowTo);
    void refresh (bool force = false);   // re-reads the config and the live links (throttled unless forced)
    const juce::Label& messageLabel() const { return message; }
    const juce::Label& noticeLabel() const { return notice; }
    // The analog test pair (a temporary cable for the AUDIO page's MEASURE ANALOG CHAIN): one side's channel, 0 = none.
    // Each box is a row at the end of its side's list (OUTPUT: the output, INPUT: the input).
    juce::ComboBox& analogOutputBox() { return analogOutput; }
    juce::ComboBox& analogInputBox() { return analogInput; }
    void chooseAnalogPair (bool input, int channelOneBased);
    // The digital loop the AUDIO page's MEASURE runs over: one side's first channel, 0 = no loop (withDigitalLoopChannel).
    juce::ComboBox& digitalOutputBox() { return digitalOutput; }
    juce::ComboBox& digitalInputBox() { return digitalInput; }
    void chooseDigitalLoop (bool input, int channelOneBased);
    // The per-deck source chooser (1 = local StemDeck, 2 = AES67, 3 = the slot holds neither complete set); nullptr past the config's decks.
    juce::ComboBox* deckSourceBox (int slotOneBased);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    bool deviceMode() const;
    void reload();
    void edit (const AppConfig&);
    void chooseDeckSource (int slotOneBased);
    void showDeckSources();
    void rebuildDeckChoosers();
    void rebuildRoutes();
    void fillRoutes();
    std::vector<RouteChoice> choicesFor (bool input) const;
    void fillAnalogPair();
    void fillDigitalLoop();
    void layoutInput (juce::Rectangle<int> area, int row);
    void layoutOutput (juce::Rectangle<int> area, int row);

    HostView& view;
    AppConfig shown;
    int ticks = 0;
    juce::Label title, linksCaption, message, notice;
    juce::ComboBox analogOutput, analogInput, digitalOutput, digitalInput;
    // The MEASURE group at the end of one side's list: its caption and the two rows' captions.
    struct MeasureRows { juce::Label caption, digital, analog; };
    MeasureRows outputMeasure, inputMeasure;
    juce::TextEditor linksEditor;
    struct DeckChooser { juce::Label label; juce::ComboBox box; };
    std::vector<std::unique_ptr<DeckChooser>> deckChoosers;

    // One dropdown: its port and the caption before it.
    struct Route { std::string port; bool input = false; juce::Label caption; juce::ComboBox box; };
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
