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

AudioPage::AudioPage (HostView& v, juce::AudioDeviceManager& m) : view (v), manager (m)
{
    for (auto [heading, text] : { std::pair { &driverHeading, "DRIVER" }, std::pair { &measureHeading, "MEASURE" } })
    {
        heading->setText (text, juce::dontSendNotification);
        heading->setColour (juce::Label::textColourId, palette::textMuted);
    }
    for (auto* label : { &rate, &quantum, &rateSource, &bitDepth, &load, &roundTrip, &analogChain })
    {
        styleValue (*label);
        label->setMinimumHorizontalScale (0.6f);   // one font size: a long line is squeezed, never set smaller
    }
    look::help ("The analog chain (analog in, AD, engine, DA, analog out) is the digital loop as measured now plus the "
                "converters' share, measured once: MEASURE ANALOG CHAIN plays the same quiet test signal on the chosen "
                "output and the digital loop at once and stores the difference with its date. It never runs on its own. "
                "The pair is set on the I/O page.",
                { &analogChain });
    measureAnalog.onClick = [this] { view.measureAnalogChain(); };
    roundTrip.setMinimumHorizontalScale (1.0f);
    look::help ("Input-to-output round trip of what runs. Measured: a quiet test signal (-20 dBFS, 0.2 s) on the "
                "digital loop's output, found again on its input (the I/O page's digital loop, S/PDIF out to S/PDIF in), "
                "once after the start, after a new block or periods, and on MEASURE. The digital loop excludes the AD/DA "
                "converters: analog in to the speakers takes about 0.5-1 ms more. Estimated (without a measurement): the "
                "capture latency the backend reports, one period of the engine's processing, and the playback latency it reports.",
                { &roundTrip, &measure });
    measure.onClick = [this] { view.measureRoundTrip(); };
    pending.setColour (juce::Label::textColourId, palette::caution);
    confirm.setColour (juce::Label::textColourId, palette::caution);
    confirm.setMinimumHorizontalScale (0.5f);
    rateSource.setColour (juce::Label::textColourId, palette::textMuted);
    bitDepth.setColour (juce::Label::textColourId, palette::textMuted);
    load.setColour (juce::Label::textColourId, palette::textMuted);

    apply.onClick = [this] { applyChosen(); };
    cancel.onClick = [this] { chosenTiming.reset(); refresh(); };
    revert.onClick = [this] { revertTiming(); };
    look::help ("PipeWire: the block is the graph's quantum, forced by the engine and changed live. The periods are the "
                "sound card's ALSA buffer in blocks (2 is shorter, 3 is safer); a change restarts WirePlumber, and the "
                "audio stops for about two seconds. Nothing changes before APPLY; REVERT goes back to the previous choice. "
                "After a change the round trip is measured again; watch DSP load and xruns.",
                { &blockBox, &periodsBox, &apply, &revert, &confirm });

    for (auto* child : std::initializer_list<juce::Component*> { &driverHeading, &measureHeading, &rate, &quantum, &rateSource, &bitDepth,
                                                                &load, &roundTrip, &measure, &analogChain, &measureAnalog,
                                                                &pending, &clock, &typeCaption, &typeBox, &blockCaption, &blockBox })
        addAndMakeVisible (child);
    for (auto* child : std::initializer_list<juce::Component*> { &inputCaption, &outputCaption, &rateCaption, &periodsCaption,
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
    for (auto* box : { &inputBox, &outputBox, &rateBox })
    {
        box->onChange = [this] { editDevice(); };
        addChildComponent (*box);
    }
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
    for (auto* child : std::initializer_list<juce::Component*> { &inputCaption, &outputCaption, &rateCaption, &inputBox, &outputBox, &rateBox })
        child->setVisible (device);
    for (auto* child : std::initializer_list<juce::Component*> { &periodsCaption, &periodsBox, &confirm, &revert })
        child->setVisible (! device);
    refresh();
    resized();
}

void AudioPage::showTiming (const BackendStatus& status)
{
    const bool runsDevice = view.running().backend.type == BackendSettings::Type::device;
    const int periods = status.timing.available ? status.timing.applied.periods : view.running().backend.periods;
    quantum.setText (runsDevice ? "Block " + juce::String (status.blockSize) + " frames" : timingText (status.blockSize, periods),
                     juce::dontSendNotification);
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
    analogChain.setText (analogChainText (estimatedRoundTrip (status), measured)
                             + (chosen ? juce::String() : juce::String ("  -  no analog test pair (I/O page)")),
                         juce::dontSendNotification);
    // What to plug first: the key's help (the line under it is gone, 2026-10-30).
    measureAnalog.setTooltip (chosen ? "Before MEASURE ANALOG CHAIN: plug a temporary cable from output " + juce::String (pair->output)
                                           + " to input " + juce::String (pair->input) + "; input " + juce::String (pair->input)
                                           + " must be free (not in use). The test signal is -20 dBFS, on output " + juce::String (pair->output) + " only."
                                     : juce::String ("Set the analog test pair on the I/O page first."));
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
    rate.setText ("Sample rate   " + juce::String (juce::roundToInt (status.sampleRate)) + " Hz", juce::dontSendNotification);
    rateSource.setText (status.rateSource, juce::dontSendNotification);
    bitDepth.setText ("Bit depth   " + juce::String (status.bitDepth) + (runsDevice ? "  -  set by the device" : "  -  float samples, set by JACK"),
                      juce::dontSendNotification);
    load.setText ("DSP load   " + juce::String (juce::roundToInt (status.dspLoadPercent)) + " %      xruns " + juce::String (status.xruns),
                  juce::dontSendNotification);
    showTiming (status);
    showMeasurements (status);
    clock.show (view.clock());
    resized();
}

void AudioPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

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

// What runs, one compact block: a line of text each, no gap between them.
void AudioPage::layoutInfo (juce::Rectangle<int>& area, int line)
{
    for (auto* label : { &rate, &quantum, &rateSource, &bitDepth, &load })
        label->setBounds (area.removeFromTop (line));
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

// DRIVER, MEASURE, CLOCK top to bottom; every text at one height, a share of the row.
void AudioPage::resized()
{
    auto page = getLocalBounds().reduced (getWidth() / 40);
    const int settingsRows = deviceChoosers ? 5 : 4;
    const int rows = settingsRows + 2 + 2 + 1;   // the settings, the two key rows of MEASURE, two headings, the pending line
    const int infoLines = 5;
    // The clock keeps at least a quarter of the page; the rest is rows (a heading or an info line is 0.6 of one).
    const int clockHeight = juce::jmax (page.getHeight() / 4, juce::jmin (page.getHeight() / 2, 6 * 24));
    const int available = page.getHeight() - clockHeight - page.getHeight() / 40;
    const int row = juce::jmax (12, juce::jmin (getHeight() / 14, (int) ((float) available / ((float) rows + 0.6f * (float) infoLines + 0.5f))));
    const int line = row * 3 / 5;
    const auto text = juce::FontOptions ((float) row * 0.36f);
    for (auto* label : { &driverHeading, &measureHeading, &rate, &quantum, &rateSource, &bitDepth, &load, &roundTrip, &pending,
                         &analogChain, &confirm, &typeCaption, &inputCaption, &outputCaption, &rateCaption, &blockCaption, &periodsCaption })
        label->setFont (text);
    clock.setTextHeight ((float) row * 0.36f);

    auto area = page;
    driverHeading.setBounds (area.removeFromTop (line));
    layoutSettings (area, row);
    pending.setBounds (area.removeFromTop (pending.getText().isEmpty() ? 0 : line));
    layoutInfo (area, line);
    area.removeFromTop (row / 4);
    measureHeading.setBounds (area.removeFromTop (line));
    layoutMeasure (area, row);
    area.removeFromTop (row / 4);
    // CLOCK under MEASURE, down to the page's end (never less than the share reserved above).
    clock.setBounds (area.getHeight() >= clockHeight ? area : page.withTop (page.getBottom() - clockHeight));
}
}
