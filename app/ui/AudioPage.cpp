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
}

AudioPage::AudioPage (HostView& v, juce::AudioDeviceManager& m) : view (v), manager (m)
{
    title.setText ("AUDIO", juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, palette::textMuted);
    for (auto* label : { &rate, &quantum, &rateSource, &bitDepth, &load, &roundTrip, &analogChain })
        styleValue (*label);
    for (auto* label : { &analogHint, &measureHint })
    {
        look::caption (*label);
        label->setMinimumHorizontalScale (0.5f);
    }
    look::help ("The analog chain (analog in, AD, engine, DA, analog out) is the digital loop as measured now plus the "
                "converters' share, measured once: MEASURE ANALOG CHAIN plays the same quiet test signal on the chosen "
                "output and the digital loop at once and stores the difference with its date. It never runs on its own. "
                "The pair is set on the I/O page.",
                { &analogChain, &analogHint, &measureAnalog });
    measureAnalog.onClick = [this] { view.measureAnalogChain(); };
    roundTrip.setMinimumHorizontalScale (1.0f);
    look::help ("Input-to-output round trip of what runs. Measured: a quiet test signal (-20 dBFS, 0.2 s) on the "
                "digital loop's output, found again on its input (the I/O page's digital loop, S/PDIF out to S/PDIF in), "
                "once after the start, after a new block, and on MEASURE. The digital loop excludes the AD/DA "
                "converters: analog in to the speakers takes about 0.5-1 ms more. Estimated (without a measurement): the "
                "capture latency the backend reports, one period of the engine's processing, and the playback latency it reports.",
                { &roundTrip, &measure, &measureHint });
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
    look::help ("PipeWire: the block is the graph's quantum, forced by the engine and changed live. Nothing changes "
                "before APPLY; REVERT goes back to the previous choice. After a change the round trip is measured again; "
                "watch DSP load and xruns.",
                { &blockBox, &apply, &revert, &confirm });

    for (auto* child : std::initializer_list<juce::Component*> { &title, &rate, &quantum, &rateSource, &bitDepth, &load, &roundTrip,
                                                                &measure, &measureHint, &analogChain, &analogHint, &measureAnalog,
                                                                &pending, &clock, &typeCaption, &typeBox, &blockCaption, &blockBox })
        addAndMakeVisible (child);
    for (auto* child : std::initializer_list<juce::Component*> { &inputCaption, &outputCaption, &rateCaption, &confirm, &apply,
                                                                &cancel, &revert })
        addChildComponent (child);
    const std::pair<juce::Label*, const char*> captions[] = { { &typeCaption, "Device type" }, { &inputCaption, "Input device" },
        { &outputCaption, "Output device" }, { &rateCaption, "Sample rate" }, { &blockCaption, "Block" } };
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

// PipeWire: the blocks the page offers, showing the choice not applied yet, else what runs.
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
    blockBox.setSelectedId (shown.block, juce::dontSendNotification);
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
           && status.timing.available;
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

void AudioPage::applyChosen()
{
    if (! chosenTiming || ! timingLive())
        return;
    view.applyTiming (*chosenTiming);
    chosenTiming.reset();
    refresh();
}

// A previous block comes back at once (it is live).
void AudioPage::revertTiming()
{
    const auto previous = view.status().timing.previous;
    if (! previous || ! timingLive())
        return;
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
    for (auto* child : std::initializer_list<juce::Component*> { &confirm, &revert })
        child->setVisible (! device);
    refresh();
    resized();
}

void AudioPage::showTiming (const BackendStatus& status)
{
    quantum.setText (timingText (status.blockSize), juce::dontSendNotification);
    if (deviceChoosers)
        return;

    fillTiming();
    const bool live = timingLive();
    blockBox.setEnabled (live);
    revert.setEnabled (live && status.timing.previous.has_value());
    const bool asking = chosenTiming.has_value() && live;
    apply.setVisible (asking);
    cancel.setVisible (asking);
    juce::String line;
    if (asking)
        line = "Block " + juce::String (chosenTiming->block) + " frames applies live, without a restart. APPLY?";
    else if (! status.timing.available)
        line = "The block applies once the engine runs on PipeWire (Save)";
    else
        line = status.timing.message;
    confirm.setText (line, juce::dontSendNotification);
    confirm.setColour (juce::Label::textColourId, asking ? palette::caution : palette::textMuted);
}

void AudioPage::showMeasurements (const BackendStatus& status)
{
    roundTrip.setText (roundTripText (estimatedRoundTrip (status), status.roundTripMeasured), juce::dontSendNotification);
    using State = RoundTripReport::State;
    const auto& measured = status.roundTripMeasured;
    const bool loopSet = view.running().io.roundTripLoop.has_value();
    measure.setEnabled (loopSet && ! measured.measuring && measured.state != State::noLoop && measured.state != State::notReported);
    measureHint.setText (loopSet ? juce::String() : juce::String ("set the digital loop on the I/O page"), juce::dontSendNotification);
    measureHint.setVisible (! loopSet);
    analogChain.setText (analogChainText (estimatedRoundTrip (status), measured), juce::dontSendNotification);
    const auto pair = view.running().io.analogLoop;
    const bool chosen = pair && pair->output > 0 && pair->input > 0;
    analogHint.setText (chosen ? "Before MEASURE ANALOG CHAIN: plug a temporary cable from output " + juce::String (pair->output)
                                     + " to input " + juce::String (pair->input) + "; input " + juce::String (pair->input)
                                     + " must be free (not in use). The test signal is -20 dBFS, on output " + juce::String (pair->output) + " only."
                               : juce::String ("set the analog test pair on the I/O page"),
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

// The settings: the device type, then a JUCE type's devices, rate and block, or PipeWire's block with
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
    auto buttons = area.removeFromTop (row);
    revert.setBounds (buttons.removeFromRight (buttons.getWidth() / 6).reduced (2));
    if (apply.isVisible())
    {
        cancel.setBounds (buttons.removeFromRight (buttons.getWidth() / 5).reduced (2));
        apply.setBounds (buttons.removeFromRight (buttons.getWidth() / 4).reduced (2));
    }
    confirm.setBounds (buttons);
}

// What runs, below the settings: every line one row, MEASURE and MEASURE ANALOG CHAIN beside theirs.
void AudioPage::layoutInfo (juce::Rectangle<int>& area, int row)
{
    for (auto* label : { &rate, &quantum, &rateSource, &bitDepth, &load })
        label->setBounds (area.removeFromTop (row));
    auto line = area.removeFromTop (row);
    measure.setBounds (line.removeFromRight (line.getWidth() / 6).reduced (2));
    roundTrip.setFont (juce::FontOptions (fittedTextHeight (roundTrip.getText(), (float) row * 0.4f, (float) line.getWidth() * 0.95f)));
    roundTrip.setBounds (line);
    measureHint.setBounds (measureHint.isVisible() ? area.removeFromTop (row) : juce::Rectangle<int>());
    auto analogLine = area.removeFromTop (row);
    measureAnalog.setBounds (analogLine.removeFromRight (analogLine.getWidth() / 4).reduced (2));
    analogChain.setFont (juce::FontOptions (fittedTextHeight (analogChain.getText(), (float) row * 0.4f, (float) analogLine.getWidth() * 0.95f)));
    analogChain.setBounds (analogLine);
    analogHint.setBounds (area.removeFromTop (row));
}

void AudioPage::resized()
{
    auto page = getLocalBounds().reduced (getWidth() / 40);
    // The clock at the bottom, reserved before anything else: it keeps at least a quarter of the page.
    clock.setBounds (page.removeFromBottom (juce::jmax (page.getHeight() / 4, juce::jmin (page.getHeight() / 2, 6 * 24))));
    page.removeFromBottom (page.getHeight() / 40);
    const int settingsRows = deviceChoosers ? 5 : 4;
    const int infoRows = 8 + (measureHint.isVisible() ? 1 : 0);
    const int rows = 2 + settingsRows + infoRows;   // title and the pending line besides
    const int row = juce::jmax (12, juce::jmin (getHeight() / 14, page.getHeight() / rows));
    auto area = page;
    title.setBounds (area.removeFromTop (row));
    layoutSettings (area, row);
    pending.setBounds (area.removeFromTop (row));
    layoutInfo (area, row);
}
}
