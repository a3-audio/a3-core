// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Dropdown.h"
#include "AudioTiming.h"
#include "ClockPanel.h"
#include "HostView.h"
#include "Look.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <optional>

namespace a3app
{
// The audio page, three sections (2026-10-30): DRIVER, MEASURE and CLOCK, every text in one size. DRIVER: the sound-device settings: one device-type list, "PipeWire (JACK)" (the JACK backend) beside
// JUCE's device types. PipeWire: Block (the forced quantum, live) and Periods (the sound card's ALSA periods, through
// the engine's WirePlumber drop-in and a WirePlumber restart), applied by the engine only on APPLY, after the page
// said what happens; REVERT goes back to the previous choice (TimingControl.h). A JUCE type: input and output device,
// rate and block size, pending until Save (no periods: JUCE's ALSA device sets its own). Below the settings what
// runs: rate, block and periods, where they come from, bit depth, DSP load and xruns, the round trip (measured over
// the I/O page's digital loop, MEASURE runs it again, or estimated) and the analog chain in MEASURE; at the bottom the
// CLOCK section (ClockPanel). MEASURE and MEASURE ANALOG CHAIN are off while the I/O page names no pair for them, and
// their own line says so (no hint line under them). The page opens no device: the manager is a catalogue of device types and is never initialised.
class AudioPage final : public juce::Component
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
    juce::Button& applyButton() { return apply; }
    juce::Button& cancelButton() { return cancel; }
    juce::Button& revertButton() { return revert; }
    const juce::Label& confirmLabel() const { return confirm; }
    const juce::Label& rateLabel() const { return rate; }
    const juce::Label& quantumLabel() const { return quantum; }
    const juce::Label& rateSourceLabel() const { return rateSource; }
    const juce::Label& bitDepthLabel() const { return bitDepth; }
    const juce::Label& loadLabel() const { return load; }
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
    void layoutSettings (juce::Rectangle<int>& area, int row);
    void layoutInfo (juce::Rectangle<int>& area, int line);
    void layoutMeasure (juce::Rectangle<int>& area, int row);

    HostView& view;
    juce::AudioDeviceManager& manager;
    juce::Label driverHeading, measureHeading, rate, quantum, rateSource, bitDepth, load, roundTrip, pending, analogChain, confirm;
    juce::TextButton measure { "MEASURE" }, measureAnalog { "MEASURE ANALOG CHAIN" }, apply { "APPLY" }, cancel { "CANCEL" },
        revert { "REVERT" };
    juce::Label typeCaption, inputCaption, outputCaption, rateCaption, blockCaption, periodsCaption;
    Dropdown typeBox, inputBox, outputBox, rateBox, blockBox, periodsBox;
    ClockPanel clock;
    std::optional<AudioTiming> chosenTiming;   // PipeWire: chosen on the page, not applied yet
    bool deviceChoosers = false;
    bool filling = false;
};
}
