// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"

namespace a3app
{
// Per engine port: JACK shows the peers the I/O map names (editable, comma separated) and the
// live ones it does not (foreign: shown in muted text, never edited); a device shows the device
// channel (editable). Below: the direct links the engine makes between two foreign ports.
// Every edit is pending until Save.
class IoPage final : public juce::Component
{
public:
    explicit IoPage (HostView&);
    ~IoPage() override;

    const std::vector<IoRow>& rows() const { return portRows; }
    juce::String rowText (int row) const;               // mapped and foreign, as the row shows them
    juce::String editableText (const std::string& port) const;   // what the editor holds: the config's, never the foreign

    void setPeers (const std::string& port, const juce::String& commaSeparated);
    void setDeviceChannel (const std::string& port, int channel0OrMinusOne);
    void setDirectLinks (const juce::String& linesOfFromArrowTo);
    void refresh (bool force = false);   // re-reads the config and the live links (throttled unless forced)
    const juce::Label& messageLabel() const { return message; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Model;
    bool deviceMode() const;
    void reload();
    void selectPort (int row);
    void commitEditor();
    void edit (const AppConfig&);

    HostView& view;
    std::vector<IoRow> portRows;
    LiveLinks lastLinks;
    AppConfig shown;
    int ticks = 0;
    juce::Label title, portCaption, linksCaption, message;
    std::unique_ptr<Model> model;
    juce::ListBox list;
    juce::TextEditor portEditor, linksEditor;
};
}
