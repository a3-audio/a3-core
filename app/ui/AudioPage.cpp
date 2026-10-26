// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioPage.h"

namespace a3app
{
namespace
{
constexpr float captionShare = 0.8f;   // type step: caption = body * 0.8

void styleValue (juce::Label& label)
{
    label.setEditable (false);   // read-only: the server or the device owns the value
    label.setColour (juce::Label::textColourId, palette::text);
    label.setJustificationType (juce::Justification::centredLeft);
}
}

AudioPage::AudioPage (HostView& v, juce::AudioDeviceManager& m) : view (v), manager (m)
{
    title.setText ("AUDIO", juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, palette::textMuted);
    backendCaption.setText ("Backend", juce::dontSendNotification);
    backendCaption.setColour (juce::Label::textColourId, palette::textMuted);
    for (auto* label : { &rate, &quantum, &rateSource, &bitDepth })
        styleValue (*label);
    applyMessage.setColour (juce::Label::textColourId, palette::danger);
    rateSource.setColour (juce::Label::textColourId, palette::textMuted);
    bitDepth.setColour (juce::Label::textColourId, palette::textMuted);

    jackButton.setRadioGroupId (1);
    deviceButton.setRadioGroupId (1);
    for (auto* button : { &jackButton, &deviceButton })
        button->setClickingTogglesState (false);   // the config decides which one is on, not the click
    jackButton.onClick = [this] { chooseBackend (BackendSettings::Type::jack); };
    deviceButton.onClick = [this] { chooseBackend (BackendSettings::Type::device); };

    for (auto* child : std::initializer_list<juce::Component*> { &title, &backendCaption, &rate, &quantum, &rateSource, &bitDepth,
                                                                &applyMessage, &jackButton, &deviceButton })
        addAndMakeVisible (child);
    rebuildForBackend();
}

AudioPage::~AudioPage() = default;

void AudioPage::chooseBackend (BackendSettings::Type type)
{
    auto config = view.config();
    if (config.backend.type == type)
        return;
    config.backend.type = type;
    const auto result = view.apply (config);
    applyMessage.setText (result.failed() ? result.getErrorMessage() : juce::String(), juce::dontSendNotification);
    if (result.wasOk())
        rebuildForBackend();
}

void AudioPage::rebuildForBackend()
{
    const bool device = view.config().backend.type == BackendSettings::Type::device;
    jackButton.setToggleState (! device, juce::dontSendNotification);
    deviceButton.setToggleState (device, juce::dontSendNotification);
    selector.reset();
    if (device)
    {
        selector = std::make_unique<juce::AudioDeviceSelectorComponent> (manager, 0, 64, 0, 64, false, false, false, false);
        addAndMakeVisible (*selector);
    }
    rate.setVisible (! device);
    quantum.setVisible (! device);
    refresh();
    resized();
}

void AudioPage::refresh()
{
    const auto status = view.status();
    const bool device = view.config().backend.type == BackendSettings::Type::device;
    rate.setText ("Sample rate   " + juce::String (juce::roundToInt (status.sampleRate)) + " Hz", juce::dontSendNotification);
    quantum.setText ("Quantum   " + juce::String (status.blockSize) + " frames", juce::dontSendNotification);
    rateSource.setText (status.rateSource, juce::dontSendNotification);
    bitDepth.setText ("Bit depth   " + juce::String (status.bitDepth) + (device ? "  -  set by the device" : "  -  float samples, set by JACK"),
                      juce::dontSendNotification);
    if (device != (selector != nullptr))
        rebuildForBackend();
}

void AudioPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void AudioPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    const int row = juce::jmax (24, getHeight() / 14);
    title.setBounds (area.removeFromTop (row));
    auto choice = area.removeFromTop (row);
    backendCaption.setBounds (choice.removeFromLeft (choice.getWidth() / 4));
    jackButton.setBounds (choice.removeFromLeft (choice.getWidth() / 3).reduced (2));
    deviceButton.setBounds (choice.removeFromLeft (choice.getWidth() / 2).reduced (2));
    applyMessage.setBounds (choice);
    area.removeFromTop (row / 2);

    if (selector != nullptr)
    {
        bitDepth.setBounds (area.removeFromBottom (row));
        rateSource.setBounds (area.removeFromBottom (row));
        selector->setBounds (area);
        return;
    }
    rate.setBounds (area.removeFromTop (row));
    quantum.setBounds (area.removeFromTop (row));
    rateSource.setBounds (area.removeFromTop (row));
    bitDepth.setBounds (area.removeFromTop (row));
}
}
