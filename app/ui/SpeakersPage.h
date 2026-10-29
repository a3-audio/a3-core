// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AllRadDesign.h"
#include "HostView.h"
#include "Look.h"

namespace a3app
{
// The speaker map of one zone (MAIN or BOOTH) in IEM's own AllRADecoder editor: its table, noise buttons,
// 3D view and energy map, on a processor here that never gets audio, loaded from the pending config's
// <zone>.decoder.json. Beside it our bar: the zone, the sub channel and crossover (SimpleDecoder's), and
// SAVE TO ZONE: IEM's calculateDecoder and saveConfigurationToFile make the file, withZoneDecoder writes the
// user variant and makes it the pending edit; Save restarts the engine. A noise button plays a 1 s pink-noise
// burst in the shown zone of the running engine (its running decoder), before its level stage and gate.
class SpeakersPage final : public juce::Component
{
public:
    // `userRoot`: where user variants are written (<configDir>/engine/spatial); empty: SAVE TO ZONE refuses.
    SpeakersPage (HostView&, juce::File userRoot);
    ~SpeakersPage() override;

    void showZone (a3::SpeakerZone);
    a3::SpeakerZone zone() const { return shown; }
    void setSubChannel (int channel);   // 0 = no sub
    void setCrossover (float hz);       // the sub's low pass and the tops' high pass
    void saveToZone();
    void refresh();                     // reloads when the pending spatial directory changed

    juce::AudioProcessorEditor* editor() { return iemEditor.get(); }
    const juce::Label& messageLabel() const { return message; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void load();
    void showParameters();
    juce::var editedParameters() const;

    HostView& view;
    const juce::File userRoot;
    a3::SpeakerZone shown = a3::SpeakerZone::main;
    juce::File loadedFrom;
    juce::var parameters;   // the zone's <zone>.parameters.json as loaded
    int subChannel = 0;     // 0 = no sub
    float crossoverHz = 150.0f;
    a3::AllRadDesigner designer;
    std::unique_ptr<juce::AudioProcessorEditor> iemEditor;
    juce::Viewport viewport;
    juce::TextButton mainKey { "MAIN" }, boothKey { "BOOTH" }, save { "SAVE TO ZONE" };
    juce::Label title, subCaption, crossoverCaption, message, note;
    juce::ComboBox sub;
    juce::Slider crossover { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
};
}
