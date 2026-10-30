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
    for (auto* label : { &rate, &quantum, &rateSource, &bitDepth, &roundTrip })
        styleValue (*label);
    roundTrip.setMinimumHorizontalScale (1.0f);
    look::help ("Estimated input-to-output round trip of what runs: the capture latency the backend reports for the "
                "engine's inputs, one period of the engine's processing, and the playback latency it reports for its "
                "outputs. A converter's own delay is included only as far as the driver reports it.", { &roundTrip });
    applyMessage.setColour (juce::Label::textColourId, palette::danger);
    pending.setColour (juce::Label::textColourId, palette::caution);
    rateSource.setColour (juce::Label::textColourId, palette::textMuted);
    bitDepth.setColour (juce::Label::textColourId, palette::textMuted);

    jackButton.setRadioGroupId (1);
    deviceButton.setRadioGroupId (1);
    for (auto* button : { &jackButton, &deviceButton })
        button->setClickingTogglesState (false);   // the config decides which one is on, not the click
    jackButton.onClick = [this] { chooseBackend (BackendSettings::Type::jack); };
    deviceButton.onClick = [this] { chooseBackend (BackendSettings::Type::device); };

    for (auto* child : std::initializer_list<juce::Component*> { &title, &backendCaption, &rate, &quantum, &rateSource, &bitDepth,
                                                                &roundTrip, &applyMessage, &pending, &jackButton, &deviceButton, &clock })
        addAndMakeVisible (child);
    const std::pair<juce::Label*, const char*> captions[] = { { &typeCaption, "Device type" }, { &inputCaption, "Input device" },
        { &outputCaption, "Output device" }, { &rateCaption, "Sample rate" }, { &blockCaption, "Block size" } };
    for (const auto& [label, text] : captions)
    {
        label->setText (text, juce::dontSendNotification);
        look::caption (*label);
        addChildComponent (*label);
    }
    for (auto* box : { &typeBox, &inputBox, &outputBox, &rateBox, &blockBox })
    {
        box->onChange = [this] { editDevice(); };
        addChildComponent (*box);
    }
    rebuildForBackend();
}

AudioPage::~AudioPage() = default;

namespace
{
constexpr int defaultItem = 1;   // an empty name: the backend takes the system's default device

void fill (juce::ComboBox& box, const juce::StringArray& names, const juce::String& current)
{
    box.clear (juce::dontSendNotification);
    box.addItem ("(default)", defaultItem);
    for (int i = 0; i < names.size(); ++i)
        box.addItem (names[i], i + 2);
    if (current.isNotEmpty() && ! names.contains (current))
        box.addItem (current + "  (not found)", names.size() + 2);
    const int index = names.indexOf (current);
    box.setSelectedId (current.isEmpty() ? defaultItem : index >= 0 ? index + 2 : names.size() + 2, juce::dontSendNotification);
}

juce::String chosenName (const juce::ComboBox& box)
{
    if (box.getSelectedId() == defaultItem)
        return {};
    return box.getText().upToFirstOccurrenceOf ("  (not found)", false, false);
}
}

void AudioPage::fillChoosers()
{
    const auto settings = view.config().backend;
    const auto& types = manager.getAvailableDeviceTypes();   // scans, opens nothing
    juce::StringArray typeNames, inputs, outputs;
    for (auto* type : types)
        typeNames.add (type->getTypeName());
    const auto* selected = types.isEmpty() ? nullptr : types[juce::jmax (0, typeNames.indexOf (settings.deviceType))];
    if (selected != nullptr)
    {
        inputs = selected->getDeviceNames (true);
        outputs = selected->getDeviceNames (false);
    }
    filling = true;
    fill (typeBox, typeNames, settings.deviceType);
    fill (inputBox, inputs, settings.inputDevice);
    fill (outputBox, outputs, settings.outputDevice);
    rateBox.clear (juce::dontSendNotification);
    int id = 1;
    for (const double candidate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        rateBox.addItem (juce::String (juce::roundToInt (candidate)) + " Hz", id);
        if (candidate == settings.sampleRate)
            rateBox.setSelectedId (id, juce::dontSendNotification);
        ++id;
    }
    blockBox.clear (juce::dontSendNotification);
    for (int frames = 64; frames <= 2048; frames *= 2)
    {
        blockBox.addItem (juce::String (frames) + " frames", frames);
        if (frames == settings.blockSize)
            blockBox.setSelectedId (frames, juce::dontSendNotification);
    }
    filling = false;
}

void AudioPage::editDevice()
{
    if (filling)
        return;
    auto config = view.config();
    auto& backend = config.backend;
    const auto newType = chosenName (typeBox);
    const bool typeChanged = newType != backend.deviceType;
    backend.deviceType = newType;
    backend.inputDevice = typeChanged ? juce::String() : chosenName (inputBox);
    backend.outputDevice = typeChanged ? juce::String() : chosenName (outputBox);
    if (rateBox.getSelectedId() > 0)
        backend.sampleRate = rateBox.getText().getDoubleValue();
    if (blockBox.getSelectedId() > 0)
        backend.blockSize = blockBox.getSelectedId();
    view.edit (config);
    if (typeChanged)
        fillChoosers();   // the other type's devices
    refresh();
}

void AudioPage::chooseBackend (BackendSettings::Type type)
{
    auto config = view.config();
    if (config.backend.type == type)
        return;
    config.backend.type = type;
    view.edit (config);
    rebuildForBackend();
}

void AudioPage::rebuildForBackend()
{
    const bool device = view.config().backend.type == BackendSettings::Type::device;
    jackButton.setToggleState (! device, juce::dontSendNotification);
    deviceButton.setToggleState (device, juce::dontSendNotification);
    deviceChoosers = device;
    if (device)
        fillChoosers();
    for (auto* child : std::initializer_list<juce::Component*> { &typeCaption, &inputCaption, &outputCaption, &rateCaption, &blockCaption,
                                                                &typeBox, &inputBox, &outputBox, &rateBox, &blockBox })
        child->setVisible (device);
    rate.setVisible (! device);
    quantum.setVisible (! device);
    refresh();
    resized();
}

void AudioPage::refresh()
{
    const auto status = view.status();
    const bool device = view.config().backend.type == BackendSettings::Type::device;
    const bool runsDevice = view.running().backend.type == BackendSettings::Type::device;   // the values below describe what runs
    pending.setText (device != runsDevice ? "not saved - applies after Save" : juce::String(), juce::dontSendNotification);
    rate.setText ("Sample rate   " + juce::String (juce::roundToInt (status.sampleRate)) + " Hz", juce::dontSendNotification);
    quantum.setText ("Quantum   " + juce::String (status.blockSize) + " frames", juce::dontSendNotification);
    rateSource.setText (status.rateSource, juce::dontSendNotification);
    bitDepth.setText ("Bit depth   " + juce::String (status.bitDepth) + (runsDevice ? "  -  set by the device" : "  -  float samples, set by JACK"),
                      juce::dontSendNotification);
    roundTrip.setText (roundTripText (estimatedRoundTrip (status)), juce::dontSendNotification);
    clock.show (view.clock());
    if (device != deviceChoosers)
        rebuildForBackend();
}

void AudioPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

// The round trip's line, its text shrunk to the width so the parts are never cut off.
void AudioPage::layoutRoundTrip (juce::Rectangle<int>& area, int row)
{
    const auto line = area.removeFromTop (row);
    roundTrip.setFont (juce::FontOptions (fittedTextHeight (roundTrip.getText(), (float) row * 0.4f, (float) line.getWidth() * 0.95f)));
    roundTrip.setBounds (line);
}

// The clock below the rest, in what is left of the page.
void AudioPage::layoutClock (juce::Rectangle<int>& area, int row)
{
    area.removeFromTop (row / 2);
    clock.setBounds (area);
}

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
    pending.setBounds (area.removeFromTop (row));
    area.removeFromTop (row / 2);

    if (deviceChoosers)
    {
        const auto line = [&] (juce::Label& caption, juce::ComboBox& box)
        {
            auto r = area.removeFromTop (row);
            caption.setBounds (r.removeFromLeft (r.getWidth() / 4));
            box.setBounds (r.reduced (2));
        };
        line (typeCaption, typeBox);
        line (inputCaption, inputBox);
        line (outputCaption, outputBox);
        line (rateCaption, rateBox);
        line (blockCaption, blockBox);
        rateSource.setBounds (area.removeFromTop (row));
        bitDepth.setBounds (area.removeFromTop (row));
        layoutRoundTrip (area, row);
        layoutClock (area, row);
        return;
    }
    rate.setBounds (area.removeFromTop (row));
    quantum.setBounds (area.removeFromTop (row));
    rateSource.setBounds (area.removeFromTop (row));
    bitDepth.setBounds (area.removeFromTop (row));
    layoutRoundTrip (area, row);
    layoutClock (area, row);
}
}
