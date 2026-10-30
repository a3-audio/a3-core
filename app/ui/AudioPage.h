// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Look.h"
#include "AudioTiming.h"
#include "ChannelsPage.h"
#include "ClockPanel.h"
#include "HostView.h"
#include "Look.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <optional>

namespace a3app
{
// The audio page (2026-10-30, second round), four cards in the ZONES card's style. DRIVER: one device-type list,
// "PipeWire (JACK)" (the JACK backend) beside JUCE's device types; then every setting a dropdown showing what is set:
// a JUCE type's input and output device, sample rate and block (pending until Save); PipeWire's sample rate (the
// graph's, the one choice it shows: the engine does not change PipeWire's rate), Block (the forced quantum, live) and
// Periods (the card's ALSA periods, through the engine's WirePlumber drop-in and a restart), applied only on APPLY
// after the page said what happens; REVERT goes back to the previous choice (TimingControl.h). CHANNELS: N and the
// deck count (ChannelsPage). MEASURE: the round trip (over the I/O page's digital loop) and the analog chain with
// their keys, off while the I/O page names no pair, their line saying why. CLOCK: the master and the one-word
// state (ClockPanel::Mode::choice). What runs in detail (rate source, bit depth, DSP load, xruns, the clock's
// numbers) is on STATUS. Every text is one size. The page opens no device: the manager is a catalogue of device types.
// The MEASURE card's values (2026-10-30, third round): "7.8 ms   21:05" once measured, else why not ("no digital loop
// (I/O page)", "not measured yet", ...); the analog roundtrip is the digital one now plus the converters' stored share
// ("9.1 ms   2026-10-30"), else why not, and what to plug before the first run.
juce::String digitalRoundtripValue (const RoundTrip& estimate, const RoundTripReport&, bool loopSet);
juce::String analogRoundtripValue (const RoundTrip& estimate, const RoundTripReport&, const std::optional<AnalogLoop>& pair);

class AudioPage final : public juce::Component, public CardPage
{
public:
    static inline const juce::String pipewireType { "PipeWire (JACK)" };

    AudioPage (HostView&, juce::AudioDeviceManager&);
    ~AudioPage() override;

    void chooseDeviceType (const juce::String& name);   // pipewireType or one of JUCE's; an edit, Save applies
    void chooseBlock (int frames);
    void choosePeriods (int periods);
    void refresh();   // re-reads the view's status (the window's timer)

    bool showsDeviceSelector() const { return deviceChoosers; }   // a JUCE type: input, output, rate
    bool showsPeriods() const { return periodsBox.isVisible(); }
    juce::ComboBox& typeChooser() { return typeBox; }
    juce::ComboBox& blockChooser() { return blockBox; }
    juce::ComboBox& periodsChooser() { return periodsBox; }
    juce::ComboBox& rateChooser() { return rateBox; }
    ChannelsPage& channelsCard() { return channels; }
    // The cards (CardPage::cardAreas): DRIVER, CHANNELS, MEASURE, CLOCK.
    juce::Button& applyButton() { return apply; }
    juce::Button& cancelButton() { return cancel; }
    juce::Button& revertButton() { return revert; }
    const juce::Label& confirmLabel() const { return confirm; }
    const juce::Label& pendingLabel() const { return pending; }
    const juce::Label& roundTripLabel() const { return roundTrip; }
    juce::Button& measureButton() { return measure; }
    const juce::Label& analogChainLabel() const { return analogChain; }
    juce::Button& analogButton() { return measureAnalog; }
    const ClockPanel& clockPanel() const { return clock; }
    ClockPanel& clockPanel() { return clock; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void rebuildForType();
    void fillTypes();
    void fillChoosers();
    void fillTiming();
    void editDevice();
    void showTiming (const BackendStatus&);
    void showMeasurements (const BackendStatus&);
    bool timingLive() const;            // the engine runs on PipeWire and takes a change now
    AudioTiming currentTiming() const;  // what runs (the engine's report), else the config's
    AudioTiming shownTiming() const;    // the choice not applied yet, else what runs
    void choose (const AudioTiming&);
    void applyChosen();
    void revertTiming();
    void layoutSettings (juce::Rectangle<int>& area);
    void layoutMeasure (juce::Rectangle<int>& area);
    void fillPipeWireRate();

    HostView& view;
    juce::AudioDeviceManager& manager;
    juce::Label driverHeading, channelsHeading, measureHeading, digitalCaption, analogCaption, roundTrip, pending, analogChain, confirm;
    layout::Tokens tokens;
    juce::TextButton measure { "MEASURE" }, measureAnalog { "MEASURE ANALOG CHAIN" }, apply { "APPLY" }, cancel { "CANCEL" },
        revert { "REVERT" };
    juce::Label typeCaption, inputCaption, outputCaption, rateCaption, blockCaption, periodsCaption;
    Dropdown typeBox, inputBox, outputBox, rateBox, blockBox, periodsBox;
    ChannelsPage channels;
    ClockPanel clock { ClockPanel::Mode::choice };
    std::optional<AudioTiming> chosenTiming;   // PipeWire: chosen on the page, not applied yet
    bool deviceChoosers = false;
    bool filling = false;
};
}
