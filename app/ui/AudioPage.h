// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace a3app
{
// The audio page. JACK: rate and quantum are read-only (the server owns them) with where they
// come from. Device: JUCE's own selector for device, rate and block size, and the bit depth as
// the device reports it. The backend choice is an edit handed to HostView::apply.
class AudioPage final : public juce::Component
{
public:
    AudioPage (HostView&, juce::AudioDeviceManager&);
    ~AudioPage() override;

    void chooseBackend (BackendSettings::Type);
    void refresh();   // re-reads the view's status (the window's timer)

    bool showsDeviceSelector() const { return selector != nullptr; }
    const juce::Label& rateLabel() const { return rate; }
    const juce::Label& quantumLabel() const { return quantum; }
    const juce::Label& rateSourceLabel() const { return rateSource; }
    const juce::Label& bitDepthLabel() const { return bitDepth; }
    const juce::Label& pendingLabel() const { return pending; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void rebuildForBackend();

    HostView& view;
    juce::AudioDeviceManager& manager;
    juce::Label title, backendCaption, rate, quantum, rateSource, bitDepth, applyMessage, pending;
    juce::TextButton jackButton { "JACK" }, deviceButton { "Device" };
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
};
}
