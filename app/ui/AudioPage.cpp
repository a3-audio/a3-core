// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioPage.h"

namespace a3app
{
namespace
{
constexpr int pipewireItem = 1;   // the type list: PipeWire (JACK) first, then JUCE's types from 2
constexpr int defaultItem = 1;    // the device lists: an empty name, the backend takes the system's default device

void styleValue (juce::Label& label)
{
    label.setEditable (false);   // read-only: the server or the device owns the value
    label.setColour (juce::Label::textColourId, palette::text);
    label.setJustificationType (juce::Justification::centredLeft);
}

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

const juce::String restartWarning = "the audio stops for about two seconds while WirePlumber restarts";
}

AudioPage::AudioPage (HostView& v, juce::AudioDeviceManager& m) : view (v), manager (m), channels (v)
{
    for (auto [heading, text] : { std::pair { &driverHeading, "DRIVER" }, std::pair { &channelsHeading, "CHANNELS" },
                                  std::pair { &measureHeading, "MEASURE" } })
    {
        heading->setText (text, juce::dontSendNotification);
        heading->setColour (juce::Label::textColourId, palette::textMuted);
    }
    for (auto* label : { &roundTrip, &analogChain })
    {
        styleValue (*label);
        label->setMinimumHorizontalScale (0.6f);   // one font size: a long line is squeezed, never set smaller
    }
    measureAnalog.onClick = [this] { view.measureAnalogChain(); };
    roundTrip.setMinimumHorizontalScale (1.0f);
    measure.onClick = [this] { view.measureRoundTrip(); };
    pending.setColour (juce::Label::textColourId, palette::caution);
    confirm.setColour (juce::Label::textColourId, palette::caution);
    confirm.setMinimumHorizontalScale (0.5f);

    apply.onClick = [this] { applyChosen(); };
    cancel.onClick = [this] { chosenTiming.reset(); refresh(); };
    revert.onClick = [this] { revertTiming(); };

    for (auto* child : std::initializer_list<juce::Component*> { &driverHeading, &channelsHeading, &measureHeading, &roundTrip, &measure,
                                                                &analogChain, &measureAnalog, &pending, &clock, &channels, &typeCaption,
                                                                &typeBox, &rateCaption, &rateBox, &blockCaption, &blockBox })
        addAndMakeVisible (child);
    for (auto* child : std::initializer_list<juce::Component*> { &inputCaption, &outputCaption, &periodsCaption,
                                                                &periodsBox, &confirm, &apply, &cancel, &revert })
        addChildComponent (child);
    const std::pair<juce::Label*, const char*> captions[] = { { &typeCaption, "Device type" }, { &inputCaption, "Input device" },
        { &outputCaption, "Output device" }, { &rateCaption, "Sample rate" }, { &blockCaption, "Block" }, { &periodsCaption, "Periods" } };
    for (const auto& [label, text] : captions)
    {
        label->setText (text, juce::dontSendNotification);
        look::caption (*label);
    }
    typeBox.onChange = [this] { if (! filling) chooseDeviceType (typeBox.getText()); };
    for (auto* box : { &inputBox, &outputBox })
    {
        box->onChange = [this] { editDevice(); };
        addChildComponent (*box);
    }
    rateBox.onChange = [this] { if (deviceChoosers) editDevice(); };   // PipeWire's rate is the graph's: its one choice
    blockBox.onChange = [this] { if (! filling && blockBox.getSelectedId() > 0) chooseBlock (blockBox.getSelectedId()); };
    periodsBox.onChange = [this] { if (! filling && periodsBox.getSelectedId() > 0) choosePeriods (periodsBox.getSelectedId()); };
    rebuildForType();
}

AudioPage::~AudioPage() = default;

void AudioPage::fillTypes()
{
    const auto settings = view.config().backend;
    const auto& types = manager.getAvailableDeviceTypes();   // scans, opens nothing
    filling = true;
    typeBox.clear (juce::dontSendNotification);
    typeBox.addItem (pipewireType, pipewireItem);
    int chosen = pipewireItem;
    for (int i = 0; i < types.size(); ++i)
    {
        const auto name = types[i]->getTypeName();
        typeBox.addItem (name, i + 2);
        const bool isTheDevice = name == settings.deviceType || (settings.deviceType.isEmpty() && i == 0);
        if (settings.type == BackendSettings::Type::device && isTheDevice)
            chosen = i + 2;
    }
    typeBox.setSelectedId (chosen, juce::dontSendNotification);
    filling = false;
}

void AudioPage::fillChoosers()
{
    const auto settings = view.config().backend;
    const auto& types = manager.getAvailableDeviceTypes();
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
        blockBox.addItem (juce::String (frames) + " frames", frames);
    blockBox.setSelectedId (settings.blockSize, juce::dontSendNotification);
    filling = false;
}

// PipeWire: the block and periods the page offers, showing the choice not applied yet, else what runs.
void AudioPage::fillTiming()
{
    const auto shown = shownTiming();
    filling = true;
    if (blockBox.getNumItems() != (int) pipewireBlocks().size() || blockBox.getItemId (0) != pipewireBlocks().front())
    {
        blockBox.clear (juce::dontSendNotification);
        for (const int frames : pipewireBlocks())
            blockBox.addItem (juce::String (frames) + " frames", frames);
    }
    if (periodsBox.getNumItems() == 0)
        for (const int periods : pipewirePeriods())
            periodsBox.addItem (juce::String (periods) + " periods", periods);
    blockBox.setSelectedId (shown.block, juce::dontSendNotification);
    periodsBox.setSelectedId (shown.periods, juce::dontSendNotification);
    filling = false;
    fillPipeWireRate();
}

// PipeWire: the graph's rate, the one item (the dropdown shows what runs; the engine does not change PipeWire's rate).
void AudioPage::fillPipeWireRate()
{
    const int running = juce::roundToInt (view.status().sampleRate);
    if (rateBox.getNumItems() == 1 && rateBox.getItemId (0) == running)
        return;
    filling = true;
    rateBox.clear (juce::dontSendNotification);
    if (running > 0)
        rateBox.addItem (juce::String (running) + " Hz", running);
    rateBox.setSelectedId (running, juce::dontSendNotification);
    filling = false;
}

void AudioPage::editDevice()
{
    if (filling)
        return;
    auto config = view.config();
    auto& backend = config.backend;
    backend.inputDevice = chosenName (inputBox);
    backend.outputDevice = chosenName (outputBox);
    if (rateBox.getSelectedId() > 0)
        backend.sampleRate = rateBox.getText().getDoubleValue();
    if (blockBox.getSelectedId() > 0)
        backend.blockSize = blockBox.getSelectedId();
    view.edit (config);
    refresh();
}

void AudioPage::chooseDeviceType (const juce::String& name)
{
    auto config = view.config();
    auto& backend = config.backend;
    if (name == pipewireType)
    {
        if (backend.type == BackendSettings::Type::jack)
            return;
        backend.type = BackendSettings::Type::jack;
    }
    else
    {
        if (backend.type == BackendSettings::Type::device && backend.deviceType == name)
            return;
        if (backend.deviceType != name)
            backend.inputDevice = backend.outputDevice = {};   // the other type's devices
        backend.type = BackendSettings::Type::device;
        backend.deviceType = name;
    }
    chosenTiming.reset();
    view.edit (config);
    rebuildForType();
}

bool AudioPage::timingLive() const
{
    const auto status = view.status();
    return view.running().backend.type == BackendSettings::Type::jack && view.config().backend.type == BackendSettings::Type::jack
           && status.timing.available && ! status.timing.restarting;
}

AudioTiming AudioPage::currentTiming() const
{
    const auto status = view.status();
    return status.timing.available ? status.timing.applied : timingOf (view.running().backend);
}

AudioTiming AudioPage::shownTiming() const { return chosenTiming.value_or (currentTiming()); }

void AudioPage::choose (const AudioTiming& timing)
{
    if (timing == currentTiming())
        chosenTiming.reset();
    else
        chosenTiming = timing;
    refresh();
}

void AudioPage::chooseBlock (int frames)
{
    if (deviceChoosers)
    {
        filling = true;
        blockBox.setSelectedId (frames, juce::dontSendNotification);
        filling = false;
        return editDevice();
    }
    auto timing = shownTiming();
    timing.block = frames;
    choose (timing);
}

void AudioPage::choosePeriods (int periods)
{
    auto timing = shownTiming();
    timing.periods = periods;
    choose (timing);
}

void AudioPage::applyChosen()
{
    if (! chosenTiming || ! timingLive())
        return;
    view.applyTiming (*chosenTiming);
    chosenTiming.reset();
    refresh();
}

// A previous block comes back at once (it is live); previous periods restart WirePlumber, so the page asks first.
void AudioPage::revertTiming()
{
    const auto previous = view.status().timing.previous;
    if (! previous || ! timingLive())
        return;
    if (previous->periods != currentTiming().periods)
        return choose (*previous);
    chosenTiming.reset();
    view.applyTiming (*previous);
    refresh();
}

void AudioPage::rebuildForType()
{
    const bool device = view.config().backend.type == BackendSettings::Type::device;
    deviceChoosers = device;
    fillTypes();
    if (device)
        fillChoosers();
    else
        fillTiming();
    for (auto* child : std::initializer_list<juce::Component*> { &inputCaption, &outputCaption, &inputBox, &outputBox })
        child->setVisible (device);
    for (auto* child : std::initializer_list<juce::Component*> { &periodsCaption, &periodsBox, &confirm, &revert })
        child->setVisible (! device);
    refresh();
    resized();
}

void AudioPage::showTiming (const BackendStatus& status)
{
    if (deviceChoosers)
        return;

    fillTiming();
    const bool live = timingLive();
    blockBox.setEnabled (live);
    periodsBox.setEnabled (live);
    revert.setEnabled (live && status.timing.previous.has_value());
    const bool asking = chosenTiming.has_value() && live;
    apply.setVisible (asking);
    cancel.setVisible (asking);
    juce::String line;
    if (asking)
        line = chosenTiming->periods != currentTiming().periods
                   ? "Periods " + juce::String (chosenTiming->periods) + ": " + restartWarning + ". APPLY?"
                   : "Block " + juce::String (chosenTiming->block) + " frames applies live, without a restart. APPLY?";
    else if (status.timing.restarting)
        line = "WirePlumber is restarting; the device comes back on its own";
    else if (! status.timing.available)
        line = "Block and periods apply once the engine runs on PipeWire (Save)";
    else
        line = status.timing.message;
    confirm.setText (line, juce::dontSendNotification);
    confirm.setColour (juce::Label::textColourId, asking || status.timing.restarting ? palette::caution : palette::textMuted);
}

void AudioPage::showMeasurements (const BackendStatus& status)
{
    using State = RoundTripReport::State;
    const auto& measured = status.roundTripMeasured;
    const bool loopSet = view.running().io.roundTripLoop.has_value();
    roundTrip.setText (roundTripText (estimatedRoundTrip (status), measured)
                           + (loopSet ? juce::String() : juce::String ("  -  no digital loop (I/O page)")),
                       juce::dontSendNotification);
    measure.setEnabled (loopSet && ! measured.measuring && measured.state != State::noLoop && measured.state != State::notReported);
    const auto pair = view.running().io.analogLoop;
    const bool chosen = pair && pair->output > 0 && pair->input > 0;
    const bool converted = measured.analog == RoundTripReport::Analog::measured;
    analogChain.setText (analogChainText (estimatedRoundTrip (status), measured)
                             + (! chosen ? juce::String ("  -  no analog test pair (I/O page)")
                                         : converted ? juce::String()
                                                     : "  -  cable output " + juce::String (pair->output) + " to input " + juce::String (pair->input)),
                         juce::dontSendNotification);
    measureAnalog.setEnabled (chosen && loopSet && ! measured.analogMeasuring && ! measured.measuring && measured.state != State::noLoop
                              && measured.state != State::notReported);
}

void AudioPage::refresh()
{
    const auto status = view.status();
    const bool device = view.config().backend.type == BackendSettings::Type::device;
    if (device != deviceChoosers)
        return rebuildForType();
    const bool runsDevice = view.running().backend.type == BackendSettings::Type::device;   // the values below describe what runs
    pending.setText (device != runsDevice ? "not saved - applies after Save" : juce::String(), juce::dontSendNotification);
    if (! device)
        fillPipeWireRate();
    showTiming (status);
    channels.refresh();
    showMeasurements (status);
    clock.show (view.clock());
    resized();
}

// The cards on the page's surface, as ZONES' info card (look::card); CLOCK paints its own.
void AudioPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    for (const auto& card : { driverCard, channelsArea, measureCard })
        look::card (g, card, getWidth());
}

// The settings: the device type, then a JUCE type's devices, rate and block, or PipeWire's block and periods with
// the question before APPLY.
void AudioPage::layoutSettings (juce::Rectangle<int>& area, int row)
{
    const auto line = [&] (juce::Label& caption, juce::Component& box)
    {
        auto r = area.removeFromTop (row);
        caption.setBounds (r.removeFromLeft (r.getWidth() / 4));
        box.setBounds (r.reduced (2));
    };
    line (typeCaption, typeBox);
    if (deviceChoosers)
    {
        line (inputCaption, inputBox);
        line (outputCaption, outputBox);
        line (rateCaption, rateBox);
        line (blockCaption, blockBox);
        return;
    }
    line (rateCaption, rateBox);
    line (blockCaption, blockBox);
    line (periodsCaption, periodsBox);
    auto buttons = area.removeFromTop (row);
    revert.setBounds (buttons.removeFromRight (buttons.getWidth() / 6).reduced (2));
    if (apply.isVisible())
    {
        cancel.setBounds (buttons.removeFromRight (buttons.getWidth() / 5).reduced (2));
        apply.setBounds (buttons.removeFromRight (buttons.getWidth() / 4).reduced (2));
    }
    confirm.setBounds (buttons);
}

// The round trip and the analog chain, each a row with its key at the right.
void AudioPage::layoutMeasure (juce::Rectangle<int>& area, int row)
{
    auto line = area.removeFromTop (row);
    measure.setBounds (line.removeFromRight (line.getWidth() / 6).reduced (2));
    roundTrip.setBounds (line);
    auto analogLine = area.removeFromTop (row);
    measureAnalog.setBounds (analogLine.removeFromRight (analogLine.getWidth() / 4).reduced (2));
    analogChain.setBounds (analogLine);
}

// DRIVER, CHANNELS, MEASURE and CLOCK cards top to bottom; every text at one height, a share of the row.
void AudioPage::resized()
{
    auto page = getLocalBounds().reduced (getWidth() / 40);
    const int gap = juce::jmax (2, getHeight() / 80);
    const int driverRows = deviceChoosers ? 6 : 5;   // the type, the devices or not, rate, block, periods, the APPLY line
    const int rows = 1 + driverRows + 1 + 1 + 3 + 1 + 2 + 3;   // headings, DRIVER, pending, CHANNELS, MEASURE, CLOCK
    const int inset = juce::jmax (2, getWidth() / 60);
    // Four cards (each with an inset above and below) and the gaps between them must fit, whatever the window's shape.
    const int row = juce::jmax (8, juce::jmin (getHeight() / 14, (page.getHeight() - 3 * gap - 8 * inset) / (rows + 1)));
    const auto text = juce::FontOptions ((float) row * 0.36f);
    for (auto* label : { &driverHeading, &channelsHeading, &measureHeading, &roundTrip, &pending, &analogChain, &confirm, &typeCaption,
                         &inputCaption, &outputCaption, &rateCaption, &blockCaption, &periodsCaption })
        label->setFont (text);
    channels.setTextHeight ((float) row * 0.36f);
    clock.setTextHeight ((float) row * 0.36f);

    const auto card = [&] (int contentRows, int extra) { return page.removeFromTop (contentRows * row + extra + 2 * inset); };
    driverCard = card (driverRows + 1, pending.getText().isEmpty() ? 0 : row * 3 / 5);
    page.removeFromTop (gap);
    auto area = driverCard.reduced (inset);
    driverHeading.setBounds (area.removeFromTop (row));
    layoutSettings (area, row);
    pending.setBounds (area);

    channelsArea = card (4, 0);
    page.removeFromTop (gap);
    area = channelsArea.reduced (inset);
    channelsHeading.setBounds (area.removeFromTop (row));
    channels.setBounds (area);

    measureCard = card (3, 0);
    page.removeFromTop (gap);
    area = measureCard.reduced (inset);
    measureHeading.setBounds (area.removeFromTop (row));
    layoutMeasure (area, row);

    clock.setBounds (page.withHeight (juce::jmin (page.getHeight(), 3 * row + 2 * inset)));
}
}
