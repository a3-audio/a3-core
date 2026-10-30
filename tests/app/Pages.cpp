// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <gtest/gtest.h>
#include <set>
#include <juce_opengl/juce_opengl.h>
#include "AppTruth.h"
#include "CoreWindowRig.h"
#include "EngineFixture.h"
#include "FakeView.h"
#include "PageSnapshot.h"
#include "RemoteRig.h"
#include "AudioPage.h"
#include "CalibrationPage.h"
#include "ChannelsPage.h"
#include "ConfigEdits.h"
#include "IoPage.h"
#include "Look.h"
#include "MainWindow.h"
#include "SaveBar.h"
#include "SpeakersPage.h"
#include "SpatialTestData.h"
#include "StatusPage.h"
#include "ZonesPage.h"
#include "Dropdown.h"
#include "WorkspaceSwitch.h"
#include <array>
#include <map>

namespace
{
using a3apptest::FakeView;
using a3apptest::RemoteRig;
using a3apptest::snapshot;
using a3apptest::expectPainted;
using a3apptest::NoDevices;
using a3apptest::CoreWindowRig;

struct Pages : ::testing::Test
{
    juce::MessageManagerLock lock;   // the test runner's dispatch thread must stay out of component code
    a3app::Look look;
    juce::AudioDeviceManager manager;
    Pages()
    {
        manager.addAudioDeviceType (std::make_unique<NoDevices>());
        juce::LookAndFeel::setDefaultLookAndFeel (&look);   // the pages' colours, as the window sets them
    }
    ~Pages() override { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
};
}

TEST_F (Pages, AudioPagePaintsForJackAndDevice)
{
    FakeView view;
    {
        a3app::AudioPage page (view, manager);
        expectPainted (snapshot (page, "audio-jack"));
        EXPECT_FALSE (page.showsDeviceSelector());
    }
    view.runDevice();
    view.state.rateSource = "set by the device";
    view.state.bitDepth = 24;
    a3app::AudioPage page (view, manager);
    expectPainted (snapshot (page, "audio-device"));
    EXPECT_TRUE (page.showsDeviceSelector());
    EXPECT_NE (a3app::driverInfo (view).bitDepth.indexOf ("set by the device"), -1);   // on STATUS since 2026-10-30
}

// AUDIO's MEASURE (third round): "Digital roundtrip" and its value only, no estimate in brackets; the estimate with its
// parts is STATUS' ROUND TRIP line, which follows a new quantum.
TEST_F (Pages, AudioSaysTheDigitalRoundtripAndStatusTheEstimate)
{
    FakeView view;
    view.state.captureLatency = 256;
    view.state.playbackLatency = 384;
    view.cfg.io.roundTripLoop = view.runningCfg.io.roundTripLoop = a3app::RoundTripLoop {};
    view.state.roundTripMeasured.state = a3app::RoundTripReport::State::notMeasured;
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    page.refresh();
    juce::StringArray captions;
    for (auto* child : page.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
            captions.add (label->getText());
    EXPECT_TRUE (captions.contains ("Digital roundtrip")) << captions.joinIntoString (", ");
    EXPECT_TRUE (captions.contains ("Analog roundtrip")) << captions.joinIntoString (", ");
    EXPECT_EQ (page.roundTripLabel().getText(), "not measured yet");
    EXPECT_FALSE (page.roundTripLabel().getText().containsAnyOf ("()")) << "no brackets";
    EXPECT_TRUE (a3app::driverInfo (view).roundTrip.contains ("768 samples")) << a3app::driverInfo (view).roundTrip;   // 256 + 128 + 384
    view.state.blockSize = 256;
    EXPECT_TRUE (a3app::driverInfo (view).roundTrip.contains ("896 samples")) << a3app::driverInfo (view).roundTrip;
    expectPainted (a3apptest::snapshotAt (page, "audio-roundtrip-jack", page.getWidth(), page.getHeight()));
}

namespace
{
a3clock::ClockReport clockReport (a3clock::Health health, const char* reason)
{
    a3clock::ClockReport report;
    report.running = true;
    report.status.health = health;
    report.status.reason = reason;
    report.status.device = "/dev/ptp0";
    report.status.interface = "enp5s0";
    report.status.portState = "MASTER";
    report.status.frequencyPpm = 14.1023;
    report.status.errorNs = 2'345.0;
    report.status.ratePpm = 0.012;
    report.status.lockedSeconds = health == a3clock::Health::locked ? 3725.0 : 0.0;
    report.status.counters.corrections = 3990;
    report.status.counters.reanchors = 1;
    report.status.scarlettBreaks = 1;
    report.status.observeOnly = health == a3clock::Health::observing;
    return report;
}

int pixelsOf (const juce::Image& image, juce::Colour colour)
{
    int n = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            n += image.getPixelAt (x, y).getARGB() == colour.getARGB();
    return n;
}
}

// The one clock on the AUDIO page (2026-10-30): its state in every case the role can be in, and a
// warning frame whenever the PTP clock is not locked to the Scarlett.

TEST_F (Pages, StatusShowsTheClockInEveryState)
{
    struct Case
    {
        const char* name;
        a3clock::ClockReport report;
        const char* word;
        juce::Colour warning;   // transparent: no warning
    };
    a3clock::ClockReport absent;
    absent.why = "the clock role is not running (a3-clock.service): nothing steers the PTP clock";
    const Case cases[] = {
        { "locked", clockReport (a3clock::Health::locked, "the PTP clock follows the Scarlett"), "LOCKED", juce::Colours::transparentBlack },
        { "acquiring", clockReport (a3clock::Health::acquiring, "anchored: steering towards the lock"), "ACQUIRING", a3app::palette::caution },
        { "observing", clockReport (a3clock::Health::observing, "observing"), "OBSERVING", a3app::palette::caution },
        { "locked-role-not-read", [] { auto r = clockReport (a3clock::Health::locked, ""); r.status.portState.clear(); return r; }(), "LOCKED",
          juce::Colours::transparentBlack },
        { "holdover", clockReport (a3clock::Health::holdover, "no measurement: the last frequency is held"), "HOLDOVER", a3app::palette::danger },
        { "fault", clockReport (a3clock::Health::fault, "the PTP clock refused a frequency write"), "FAULT", a3app::palette::danger },
        { "not-running", absent, "NOT RUNNING", a3app::palette::danger },
    };
    for (const auto& c : cases)
    {
        FakeView view;
        view.clockReport = c.report;
        a3app::StatusPage page (view);
        page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
        page.refresh();
        const auto& clock = page.clockPanel();
        EXPECT_TRUE (clock.stateLabel().getText().startsWith (c.word)) << c.name << ": " << clock.stateLabel().getText();
        EXPECT_EQ (clock.warns(), ! c.warning.isTransparent()) << c.name;
        EXPECT_TRUE (page.getLocalBounds().contains (clock.getBounds())) << c.name;
        EXPECT_GT (clock.getHeight(), 0) << c.name;
        const auto image = a3apptest::snapshotAt (page, juce::String ("status-clock-") + c.name, page.getWidth(), page.getHeight());
        expectPainted (image);
        if (c.warning.isTransparent())
        {
            EXPECT_EQ (pixelsOf (image, a3app::palette::danger), 0) << c.name;
            EXPECT_EQ (pixelsOf (image, a3app::palette::caution), 0) << c.name;
        }
        else
            EXPECT_GT (pixelsOf (image, c.warning), 200) << c.name;   // the frame, not only text
    }
}

// S4: the clock keeps its rows whatever else the page shows: the device choosers, a landscape window.
TEST_F (Pages, TheClockKeepsItsRowsInEveryLayout)
{
    struct Layout
    {
        const char* name;
        bool device;
        int width, height;
    };
    for (const auto& layout : { Layout { "portrait-jack", false, 768, 1024 }, Layout { "portrait-device", true, 768, 1024 },
                                Layout { "landscape-device", true, 1280, 600 }, Layout { "small-device", true, 900, 480 } })
    {
        FakeView view;
        view.clockReport = clockReport (a3clock::Health::holdover, "no measurement: the last frequency is held");
        if (layout.device)
            view.runDevice();
        a3app::AudioPage page (view, manager);
        page.setSize (layout.width, layout.height);
        page.refresh();
        const auto& clock = page.clockPanel();
        EXPECT_GE (clock.getHeight(), layout.height / 12) << layout.name << ": the master and the state (the detail is on STATUS)";
        EXPECT_GT (clock.masterBox().getHeight(), 0) << layout.name;
        EXPECT_TRUE (page.getLocalBounds().contains (clock.getBounds())) << layout.name;
        expectPainted (a3apptest::snapshotAt (page, juce::String ("audio-clock-") + layout.name, layout.width, layout.height));
    }
}

// S4: the window's footer carries the clock's one word on every page, coloured as the panel's frame.
TEST_F (Pages, TheFooterShowsTheClockOnEveryPage)
{
    FakeView view;
    view.clockReport = clockReport (a3clock::Health::locked, "");
    a3app::SaveBar bar (view);
    bar.setSize (900, 60);
    bar.refresh();
    EXPECT_TRUE (bar.clockLabel().getText().contains ("LOCKED")) << bar.clockLabel().getText();
    EXPECT_EQ (bar.clockLabel().findColour (juce::Label::textColourId), a3app::palette::running);
    view.clockReport.status.portState.clear();   // ptp4l's port state not read yet: still synced, still LOCKED
    bar.refresh();
    EXPECT_TRUE (bar.clockLabel().getText().contains ("LOCKED")) << bar.clockLabel().getText();
    EXPECT_EQ (bar.clockLabel().findColour (juce::Label::textColourId), a3app::palette::running);
    view.clockReport.status.health = a3clock::Health::holdover;
    bar.refresh();
    EXPECT_TRUE (bar.clockLabel().getText().contains ("HOLDOVER"));
    EXPECT_EQ (bar.clockLabel().findColour (juce::Label::textColourId), a3app::palette::danger);
    EXPECT_GT (bar.clockLabel().getWidth(), 0);
    // 2026-10-30: right-aligned, directly left of Save.
    std::vector<juce::Component*> save;
    for (auto* child : bar.getChildren())
        if (auto* key = dynamic_cast<juce::TextButton*> (child); key != nullptr && key->getButtonText() == "Save")
            save.push_back (key);
    ASSERT_EQ (save.size(), 1u);
    EXPECT_TRUE (bar.clockLabel().getJustificationType().testFlags (juce::Justification::right));
    EXPECT_LE (bar.clockLabel().getRight(), save.front()->getX());
    EXPECT_GE (bar.clockLabel().getRight(), save.front()->getX() - bar.getHeight() / 2) << "beside Save, not across the bar";
    EXPECT_GT (bar.clockLabel().getX(), bar.getWidth() / 3);
    for (const auto* line : { &bar.pendingLabel(), &bar.messageLabel() })
        EXPECT_LE (line->getRight(), bar.clockLabel().getX());
}

// The PTP role is its own line under the state: neutral while not read yet, a warning only when
// known and not MASTER.
TEST_F (Pages, ThePtpRoleIsASecondLine)
{
    FakeView view;
    view.clockReport = clockReport (a3clock::Health::locked, "");
    a3app::StatusPage page (view);   // the clock's detail is on STATUS since 2026-10-30
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    page.refresh();
    const auto& clock = page.clockPanel();
    EXPECT_EQ (clock.roleLabel().getText(), "PTP grandmaster: confirmed");
    EXPECT_EQ (clock.roleLabel().findColour (juce::Label::textColourId), a3app::palette::textMuted);
    EXPECT_GT (clock.roleLabel().getHeight(), 0);
    view.clockReport.status.portState.clear();
    view.clockReport.status.portProblem = "no PTP state from a3-ptp-state: /run/a3/ptp-state.json missing";
    page.refresh();
    EXPECT_TRUE (clock.roleLabel().getText().startsWith ("PTP role: not read yet")) << clock.roleLabel().getText();
    EXPECT_EQ (clock.roleLabel().findColour (juce::Label::textColourId), a3app::palette::textMuted);
    EXPECT_TRUE (clock.stateLabel().getText().startsWith ("LOCKED")) << clock.stateLabel().getText();
    EXPECT_FALSE (clock.stateLabel().getText().containsIgnoreCase ("unverified"));
    view.clockReport.status.health = a3clock::Health::holdover;
    view.clockReport.status.portState = "SLAVE";
    page.refresh();
    EXPECT_EQ (clock.roleLabel().getText(), "PTP role: SLAVE, not the grandmaster");
    EXPECT_EQ (clock.roleLabel().findColour (juce::Label::textColourId), a3app::palette::danger);
}

TEST_F (Pages, TheClockMasterHasOneChoiceAndSaysWhy)
{
    FakeView view;
    view.clockReport = clockReport (a3clock::Health::locked, "");
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    const auto& clock = page.clockPanel();
    ASSERT_EQ (clock.masterBox().getNumItems(), 1);
    EXPECT_EQ (clock.masterBox().getText(), juce::String (juce::CharPointer_UTF8 ("Scarlett \xe2\x86\x92 PTP grandmaster")));
    EXPECT_TRUE (clock.noteLabel().getText().isEmpty()) << "no explanation (2026-10-31): " << clock.noteLabel().getText();
}

TEST_F (Pages, TheClockSaysItsNumbers)
{
    FakeView view;
    view.clockReport = clockReport (a3clock::Health::locked, "");
    a3app::StatusPage page (view);   // the clock's detail is on STATUS since 2026-10-30
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    page.refresh();
    const auto& clock = page.clockPanel();
    EXPECT_TRUE (clock.stateLabel().getText().contains ("+2.3")) << clock.stateLabel().getText();       // the phase error, us
    EXPECT_TRUE (clock.stateLabel().getText().contains ("1 h 2 min")) << clock.stateLabel().getText();  // locked for
    EXPECT_TRUE (clock.rateLabel().getText().contains ("+14.10 ppm")) << clock.rateLabel().getText();
    EXPECT_TRUE (clock.rateLabel().getText().contains ("+0.012 ppm")) << clock.rateLabel().getText();
    EXPECT_TRUE (clock.countersLabel().getText().contains ("corrections 3990")) << clock.countersLabel().getText();
    EXPECT_TRUE (clock.countersLabel().getText().contains ("Scarlett breaks 1")) << clock.countersLabel().getText();
    view.clockReport.status.health = a3clock::Health::holdover;   // the window's timer: refresh follows
    page.refresh();
    EXPECT_TRUE (clock.stateLabel().getText().startsWith ("HOLDOVER"));
}

// "The dropdown is the truth" (2026-10-30): PipeWire's rate is a dropdown showing the graph's rate, its one choice
// (the engine does not change PipeWire's rate); where it comes from is on STATUS.
TEST_F (Pages, PipeWireRateIsADropdownShowingWhatRuns)
{
    FakeView view;
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    page.refresh();
    EXPECT_TRUE (page.rateChooser().isVisible());
    EXPECT_EQ (page.rateChooser().getText(), "44100 Hz");
    EXPECT_EQ (page.rateChooser().getNumItems(), 1);
    EXPECT_NE (a3app::driverInfo (view).rateSource.indexOf ("JACK server"), -1);
    EXPECT_NE (a3app::driverInfo (view).timing.indexOf ("128"), -1);
    EXPECT_FALSE (page.showsDeviceSelector());
}

// 2026-10-30: one device-type list; PipeWire (JACK) is its first entry, beside JUCE's types. A config of
// "type": "jack" shows as PipeWire (JACK); choosing a JUCE type is the device backend.
TEST_F (Pages, PipeWireIsADeviceTypeBesideJucesTypes)
{
    FakeView view;   // engine.json "type": "jack"
    a3app::AudioPage page (view, manager);
    auto& types = page.typeChooser();
    ASSERT_EQ (types.getNumItems(), 2);
    EXPECT_EQ (types.getItemText (0), "PipeWire (JACK)");
    EXPECT_EQ (types.getItemText (1), "none");   // the tests' JUCE type
    EXPECT_EQ (types.getText(), "PipeWire (JACK)");

    page.chooseDeviceType ("none");
    EXPECT_EQ (view.edited, 1);
    EXPECT_EQ (view.applied, 0);   // an edit, not a rebuild: Save applies
    EXPECT_EQ (view.cfg.backend.type, a3app::BackendSettings::Type::device);
    EXPECT_EQ (view.cfg.backend.deviceType, "none");
    EXPECT_TRUE (page.showsDeviceSelector());
    EXPECT_FALSE (page.showsPeriods()) << "JUCE's ALSA device sets its own periods";
    EXPECT_TRUE (page.blockChooser().isVisible());
    page.chooseDeviceType ("none");   // no change, no second edit
    EXPECT_EQ (view.edited, 1);

    page.chooseDeviceType (a3app::AudioPage::pipewireType);
    EXPECT_EQ (view.cfg.backend.type, a3app::BackendSettings::Type::jack);
    EXPECT_FALSE (page.showsDeviceSelector());
}

// The block and periods on PipeWire: the choices, and nothing applied before APPLY. A new block runs live; new
// periods say first that the audio stops for about two seconds.
TEST_F (Pages, PipeWireBlockAndPeriodsApplyOnlyOnApply)
{
    FakeView view;
    view.state.timing.available = true;
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    page.refresh();
    auto& block = page.blockChooser();
    auto& periods = page.periodsChooser();
    ASSERT_EQ (block.getNumItems(), 6);
    for (const auto& [index, text] : { std::pair { 0, "32 frames" }, { 2, "128 frames" }, { 5, "1024 frames" } })
        EXPECT_EQ (block.getItemText (index), text);
    ASSERT_EQ (periods.getNumItems(), 2);
    EXPECT_EQ (periods.getItemText (0), "2 periods");
    EXPECT_EQ (periods.getItemText (1), "3 periods");
    EXPECT_EQ (block.getText(), "128 frames");
    EXPECT_EQ (periods.getText(), "3 periods");
    EXPECT_TRUE (page.showsPeriods());
    EXPECT_FALSE (page.applyButton().isVisible());

    page.chooseBlock (64);
    EXPECT_TRUE (view.timings.empty()) << "a choice is not applied on its own";
    EXPECT_TRUE (page.applyButton().isVisible());
    EXPECT_EQ (page.confirmLabel().getText(), "Block 64 frames. APPLY?") << "no explanation (2026-10-31)";
    page.cancelButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_TRUE (view.timings.empty());
    EXPECT_EQ (block.getText(), "128 frames");
    EXPECT_FALSE (page.applyButton().isVisible());

    page.choosePeriods (2);
    EXPECT_EQ (page.confirmLabel().getText(), "Periods 2: audio stops ~2 s. APPLY?") << "the warning, short";
    EXPECT_TRUE (page.getLocalBounds().contains (page.applyButton().getBounds()));
    expectPainted (a3apptest::snapshotAt (page, "audio-periods-confirm", page.getWidth(), page.getHeight()));
    page.applyButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    ASSERT_EQ (view.timings.size(), 1u);
    EXPECT_EQ (view.timings[0], (a3app::AudioTiming { 128, 2 }));
    EXPECT_FALSE (page.applyButton().isVisible());
}

// After a change the page says what runs (block, periods, the measured round trip, DSP load and xruns), and REVERT
// goes back to the previous choice: a block at once, periods only after the same question.
TEST_F (Pages, PipeWireSaysWhatRunsAndReverts)
{
    FakeView view;
    view.state.blockSize = 64;
    view.state.dspLoadPercent = 41.0f;
    view.state.xruns = 3;
    view.state.timing.available = true;
    view.state.timing.applied = { 64, 3 };
    view.state.timing.previous = a3app::AudioTiming { 128, 3 };
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    page.refresh();
    const auto driver = a3app::driverInfo (view);   // what runs: on STATUS since 2026-10-30
    EXPECT_EQ (driver.timing, juce::String (juce::CharPointer_UTF8 ("Block 64 frames \xc3\x97 3 periods")));
    EXPECT_TRUE (driver.load.contains ("41")) << driver.load;
    EXPECT_EQ (driver.xruns, "3");
    EXPECT_EQ (page.blockChooser().getText(), "64 frames");
    ASSERT_TRUE (page.revertButton().isEnabled());
    page.revertButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    ASSERT_EQ (view.timings.size(), 1u);
    EXPECT_EQ (view.timings[0], (a3app::AudioTiming { 128, 3 }));

    view.state.timing.applied = { 128, 2 };
    view.state.timing.previous = a3app::AudioTiming { 128, 3 };
    page.refresh();
    page.revertButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (view.timings.size(), 1u) << "periods: asked first";
    EXPECT_TRUE (page.confirmLabel().getText().contains ("~2 s")) << page.confirmLabel().getText();
    page.applyButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    ASSERT_EQ (view.timings.size(), 2u);
    EXPECT_EQ (view.timings[1], (a3app::AudioTiming { 128, 3 }));

    view.state.timing.previous.reset();
    view.state.timing.restarting = true;
    page.refresh();
    EXPECT_FALSE (page.revertButton().isEnabled());
    EXPECT_FALSE (page.blockChooser().isEnabled()) << "nothing new while WirePlumber restarts";
}

// The sound-device settings come first, everything the page reports below them, the clock last.
TEST_F (Pages, TheSettingsComeBeforeTheInfo)
{
    for (const bool device : { false, true })
    {
        FakeView view;
        view.state.timing.available = ! device;
        if (device)
            view.runDevice();
        a3app::AudioPage page (view, manager);
        page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
        page.refresh();
        const int settingsBottom = page.blockChooser().getBottom();
        EXPECT_GT (page.typeChooser().getBottom(), 0);
        EXPECT_LE (page.typeChooser().getBottom(), page.blockChooser().getY());
        for (const auto* info : { &page.roundTripLabel(), &page.analogChainLabel() })
        {
            EXPECT_TRUE (info->isVisible()) << device;
            EXPECT_GE (info->getY(), settingsBottom) << info->getText() << (device ? " (device)" : " (PipeWire)");
        }
        EXPECT_LE (page.analogChainLabel().getBottom(), page.clockPanel().getY());
    }
}

// MEASURE and MEASURE ANALOG CHAIN stay on the page; without their pair set on the I/O page they are off, and their
// own line says why (2026-10-30: no hint line under them).
TEST_F (Pages, MeasureButtonsSayWhichPairIsMissing)
{
    FakeView view;
    view.cfg.io.roundTripLoop = view.runningCfg.io.roundTripLoop = a3app::RoundTripLoop {};
    view.state.roundTripMeasured.state = a3app::RoundTripReport::State::measured;
    view.state.roundTripMeasured.samples = 343;
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    page.refresh();
    EXPECT_TRUE (page.measureButton().isEnabled());
    EXPECT_FALSE (page.analogButton().isEnabled());
    EXPECT_TRUE (page.analogChainLabel().getText().contains ("analog test pair")) << page.analogChainLabel().getText();
    EXPECT_TRUE (page.analogChainLabel().getText().contains ("I/O page")) << page.analogChainLabel().getText();

    view.cfg.io.roundTripLoop.reset();
    view.runningCfg.io.roundTripLoop.reset();
    view.state.roundTripMeasured = {};
    view.state.roundTripMeasured.state = a3app::RoundTripReport::State::noLoop;
    page.refresh();
    EXPECT_FALSE (page.measureButton().isEnabled());
    EXPECT_TRUE (page.roundTripLabel().getText().contains ("no digital loop")) << page.roundTripLabel().getText();
    for (auto* child : page.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->isVisible())
            EXPECT_FALSE (label->getText().startsWith ("set the")) << "no hint line: " << label->getText();
    expectPainted (a3apptest::snapshotAt (page, "audio-no-loop", page.getWidth(), page.getHeight()));
}

TEST_F (Pages, StatusPagePaintsEveryState)
{
    FakeView view;
    a3app::StatusPage page (view);
    auto paint = [&] (const char* name) { page.refresh(); expectPainted (snapshot (page, name)); };

    paint ("status-open");
    view.open = false;
    paint ("status-closed");
    view.heard = false;
    paint ("status-core-never");
    view.heard = true; view.sinceCore = 12.5;
    paint ("status-core-silent");
    view.state.missingPeers = { "system:capture_1", "StemDeck-A:out_1" };
    view.state.xruns = 3; view.state.dspLoadPercent = 71.5f;
    paint ("status-missing-peer");
    for (int i = 1; i <= 41; ++i)
        view.state.missingPeers.push_back ("AES67 deck B:out_" + juce::String (i));
    view.dropped = { "deckA_cue_L -> x:y", "deckA_cue_R -> x:y", "deckB_aux_L -> p:q", "deckB_aux_R -> p:q", "deckB_cue_L -> r:s" };
    view.dropped.push_back ("deckC_cue_L -> r:s");
    view.note = "the truth has no engine.deck-audio: deck audio reports are off";
    paint ("status-grouped-and-dropped");
    view.state.connecting = false;
    view.runDevice();
    paint ("status-no-connect");
    view.meterValues = { { NAN, INFINITY }, { -1.0f, 5.0f }, { 0.5f, 0.2f } };
    view.names = { "a", "b", "c" };
    paint ("status-nonfinite-meters");
}

TEST_F (Pages, StatusTextSaysWhatIsGoingOn)
{
    FakeView view;
    EXPECT_EQ (a3app::modeText (view.runningCfg, view.state), "JACK, connecting");
    view.state.connecting = false;
    EXPECT_EQ (a3app::modeText (view.runningCfg, view.state), "JACK, no links (--no-connect)");
    view.runDevice();
    EXPECT_EQ (a3app::modeText (view.runningCfg, view.state), "Device, no links (--no-connect)");
    EXPECT_EQ (a3app::coreText (false, 0), "Core not heard yet");
    EXPECT_EQ (a3app::coreText (true, 0.4), "Core heard, last message 0.4 s ago");
    EXPECT_EQ (a3app::coreText (true, 12.5), "Core silent for 12.5 s");
}

TEST_F (Pages, ServiceWindowHidesOnCloseOtherwiseQuits)
{
    FakeView view;
    int quits = 0;
    {
        a3app::MainWindow window (view, manager, {}, true, [&] { ++quits; });
        window.setVisible (true);   // shown on the desktop only by open(); the flag is what the test reads
        EXPECT_TRUE (window.isVisible());
        window.closeButtonPressed();
        EXPECT_EQ (quits, 0);
        EXPECT_FALSE (window.isVisible());
    }
    a3app::MainWindow window (view, manager, {}, false, [&] { ++quits; });
    window.closeButtonPressed();
    EXPECT_EQ (quits, 1);
    expectPainted (snapshot (*window.getContentComponent(), "window"));
}

// open() must give the window a native peer: the constructor is peer-less on purpose (headless
// tests), and setUsingNativeTitleBar alone does not put a window on the desktop.
TEST_F (Pages, OpenPutsTheWindowOnTheDesktop)
{
    const char* display = std::getenv ("DISPLAY");
    if (display == nullptr || *display == '\0')
        GTEST_SKIP() << "needs an X display (e.g. DISPLAY=:2)";

    FakeView view;
    a3app::MainWindow window (view, manager, {}, true, [] {});
    EXPECT_FALSE (window.isOnDesktop());
    window.open();
    EXPECT_TRUE (window.isOnDesktop());
    EXPECT_NE (window.getPeer(), nullptr);
}

// An edit is not the engine: choosing a JUCE device type on a running JACK engine changes the AUDIO page's
// layout and says "not saved", but STATUS and the read-only values keep describing what runs.
TEST_F (Pages, PendingEditDoesNotChangeWhatRuns)
{
    FakeView view;   // running JACK
    a3app::StatusPage status (view);
    a3app::AudioPage audio (view, manager);
    EXPECT_TRUE (audio.pendingLabel().getText().isEmpty());

    audio.chooseDeviceType ("none");
    status.refresh();
    audio.refresh();

    EXPECT_EQ (view.runningCfg.backend.type, a3app::BackendSettings::Type::jack);
    EXPECT_EQ (status.modeLine(), "JACK, connecting");
    EXPECT_TRUE (audio.showsDeviceSelector());   // the edit is what the page edits
    EXPECT_NE (audio.pendingLabel().getText().indexOf ("not saved"), -1);
    EXPECT_EQ (a3app::driverInfo (view).bitDepth.indexOf ("set by the device"), -1);   // 32 is JACK's value
    EXPECT_NE (a3app::driverInfo (view).bitDepth.indexOf ("JACK"), -1);

    audio.chooseDeviceType (a3app::AudioPage::pipewireType);   // back to what runs: nothing pending
    EXPECT_TRUE (audio.pendingLabel().getText().isEmpty());
}

namespace
{
// Every page and the save bar, painted against `view` (the fake itself or the remote view of it).
void paintEveryPage (a3app::HostView& view, const juce::String& suffix)
{
    a3test::ScratchDirectory copy;
    {
        a3app::ChannelsPage page (view);
        expectPainted (snapshot (page, "channels" + suffix));
    }
    {
        a3app::ZonesPage page (view, { a3test::shippedSpatialDirectory, a3test::reaperBoothSpatialDirectory, copy.root() });
        expectPainted (snapshot (page, "zones" + suffix));
    }
    if (auto* fake = dynamic_cast<FakeView*> (&view))   // the headphone EQ state is set on the fake's fields
    {
        fake->cfg.engine.phonesHeadphoneEq = "AKG-K141MK2";
        a3app::ZonesPage page (view, { a3test::shippedSpatialDirectory });
        expectPainted (snapshot (page, "zones-eq" + suffix));
        fake->cfg.engine.phonesHeadphoneEq = "off";
    }
    {
        a3app::IoPage page (view);
        expectPainted (snapshot (page, "io-jack" + suffix));
        auto device = view.config();
        device.backend.type = a3app::BackendSettings::Type::device;
        device.io.deviceChannels["analog_1"] = 0;
        view.edit (device);
        page.refresh (true);
        expectPainted (snapshot (page, "io-device" + suffix));
        device.backend.type = a3app::BackendSettings::Type::jack;
        view.edit (device);
    }
    {
        a3app::CalibrationPage page (view);
        expectPainted (snapshot (page, "calibration" + suffix));
    }
    {
        a3app::StatusPage page (view);
        expectPainted (snapshot (page, "status" + suffix));
    }
    {
        auto pending = view.config();
        pending.engine.channels = 5;   // a pending edit: the bar says so
        view.edit (pending);
        a3app::SaveBar bar (view);
        bar.refresh();
        expectPainted (snapshot (bar, "savebar" + suffix));
    }
}

void prepareEveryPage (FakeView& view)
{
    view.cfg.engine.trims.booth.reset();   // a zone that has no trim yet
    view.runningCfg = view.cfg;
    view.links = { { "system:capture_1", "a3-engine:analog_1" }, { "stranger:out", "a3-engine:analog_1" } };
}
}

TEST_F (Pages, EveryPagePaints)
{
    FakeView view;
    prepareEveryPage (view);
    paintEveryPage (view, "");
}

TEST_F (Pages, EveryPagePaintsRemote)
{
    RemoteRig rig;
    prepareEveryPage (rig.fake);
    rig.tick();
    rig.tick();
    rig.remote->refreshNow();
    paintEveryPage (*rig.remote, "-remote");
    EXPECT_TRUE (rig.remote->engineProblem().isEmpty());
}

TEST_F (Pages, ChannelsEditTheConfigAndShowTheLoad)
{
    FakeView view;
    view.state.dspLoadPercent = 12.5f;
    a3app::ChannelsPage page (view);
    page.refresh();
    page.setChannels (6);
    EXPECT_EQ (view.edited, 1);
    EXPECT_EQ (view.cfg.engine.channels, 6);
    EXPECT_EQ (view.applied, 0);
    page.setChannels (0);   // refused with a reason, nothing edited
    EXPECT_EQ (view.edited, 1);
    EXPECT_TRUE (page.messageLabel().getText().isNotEmpty());
}

// A17: the deck count sits beside N, edits the same way and shows what runs.
TEST_F (Pages, DecksEditTheConfigBesideTheChannels)
{
    FakeView view;   // two decks, named
    a3app::ChannelsPage page (view);
    page.refresh();
    EXPECT_NE (page.decksRunningLabel().getText().indexOf ("running: 2"), -1);
    page.setDecks (4);
    EXPECT_EQ (view.edited, 1);
    EXPECT_EQ (view.cfg.engine.decks, 4);
    EXPECT_TRUE (view.cfg.engine.decksNamed);
    EXPECT_EQ (a3app::deckSourceOf (view.cfg.io, 4), a3app::DeckSource::local);
    EXPECT_EQ (view.applied, 0);
    page.refresh();
    EXPECT_NE (page.decksRunningLabel().getText().indexOf ("running: 2"), -1);   // until Save
    page.setDecks (4);   // the same count: no new edit
    EXPECT_EQ (view.edited, 1);
    page.setDecks (27);  // beyond Z: refused with a reason, nothing edited
    EXPECT_EQ (view.edited, 1);
    EXPECT_NE (page.messageLabel().getText().indexOf ("26"), -1);

    ASSERT_TRUE (a3app::saveEdits (view).wasOk());
    EXPECT_EQ (view.applied, 1);
    juce::String error;
    EXPECT_EQ (a3app::loadAppConfig (view.file, error, a3apptest::truthDecks())->engine.decks, 4);
    page.refresh();
    EXPECT_NE (page.decksRunningLabel().getText().indexOf ("running: 4"), -1);
}

// MAIN and BOOTH each pick a variant from their own dropdown; one that does not load is refused with the reason.
TEST_F (Pages, ZonesPickAVariantPerZoneAndShowAFailure)
{
    FakeView view;
    a3test::ScratchDirectory copy;
    a3app::ZonesPage page (view, { a3test::shippedSpatialDirectory, copy.root(), juce::File ("/nonexistent/spatial") });
    EXPECT_EQ (page.rowCount(), 3);
    EXPECT_EQ (page.zoneChooser (a3::SpeakerZone::main).getSelectedItemIndex(), 0);
    EXPECT_EQ (page.zoneChooser (a3::SpeakerZone::booth).getSelectedItemIndex(), 0);
    page.chooseZone (a3::SpeakerZone::booth, 1);
    EXPECT_EQ (a3app::zoneSpatialDirectory (view.cfg, a3::SpeakerZone::booth), copy.root());
    EXPECT_EQ (a3app::zoneSpatialDirectory (view.cfg, a3::SpeakerZone::main), a3test::shippedSpatialDirectory);   // its own
    EXPECT_EQ (view.edited, 1);
    EXPECT_EQ (page.zoneChooser (a3::SpeakerZone::booth).getSelectedItemIndex(), 1);
    page.chooseZone (a3::SpeakerZone::main, 2);   // does not load: stays on the last good one and says why
    EXPECT_EQ (view.cfg.spatialDirectory, a3test::shippedSpatialDirectory);
    EXPECT_NE (page.messageLabel().getText().indexOf ("/nonexistent/spatial"), -1);
    EXPECT_EQ (page.zoneChooser (a3::SpeakerZone::main).getSelectedItemIndex(), 0);
}

TEST_F (Pages, ZonesPaintsTheHeadphonesChooser)
{
    FakeView view;
    a3app::ZonesPage page (view, { a3test::shippedSpatialDirectory });
    expectPainted (snapshot (page, "zones-headphones"));
    const auto& chooser = page.headphoneChooser();
    EXPECT_TRUE (chooser.isVisible());
    EXPECT_GT (chooser.getHeight(), 0);
    ASSERT_EQ (chooser.getNumItems(), 24);   // off + 23 models
    EXPECT_EQ (chooser.getItemText (0), "off");
    EXPECT_EQ (chooser.getItemText (1), "AKG-K141MK2");
    EXPECT_EQ (chooser.getSelectedItemIndex(), 0);

    page.chooseHeadphoneEq (1);
    EXPECT_EQ (view.cfg.engine.phonesHeadphoneEq, "AKG-K141MK2");
    EXPECT_EQ (view.edited, 1);
    EXPECT_EQ (page.headphoneChooser().getSelectedItemIndex(), 1);
    page.chooseHeadphoneEq (0);
    EXPECT_EQ (view.cfg.engine.phonesHeadphoneEq, "off");
}

// I/O INPUT (2026-10-30, the maintainer): no direct links section. The page has no text field and no word of them;
// a file's io.directLinks is not the page's to show or edit, so it is kept as loaded (the engine still makes them).
TEST_F (Pages, IoInputHasNoDirectLinks)
{
    FakeView view;
    view.cfg.io.directLinks = { { "y:out", "z:in" } };
    a3app::IoPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    page.showSub (a3app::IoPage::Sub::input);
    std::function<void (juce::Component&)> look = [&] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            EXPECT_EQ (dynamic_cast<juce::TextEditor*> (child), nullptr) << "no free-text field";
            if (auto* label = dynamic_cast<juce::Label*> (child))
                EXPECT_FALSE (label->getText().containsIgnoreCase ("direct link")) << label->getText();
            look (*child);
        }
    };
    look (page);
    page.refresh (true);
    EXPECT_EQ (view.edited, 0);
    EXPECT_EQ (view.cfg.io.directLinks, (std::vector<std::pair<std::string, std::string>> { { "y:out", "z:in" } }));
    expectPainted (a3apptest::snapshotAt (page, "io-input-without-direct-links", page.getWidth(), page.getHeight()));
}

TEST_F (Pages, CalibrationNeedsConfirmation)
{
    FakeView view;
    view.cfg.engine.trims.main = -3.0f;
    juce::String asked;
    std::function<void()> yes;
    a3app::CalibrationPage page (view, [&] (const juce::String& message, std::function<void()> onYes) { asked = message; yes = std::move (onYes); });

    page.proposeTrim (a3app::TrimZone::main, -6.5f);
    EXPECT_NE (asked.indexOf ("-3.0"), -1);   // old
    EXPECT_NE (asked.indexOf ("-6.5"), -1);   // new
    EXPECT_EQ (view.edited, 0);                // no confirmation yet: nothing changed
    EXPECT_EQ (view.cfg.engine.trims.main, std::optional<float> (-3.0f));
    yes = nullptr;                             // the dialog is dismissed without a yes
    EXPECT_EQ (view.edited, 0);

    page.proposeTrim (a3app::TrimZone::main, -6.5f);
    ASSERT_TRUE (yes);
    yes();
    EXPECT_EQ (view.cfg.engine.trims.main, std::optional<float> (-6.5f));
    EXPECT_EQ (view.edited, 1);

    asked = {};
    view.cfg.engine.trims.booth.reset();
    page.proposeTrim (a3app::TrimZone::booth, -20.0f);
    EXPECT_NE (asked.indexOf ("not set"), -1);   // a zone without a trim is named as closed

    asked = {};
    page.proposeTrim (a3app::TrimZone::main, 12.0f);   // beyond +6 dB: never offered
    page.proposeTrim (a3app::TrimZone::main, -95.0f);
    EXPECT_TRUE (asked.isEmpty());
    EXPECT_TRUE (page.messageLabel().getText().isNotEmpty());
    EXPECT_EQ (page.sliderRangeMin(), -90.0);
    EXPECT_EQ (page.sliderRangeMax(), 6.0);
}

TEST_F (Pages, CalibrationNeedsConfirmationRemote)
{
    RemoteRig rig;
    rig.fake.cfg.engine.trims.main = -3.0f;
    rig.fake.runningCfg = rig.fake.cfg;
    rig.tick();
    rig.remote->refreshNow();
    juce::String asked;
    std::function<void()> yes;
    a3app::CalibrationPage page (*rig.remote, [&] (const juce::String& message, std::function<void()> onYes) { asked = message; yes = std::move (onYes); });

    page.proposeTrim (a3app::TrimZone::main, -6.5f);
    EXPECT_NE (asked.indexOf ("-3.0"), -1);
    EXPECT_NE (asked.indexOf ("-6.5"), -1);
    rig.sync();
    EXPECT_EQ (rig.fake.edited, 0);   // nothing is posted before the yes
    EXPECT_EQ (rig.fake.cfg.engine.trims.main, std::optional<float> (-3.0f));

    page.proposeTrim (a3app::TrimZone::main, -6.5f);
    ASSERT_TRUE (yes);
    yes();
    rig.sync();
    EXPECT_EQ (rig.fake.edited, 1);   // the engine got the confirmed trim, pending
    EXPECT_EQ (rig.fake.cfg.engine.trims.main, std::optional<float> (-6.5f));
    EXPECT_EQ (rig.fake.runningCfg.engine.trims.main, std::optional<float> (-3.0f));
    EXPECT_EQ (rig.remote->config().engine.trims.main, std::optional<float> (-6.5f));
}

TEST_F (Pages, FilterKindIsLive)
{
    FakeView view;
    a3app::CalibrationPage calibration (view);
    auto& page = calibration.filterCard();
    EXPECT_EQ (page.shownKind(), a3::FilterKind::svf);
    page.choose (a3::FilterKind::ladder);
    EXPECT_EQ (view.filterSets, 1);
    EXPECT_EQ (view.cfg.engine.filterKind, a3::FilterKind::ladder);
    EXPECT_EQ (view.applied, 0);               // no Save, no rebuild
    EXPECT_FALSE (view.file.existsAsFile());   // nothing written
    page.refresh();
    EXPECT_EQ (page.shownKind(), a3::FilterKind::ladder);
}

TEST_F (Pages, SaveWritesThenApplies)
{
    FakeView view;
    juce::String error;
    view.cfg = *a3app::withChannels (view.cfg, 6, error);
    ASSERT_TRUE (view.cfg.engine.channels == 6);

    const auto result = a3app::saveEdits (view);
    ASSERT_TRUE (result.wasOk()) << result.getErrorMessage();
    EXPECT_EQ (view.applied, 1);
    EXPECT_TRUE (view.fileExistedAtApply);   // the file first, then the rebuild
    const auto loaded = a3app::loadAppConfig (view.file, error, a3apptest::truthDecks());
    ASSERT_TRUE (loaded) << error;
    EXPECT_EQ (loaded->engine.channels, 6);
    EXPECT_TRUE (a3app::sameConfig (view.cfg, view.runningCfg));
}

// The rig, 2026-10-31: the digital loop picked on the I/O page and saved was written but never applied, so the running
// engine kept "no loop configured" and MEASURE stayed off. A Save that only changes the loop applies it.
TEST_F (Pages, SaveAppliesALoopPickedAlone)
{
    FakeView view;
    view.runningCfg.io.roundTripLoop.reset();
    view.cfg = view.runningCfg;
    view.cfg.io.roundTripLoop = a3app::RoundTripLoop { 11, 11 };
    const auto result = a3app::saveEdits (view);
    ASSERT_TRUE (result.wasOk()) << result.getErrorMessage();
    EXPECT_EQ (view.applied, 1) << "written and applied";
    ASSERT_TRUE (view.runningCfg.io.roundTripLoop.has_value());
    EXPECT_EQ (view.runningCfg.io.roundTripLoop->output, 11);
}

TEST_F (Pages, SaveWritesThenAppliesRemote)
{
    RemoteRig rig;
    rig.tick();
    rig.remote->refreshNow();
    juce::String error;
    const auto six = a3app::withChannels (rig.remote->config(), 6, error);
    ASSERT_TRUE (six) << error;
    rig.remote->edit (*six);

    const auto result = rig.settle ([&] { return a3app::saveEdits (*rig.remote); });
    ASSERT_TRUE (result.wasOk()) << result.getErrorMessage();
    EXPECT_EQ (rig.fake.applied, 1);
    EXPECT_TRUE (rig.fake.fileExistedAtApply);   // the file first, then the rebuild
    const auto loaded = a3app::loadAppConfig (rig.fake.file, error, a3apptest::truthDecks());
    ASSERT_TRUE (loaded) << error;
    EXPECT_EQ (loaded->engine.channels, 6);
    EXPECT_TRUE (a3app::sameConfig (rig.remote->config(), rig.remote->running()));
    EXPECT_EQ (rig.fake.runningCfg.engine.channels, 6);

    rig.fake.applyResult = juce::Result::fail ("cannot open the device");
    const auto seven = a3app::withChannels (rig.remote->config(), 7, error);
    rig.remote->edit (*seven);
    const auto refused = rig.settle ([&] { return a3app::saveEdits (*rig.remote); });
    EXPECT_TRUE (refused.failed());
    EXPECT_NE (refused.getErrorMessage().indexOf ("cannot open the device"), -1);
    EXPECT_EQ (a3app::loadAppConfig (rig.fake.file, error, a3apptest::truthDecks())->engine.channels, 6);   // the previous file is back
}

// The deck count crosses to the engine the way N does: the window's edit, posted, saved, applied.
TEST_F (Pages, DecksSaveThenApplyRemote)
{
    RemoteRig rig;
    rig.tick();
    rig.remote->refreshNow();
    a3app::ChannelsPage page (*rig.remote);
    page.setDecks (4);
    ASSERT_EQ (rig.remote->config().engine.decks, 4);

    const auto result = rig.settle ([&] { return a3app::saveEdits (*rig.remote); });
    ASSERT_TRUE (result.wasOk()) << result.getErrorMessage();
    EXPECT_EQ (rig.fake.applied, 1);
    EXPECT_EQ (rig.fake.runningCfg.engine.decks, 4);
    EXPECT_EQ (a3app::deckSourceOf (rig.fake.runningCfg.io, 4), a3app::DeckSource::local);
    juce::String error;
    EXPECT_EQ (a3app::loadAppConfig (rig.fake.file, error, a3apptest::truthDecks())->engine.decks, 4);
}

TEST_F (Pages, SaveThatTheLoaderWouldRefuseWritesAndAppliesNothing)
{
    FakeView view;
    view.cfg.engine.trims.main = 40.0f;   // beyond +6 dB: not reachable by a page, but a config can say it
    const auto result = a3app::saveEdits (view);
    EXPECT_TRUE (result.failed());
    EXPECT_FALSE (view.file.existsAsFile());
    EXPECT_EQ (view.applied, 0);
}

TEST_F (Pages, SaveWithoutAnythingToApplyOnlyWrites)
{
    FakeView view;   // the edit equals what runs (a live filter change, say)
    ASSERT_TRUE (a3app::saveEdits (view).wasOk());
    EXPECT_TRUE (view.file.existsAsFile());
    EXPECT_EQ (view.applied, 0);
}

TEST_F (Pages, SaveBarShowsPendingAndAFailedApply)
{
    FakeView view;
    a3app::SaveBar bar (view);
    bar.refresh();
    EXPECT_TRUE (bar.pendingLabel().getText().isEmpty());
    juce::String error;
    view.cfg = *a3app::withChannels (view.cfg, 5, error);
    bar.refresh();
    EXPECT_NE (bar.pendingLabel().getText().indexOf ("not saved"), -1);

    view.applyResult = juce::Result::fail ("cannot open the device");
    bar.save();
    EXPECT_NE (bar.messageLabel().getText().indexOf ("cannot open the device"), -1);
}

TEST_F (Pages, AFailedApplyPutsThePreviousFileBack)
{
    FakeView view;
    view.file.replaceWithText ("{\"engine\":{\"channels\":3}}");   // what was on disk, whatever it said
    const auto before = view.file.loadFileAsString();
    juce::String error;
    view.cfg = *a3app::withChannels (view.cfg, 6, error);
    view.applyResult = juce::Result::fail ("cannot open the device");

    const auto result = a3app::saveEdits (view);
    ASSERT_TRUE (result.failed());
    EXPECT_NE (result.getErrorMessage().indexOf ("not applied"), -1);
    EXPECT_NE (result.getErrorMessage().indexOf ("cannot open the device"), -1);
    EXPECT_NE (result.getErrorMessage().indexOf ("previous config kept"), -1);
    EXPECT_EQ (view.file.loadFileAsString(), before);   // the next boot reads what it read before
    EXPECT_EQ (view.cfg.engine.channels, 6);            // the edit stays pending
    EXPECT_TRUE (view.fileExistedAtApply);

    view.file.deleteFile();   // no file before: none after
    EXPECT_TRUE (a3app::saveEdits (view).failed());
    EXPECT_FALSE (view.file.existsAsFile());

    a3app::SaveBar bar (view);
    bar.save();
    EXPECT_NE (bar.messageLabel().getText().indexOf ("not applied"), -1);
    EXPECT_NE (bar.pendingLabel().getText().indexOf ("not saved"), -1);
}

TEST_F (Pages, CalibrationSlidersStartAtTheCurrentTrim)
{
    FakeView view;
    view.cfg.engine.trims.main = -3.0f;
    view.cfg.engine.trims.booth.reset();
    a3app::CalibrationPage page (view);
    EXPECT_FLOAT_EQ ((float) page.proposalValue (a3app::TrimZone::main), -3.0f);
    EXPECT_FLOAT_EQ ((float) page.proposalValue (a3app::TrimZone::phones), 0.0f);
    EXPECT_EQ (page.proposalValue (a3app::TrimZone::booth), -90.0);   // no trim: the lowest, never above what the zone has
}

TEST_F (Pages, StatusSaysWhenNoEngineRuns)
{
    FakeView view;
    a3app::StatusPage page (view);
    page.refresh();
    EXPECT_EQ (page.modeLine().indexOf ("NO ENGINE"), -1);
    view.problem = "device gone; the previous setting could not be reopened";
    page.refresh();
    EXPECT_TRUE (page.modeLine().startsWith ("A3-CORE NOT RUNNING - ")) << page.modeLine();
    EXPECT_NE (page.modeLine().indexOf ("device gone"), -1);
    expectPainted (snapshot (page, "status-no-engine"));
}

TEST_F (Pages, PaintsTheDeckSourceChooser)
{
    FakeView view;
    a3app::setDeckSource (view.cfg.io, 1, a3app::DeckSource::local);
    a3app::setDeckSource (view.cfg.io, 2, a3app::DeckSource::aes67);
    a3app::IoPage page (view);
    page.setSize (900, 600);
    expectPainted (snapshot (page, "io-deck-source"));
    ASSERT_NE (page.deckSourceBox (2), nullptr);
    EXPECT_EQ (page.deckSourceBox (2)->getSelectedId(), 2);   // AES67
    EXPECT_EQ (page.deckSourceBox (1)->getSelectedId(), 1);   // local
    EXPECT_GT (page.deckSourceBox (2)->getHeight(), 0);

    page.deckSourceBox (2)->setSelectedId (1, juce::sendNotificationSync);
    EXPECT_EQ (view.cfg.io.jack.at ("deckB_cue_9"), std::vector<std::string> { "StemDeck-B:cue_9" });
    EXPECT_EQ (view.cfg.io.jack.at ("deckB_1"), std::vector<std::string> { "StemDeck-B:bformat_1" });
}

// A8: a chooser per slot of the count, C, D and beyond as A and B.
TEST_F (Pages, EverySlotOfTheCountHasASourceChooser)
{
    FakeView view;
    view.cfg.engine = a3test::engineConfig (4, 5);
    a3app::IoPage page (view);
    page.setSize (900, 600);
    for (int slot = 1; slot <= 5; ++slot)
    {
        ASSERT_NE (page.deckSourceBox (slot), nullptr) << slot;
        EXPECT_EQ (page.deckSourceBox (slot)->getSelectedId(), 3) << slot;   // nothing linked yet
    }
    EXPECT_EQ (page.deckSourceBox (6), nullptr);

    page.deckSourceBox (3)->setSelectedId (1, juce::sendNotificationSync);
    EXPECT_EQ (view.cfg.io.jack.at ("deckC_cue_9"), std::vector<std::string> { "StemDeck-C:cue_9" });
    page.deckSourceBox (4)->setSelectedId (2, juce::sendNotificationSync);
    EXPECT_EQ (view.cfg.io.jack.at ("deckD_aux_16"), std::vector<std::string> { "AES67 deck D:receive_AUX16" });
    page.deckSourceBox (5)->setSelectedId (1, juce::sendNotificationSync);
    EXPECT_EQ (view.cfg.io.jack.at ("deckE_1"), std::vector<std::string> { "StemDeck-E:bformat_1" });
    EXPECT_EQ (a3app::deckSourceOf (view.cfg.io, 5), a3app::DeckSource::local);
}

TEST_F (Pages, AnIncompleteDeckSlotShowsItAndOneChoiceCompletesIt)
{
    FakeView view;
    for (int i = 1; i <= 16; ++i)   // what an older saved config holds: the 16 main links only
        view.cfg.io.jack["deckA_" + std::to_string (i)] = { "StemDeck-A:bformat_" + std::to_string (i) };
    a3app::IoPage page (view);
    page.setSize (900, 600);
    expectPainted (snapshot (page, "io-deck-incomplete"));
    EXPECT_EQ (page.deckSourceBox (1)->getSelectedId(), 3);

    page.deckSourceBox (1)->setSelectedId (1, juce::sendNotificationSync);
    EXPECT_EQ (a3app::deckSourceOf (view.cfg.io, 1), a3app::DeckSource::local);
    EXPECT_EQ (view.cfg.io.jack.at ("deckA_aux_16"), std::vector<std::string> { "StemDeck-A:aux_16" });
    EXPECT_EQ (view.cfg.io.jack.at ("deckA_cue_9"), std::vector<std::string> { "StemDeck-A:cue_9" });
    EXPECT_EQ (page.deckSourceBox (1)->getSelectedId(), 1);
}

namespace
{
std::vector<juce::TextButton*> buttonsLabelled (juce::Component& parent, const juce::String& text)
{
    std::vector<juce::TextButton*> found;
    for (auto* child : parent.getChildren())
    {
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == text)
            found.push_back (button);
        for (auto* inner : buttonsLabelled (*child, text))
            found.push_back (inner);
    }
    return found;
}

// A scratch "installed" spatial directory the fake runs from, and an empty user root.
struct SpeakerRig
{
    FakeView view;
    a3test::ScratchDirectory installed;
    juce::File userRoot = juce::File::createTempFile ("a3-user-spatial");
    SpeakerRig()
    {
        view.cfg.spatialDirectory = installed.root();
        view.runningCfg = view.cfg;
    }
    ~SpeakerRig() { userRoot.deleteRecursively(); }
    // The user's variants under the root (the hidden staging area aside).
    std::vector<juce::File> variants() const
    {
        std::vector<juce::File> found;
        for (const auto& child : userRoot.findChildFiles (juce::File::findDirectories, false))
            if (! child.getFileName().startsWithChar ('.'))
                found.push_back (child);
        return found;
    }
};
}

// The info viewer shows the variant picked in the list, and only that one.
TEST_F (Pages, ZonesSummariseTheVariantPickedInTheList)
{
    FakeView view;
    a3test::ScratchDirectory broken;
    broken.root().getChildFile ("booth.decoder.json").replaceWithText ("{");
    a3app::ZonesPage page (view, { a3test::shippedSpatialDirectory, broken.root() });
    page.selectVariant (0);
    EXPECT_EQ (page.selectedVariant(), a3test::shippedSpatialDirectory);
    for (const auto zone : { a3::SpeakerZone::main, a3::SpeakerZone::booth })
    {
        const auto text = page.summaryText (zone);
        for (const auto* part : { "AllRADecoder v0.9.5", "28 Feb 2026", "inPhase", "ch 1", "150 Hz", "imaginary" })
            EXPECT_TRUE (text.contains (part)) << a3::zoneName (zone) << ": " << part << " in " << text;
    }
    EXPECT_TRUE (page.summaryText (a3::SpeakerZone::main).startsWith ("MAIN"));
    EXPECT_TRUE (page.summaryText (a3::SpeakerZone::booth).startsWith ("BOOTH"));
    expectPainted (snapshot (page, "zones-summary"));

    page.selectVariant (1);   // picking in the list edits nothing
    EXPECT_EQ (view.edited, 0);
    EXPECT_TRUE (page.summaryText (a3::SpeakerZone::booth).contains ("booth.decoder.json")) << page.summaryText (a3::SpeakerZone::booth);
    EXPECT_TRUE (page.summaryText (a3::SpeakerZone::main).contains ("Order  3"));
    expectPainted (snapshot (page, "zones-summary-unreadable"));

    bool opened = false;
    page.onOpenSpeakers = [&] { opened = true; };
    const auto keys = buttonsLabelled (page, "EDIT SPEAKERS");
    ASSERT_EQ (keys.size(), 1u);
    keys.front()->onClick();
    EXPECT_TRUE (opened);
}

TEST_F (Pages, SpeakersPageHostsIemsEditorPerZone)
{
    SpeakerRig rig;
    a3app::SpeakersPage page (rig.view, rig.userRoot);
    EXPECT_EQ (page.zone(), a3::SpeakerZone::main);
    ASSERT_NE (page.editor(), nullptr);
    EXPECT_EQ (buttonsLabelled (page, "Noise").size(), 6u);
    expectPainted (snapshot (page, "speakers-main"));
    page.showZone (a3::SpeakerZone::booth);
    EXPECT_EQ (page.zone(), a3::SpeakerZone::booth);
    EXPECT_TRUE (page.messageLabel().getText().isEmpty()) << page.messageLabel().getText();
    expectPainted (snapshot (page, "speakers-booth"));
}

TEST_F (Pages, SpeakersSayWhenAZoneDoesNotLoad)
{
    SpeakerRig rig;
    rig.installed.root().getChildFile ("main.decoder.json").replaceWithText ("{");
    a3app::SpeakersPage page (rig.view, rig.userRoot);
    EXPECT_TRUE (page.messageLabel().getText().contains ("main.decoder.json")) << page.messageLabel().getText();
    expectPainted (snapshot (page, "speakers-unloadable"));
}

TEST_F (Pages, SpeakersNoiseGoesToTheShownZone)
{
    SpeakerRig rig;
    a3app::SpeakersPage page (rig.view, rig.userRoot);
    auto noise = buttonsLabelled (page, "Noise");
    ASSERT_FALSE (noise.empty());
    noise.front()->onClick();
    page.showZone (a3::SpeakerZone::booth);
    noise = buttonsLabelled (page, "Noise");
    ASSERT_FALSE (noise.empty());
    noise.front()->onClick();
    ASSERT_EQ (rig.view.noises.size(), 2u);
    EXPECT_EQ (rig.view.noises[0].zone, a3::SpeakerZone::main);
    EXPECT_EQ (rig.view.noises[1].zone, a3::SpeakerZone::booth);
    EXPECT_FALSE (rig.view.noises[1].ambisonic);
    EXPECT_GE (rig.view.noises[1].channel, 2);
}

TEST_F (Pages, SpeakersSaveToZoneWritesAUserVariant)
{
    SpeakerRig rig;
    const auto installedBooth = rig.installed.root().getChildFile ("booth.decoder.json").loadFileAsString();
    a3app::SpeakersPage page (rig.view, rig.userRoot);
    page.showZone (a3::SpeakerZone::booth);
    page.setCrossover (120.0f);
    page.saveToZone();
    EXPECT_EQ (rig.view.edited, 1);
    ASSERT_EQ (rig.variants().size(), 1u);
    const auto variant = rig.variants().front();
    EXPECT_EQ (a3app::zoneSpatialDirectory (rig.view.cfg, a3::SpeakerZone::booth), variant);   // for the booth only
    EXPECT_EQ (a3app::zoneSpatialDirectory (rig.view.cfg, a3::SpeakerZone::main), rig.installed.root());
    EXPECT_FALSE (a3app::sameConfig (rig.view.cfg, rig.view.runningCfg));   // Save restarts on it
    const auto parameters = juce::JSON::parse (variant.getChildFile ("booth.parameters.json").loadFileAsString());
    EXPECT_EQ ((double) parameters["lowPassFrequency"], 120.0);
    EXPECT_EQ ((double) parameters["highPassFrequency"], 120.0);
    EXPECT_EQ ((double) parameters["weights"], 2.0);   // inPhase, as the file says
    const auto decoder = juce::JSON::parse (variant.getChildFile ("booth.decoder.json").loadFileAsString());
    EXPECT_TRUE (decoder["Description"].toString().contains ("AllRADecoder")) << decoder["Description"].toString();
    EXPECT_EQ (decoder["LoudspeakerLayout"]["Loudspeakers"].size(), 6);
    EXPECT_EQ (rig.installed.root().getChildFile ("booth.decoder.json").loadFileAsString(), installedBooth);
    EXPECT_TRUE (page.messageLabel().getText().startsWith ("BOOTH saved: ")) << page.messageLabel().getText();
    expectPainted (snapshot (page, "speakers-saved"));
}

TEST_F (Pages, SpeakersSaveRefusesWhatIemRefuses)
{
    SpeakerRig rig;
    a3app::SpeakersPage page (rig.view, rig.userRoot);
    auto clear = buttonsLabelled (page, "CLEAR");
    ASSERT_EQ (clear.size(), 1u);
    auto* listener = dynamic_cast<juce::Button::Listener*> (page.editor());
    ASSERT_NE (listener, nullptr);
    listener->buttonClicked (clear.front());   // IEM's CLEAR: no loudspeakers left
    page.saveToZone();
    EXPECT_EQ (rig.view.edited, 0);
    EXPECT_TRUE (rig.variants().empty());
    EXPECT_TRUE (page.messageLabel().getText().contains ("no loudspeakers")) << page.messageLabel().getText();
    expectPainted (snapshot (page, "speakers-refused"));
}

TEST_F (Pages, SpeakersSubChannelIsCheckedLikeTheEngine)
{
    SpeakerRig rig;
    a3app::SpeakersPage page (rig.view, rig.userRoot);
    page.setSubChannel (3);   // a top's channel
    page.saveToZone();
    EXPECT_EQ (rig.view.edited, 0);
    EXPECT_TRUE (page.messageLabel().getText().contains ("sub")) << page.messageLabel().getText();
    page.setSubChannel (0);   // no sub
    page.saveToZone();
    EXPECT_EQ (rig.view.edited, 1);
    const auto parameters = juce::JSON::parse (rig.view.cfg.spatialDirectory.getChildFile ("main.parameters.json").loadFileAsString());
    EXPECT_EQ ((double) parameters["swMode"], 0.0);
}

TEST_F (Pages, ZonesOpenTheWindowsSpeakersPage)
{
    SpeakerRig rig;
    a3app::MainWindow window (rig.view, manager, { rig.installed.root() }, false, [] {}, {}, {}, rig.userRoot);
    EXPECT_TRUE (window.pageList().contains ("SPEAKERS"));
    window.showPage ("ZONES");
    const auto keys = buttonsLabelled (*window.getContentComponent(), "EDIT SPEAKERS");
    ASSERT_EQ (keys.size(), 1u);
    keys.front()->onClick();
    EXPECT_EQ (window.currentPage(), "SPEAKERS");
    window.refreshNow();
    expectPainted (snapshot (*window.getContentComponent(), "window-speakers"));
}

// Saving twice from the page: two variants, the page follows the newer, the older stays.
TEST_F (Pages, SpeakersSaveTwiceKeepsBothVariants)
{
    SpeakerRig rig;
    a3app::SpeakersPage page (rig.view, rig.userRoot);
    page.showZone (a3::SpeakerZone::main);
    page.saveToZone();
    const auto first = rig.view.cfg.spatialDirectory;
    page.refresh();
    page.showZone (a3::SpeakerZone::booth);
    page.setCrossover (100.0f);
    page.saveToZone();
    EXPECT_EQ (rig.view.edited, 2);
    EXPECT_EQ (rig.view.cfg.spatialDirectory, first);   // MAIN keeps the first
    EXPECT_NE (a3app::zoneSpatialDirectory (rig.view.cfg, a3::SpeakerZone::booth), first);
    EXPECT_EQ (rig.variants().size(), 2u);
    EXPECT_TRUE (first.getChildFile ("booth.parameters.json").existsAsFile());
}

// M3, PA safety: noise must never be playable from the network through IEM's OSC receiver.
TEST_F (Pages, SpeakersNeverListenForOsc)
{
    SpeakerRig rig;
    a3app::SpeakersPage page (rig.view, rig.userRoot);
    for (const auto zone : { a3::SpeakerZone::main, a3::SpeakerZone::booth })
    {
        page.showZone (zone);
        EXPECT_FALSE (page.listensForOsc());
        ASSERT_NE (page.editor(), nullptr);
        EXPECT_FALSE (a3::AllRadDesigner::showsOscControls (*page.editor()));
    }
}

namespace
{
// Every component under `parent` (not `parent` itself), depth first.
void collectDescendants (juce::Component& parent, std::vector<juce::Component*>& found)
{
    for (auto* child : parent.getChildren())
    {
        found.push_back (child);
        collectDescendants (*child, found);
    }
}

std::vector<juce::Component*> descendants (juce::Component& parent)
{
    std::vector<juce::Component*> found;
    collectDescendants (parent, found);
    return found;
}

template <class Wanted>
Wanted* firstDescendant (juce::Component& parent)
{
    for (auto* child : descendants (parent))
        if (auto* wanted = dynamic_cast<Wanted*> (child))
            return wanted;
    return nullptr;
}

juce::Component* toggleLabelled (juce::Component& parent, const juce::String& text)
{
    for (auto* child : descendants (parent))
        if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child); toggle != nullptr && toggle->getButtonText() == text)
            return toggle;
    return nullptr;
}

juce::Label* labelSaying (juce::Component& parent, const juce::String& text)
{
    for (auto* child : descendants (parent))
        if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->getText() == text)
            return label;
    return nullptr;
}

// `child`'s area in `ancestor`'s coordinates (transforms included).
juce::Rectangle<int> areaIn (juce::Component& ancestor, juce::Component& child)
{
    return ancestor.getLocalArea (&child, child.getLocalBounds());
}

// The one visible child of the window's content that fills the page band: the shown page.
juce::Component* shownPage (a3app::MainWindow& window)
{
    auto& content = *window.getContentComponent();
    juce::Component* shown = nullptr;
    for (auto* child : content.getChildren())
        if (child->isVisible() && child->getBounds() == window.bands().page)
        {
            EXPECT_EQ (shown, nullptr) << "two pages fill the page band";
            shown = child;
        }
    return shown;
}
}

// The rig's portrait screen: the page keys in rows across the top, the page at full width below them,
// KEYS, MONITOR, Tooltips and Save in one footer at the bottom; nothing of one band in another.
TEST_F (Pages, PortraitWindowHasKeysOnTopPageBelowAndAFooter)
{
    SpeakerRig rig;
    a3app::MainWindow window (rig.view, manager, { rig.installed.root() }, false, [] {}, {}, {}, rig.userRoot);
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    auto& content = *window.getContentComponent();
    const auto bands = window.bands();
    const auto whole = content.getLocalBounds();

    EXPECT_EQ (bands.navigation.getY(), 0);   // the keys sit at the ceiling
    EXPECT_EQ (bands.footer.getBottom(), whole.getBottom());
    EXPECT_EQ (bands.page.getWidth(), whole.getWidth());   // the page has the whole width
    for (const auto& band : { bands.navigation, bands.page, bands.footer })
        EXPECT_TRUE (whole.contains (band)) << band.toString();
    EXPECT_FALSE (bands.navigation.intersects (bands.page));
    EXPECT_FALSE (bands.page.intersects (bands.footer));
    EXPECT_FALSE (bands.navigation.intersects (bands.footer));
    EXPECT_LT (bands.navigation.getHeight(), whole.getHeight() / 6);
    EXPECT_LT (bands.footer.getHeight(), whole.getHeight() / 10);

    std::vector<juce::Rectangle<int>> keyAreas;
    float keyText = -1.0f;
    for (const auto& name : window.pageList())
    {
        if (name == "MONITOR")
            continue;
        juce::TextButton* key = nullptr;
        for (auto* candidate : buttonsLabelled (content, name))
            if (candidate->getParentComponent() == &content)
                key = candidate;   // the window's own key, not a page's button of the same name
        ASSERT_NE (key, nullptr) << name;
        const auto area = areaIn (content, *key);
        EXPECT_TRUE (key->isVisible()) << name;
        EXPECT_TRUE (bands.navigation.contains (area)) << name << " " << area.toString();
        EXPECT_GE (key->getHeight(), 28) << name;   // a fingertip
        for (const auto& other : keyAreas)
            EXPECT_FALSE (other.intersects (area)) << name;
        keyAreas.push_back (area);
        const auto text = look.getTextButtonFont (*key, key->getHeight()).getHeight();
        if (keyText < 0)
            keyText = text;
        EXPECT_FLOAT_EQ (text, keyText) << name << ": one text size across the keys";
        EXPECT_LE (juce::GlyphArrangement::getStringWidth (juce::FontOptions (text), name), (float) key->getWidth()) << name;
    }

    const auto saves = buttonsLabelled (content, "Save");
    ASSERT_EQ (saves.size(), 1u);
    EXPECT_TRUE (bands.footer.contains (areaIn (content, *saves.front())));
    EXPECT_GT (areaIn (content, *saves.front()).getCentreX(), whole.getCentreX());   // Save on the right
    EXPECT_EQ (toggleLabelled (content, "Tooltips"), nullptr) << "no tooltips since 2026-10-30";

    for (const auto& name : window.pageList())
    {
        if (name == "MONITOR")
            continue;
        window.showPage (name);
        auto* page = shownPage (window);
        ASSERT_NE (page, nullptr) << name;
        expectPainted (a3apptest::snapshotAt (content, "portrait-" + name.toLowerCase().removeCharacters ("&/ "),
                                              content.getWidth(), content.getHeight()));
    }
}

// SPEAKERS: IEM's editor is scaled into the page (no scrolling), all of it inside the page, so its OpenGL
// view (a native window JUCE does not clip) cannot paint over the keys or the footer; its 3D view is
// narrower than the table.
TEST_F (Pages, SpeakersEditorIsScaledIntoThePage)
{
    SpeakerRig rig;
    a3app::MainWindow window (rig.view, manager, { rig.installed.root() }, false, [] {}, {}, {}, rig.userRoot);
    for (const auto& size : { juce::Point<int> { a3apptest::portraitWidth, a3apptest::portraitHeight }, juce::Point<int> { 1000, 680 } })
    {
        window.setSize (size.x, size.y);
        window.showPage ("SPEAKERS");
        auto* page = firstDescendant<a3app::SpeakersPage> (*shownPage (window));
        ASSERT_NE (page, nullptr);
        auto* editor = page->editor();
        ASSERT_NE (editor, nullptr);
        EXPECT_EQ (editor->getParentComponent()->getParentComponent(), page) << "on its card, no viewport: nothing to scroll";
        EXPECT_EQ (firstDescendant<juce::Viewport> (*page), firstDescendant<juce::Viewport> (*editor)) << "the page has no viewport of its own";
        const auto pageArea = page->getLocalBounds();
        const auto editorArea = areaIn (*page, *editor);
        EXPECT_TRUE (pageArea.contains (editorArea)) << size.toString() << ": " << editorArea.toString() << " in " << pageArea.toString();
        for (auto* part : descendants (*editor))
            if (part->isVisible())
                EXPECT_TRUE (editorArea.expanded (1).contains (areaIn (*page, *part)))
                    << size.toString() << ": " << part->getName() << " " << areaIn (*page, *part).toString();

        auto* viewer = firstDescendant<juce::OpenGLRenderer> (*editor);
        auto* table = firstDescendant<juce::TableListBox> (*editor);
        ASSERT_NE (viewer, nullptr);
        ASSERT_NE (table, nullptr);
        auto& viewerComponent = dynamic_cast<juce::Component&> (*viewer);
        EXPECT_TRUE (editorArea.contains (areaIn (*page, viewerComponent))) << size.toString();
        EXPECT_LT (areaIn (*page, viewerComponent).getWidth(), areaIn (*page, *table).getWidth()) << size.toString();
    }
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    window.showPage ("SPEAKERS");
    auto* page = firstDescendant<a3app::SpeakersPage> (*shownPage (window));
    ASSERT_NE (page, nullptr);
    // Portrait: the editor nearly fills the width (since the sub-page keys of 2026-10-30 its height limits it a little).
    EXPECT_GE (areaIn (*page, *page->editor()).getWidth(), page->getWidth() * 5 / 6);
    // 2026-10-31: the energy map was cut at its right edge -- its image cache, made at the unscaled size and drawn through
    // the page's transform. Nothing of the hosted editor is cached to an image.
    for (auto* part : descendants (*page->editor()))
        EXPECT_EQ (part->getCachedComponentImage(), nullptr) << typeid (*part).name();
    expectPainted (a3apptest::snapshotAt (*window.getContentComponent(), "system3-speakers", window.getContentComponent()->getWidth(),
                                          window.getContentComponent()->getHeight()));
}

// No tooltips (2026-10-30): no Tooltips switch, and the window.settings key "tooltips" from before is ignored and kept.
TEST_F (Pages, TheTooltipsKeyIsIgnored)
{
    FakeView view;
    const auto home = juce::File::createTempFile ("a3-window-home");
    const auto settings = a3app::windowSettingsFile (home);
    settings.getParentDirectory().createDirectory();
    settings.replaceWithText ("{\"tooltips\": true}\n");
    {
        a3app::MainWindow window (view, manager, {}, false, [] {}, { nullptr, settings });
        window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
        EXPECT_EQ (toggleLabelled (*window.getContentComponent(), "Tooltips"), nullptr);
        for (auto* child : descendants (*window.getContentComponent()))
            EXPECT_EQ (dynamic_cast<juce::TooltipWindow*> (child), nullptr);
    }
    a3app::writeMonitorSwitch (settings, true);
    EXPECT_TRUE (a3app::readMonitorSwitch (settings));
    EXPECT_TRUE (settings.loadFileAsString().contains ("tooltips")) << "another key of the file is kept";
    home.deleteRecursively();
}

// FILTER is a card on CALIBRATION (2026-10-31, a page until then): its heading and the two keys, no explanation.
TEST_F (Pages, FilterIsACardOnCalibration)
{
    FakeView view;
    a3app::CalibrationPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    const auto svf = buttonsLabelled (page, "SVF");
    const auto ladder = buttonsLabelled (page, "LADDER");
    ASSERT_EQ (svf.size(), 1u);
    ASSERT_EQ (ladder.size(), 1u);
    EXPECT_NE (labelSaying (page, "FILTER"), nullptr);
    ASSERT_EQ (page.cardAreas().size(), 2u) << "the trims and FILTER, no help card";
    EXPECT_TRUE (page.cardAreas()[1].contains (areaIn (page, *svf.front())));
    for (auto* child : descendants (page))
        if (auto* label = dynamic_cast<juce::Label*> (child))
            EXPECT_LT (label->getText().length(), 60) << "no explanation: " << label->getText();
    expectPainted (a3apptest::snapshotAt (page, "system3-calibration-filter", page.getWidth(), page.getHeight()));
}

namespace
{
// The ZONES page over a user root holding "spare" and "club"; MAIN runs from "club". The dialogs answer at once.
struct ZonesRig
{
    SpeakerRig speakers;
    juce::File spare = speakers.userRoot.getChildFile ("spare"), club = speakers.userRoot.getChildFile ("club");
    std::vector<juce::String> asked;
    juce::String answer = "spare-renamed";
    bool agree = true;
    a3app::ZonesPage::Dialogs dialogs {
        [this] (const juce::String& question, std::function<void()> onYes) { asked.push_back (question); if (agree) onYes(); },
        [this] (const juce::String& current, std::function<void (const juce::String&)> onName) { asked.push_back (current); onName (answer); } };
    ZonesRig()
    {
        for (const auto& variant : { spare, club })
            speakers.installed.root().copyDirectoryTo (variant);
        speakers.view.cfg.spatialDirectory = club;
        speakers.view.runningCfg = speakers.view.cfg;
    }
    int rowOf (a3app::ZonesPage& page, const juce::File& directory) const
    {
        for (int row = 0; row < page.rowCount(); ++row)
            if (page.variantAt (row) == directory)
                return row;
        return -1;
    }
};
}

// The list holds the installed choices and the user's variants; RENAME and DELETE touch only an unused one of
// the user's, DELETE after asking.
TEST_F (Pages, ZonesRenameAndDeleteOnlyAnUnusedUserVariant)
{
    ZonesRig rig;
    a3app::ZonesPage page (rig.speakers.view, { rig.speakers.installed.root() }, rig.speakers.userRoot, rig.dialogs);
    ASSERT_GE (rig.rowOf (page, rig.spare), 0);
    ASSERT_GE (rig.rowOf (page, rig.club), 0);

    page.selectVariant (rig.rowOf (page, rig.club));
    EXPECT_TRUE (page.deleteRefusal().contains ("runs")) << page.deleteRefusal();
    page.askDelete();
    EXPECT_TRUE (rig.asked.empty());   // refused before anything is asked
    EXPECT_TRUE (rig.club.isDirectory());
    page.selectVariant (rig.rowOf (page, rig.speakers.installed.root()));
    EXPECT_FALSE (page.deleteRefusal().isEmpty());
    EXPECT_FALSE (page.renameRefusal().isEmpty());

    page.selectVariant (rig.rowOf (page, rig.spare));
    EXPECT_TRUE (page.renameRefusal().isEmpty());
    page.askRename();
    const auto renamed = rig.speakers.userRoot.getChildFile ("spare-renamed");
    EXPECT_TRUE (renamed.isDirectory());
    EXPECT_FALSE (rig.spare.exists());
    EXPECT_EQ (page.selectedVariant(), renamed);   // the list follows

    rig.agree = false;
    page.askDelete();
    EXPECT_TRUE (renamed.isDirectory());   // not without a yes
    rig.agree = true;
    page.askDelete();
    EXPECT_FALSE (renamed.exists());
    EXPECT_EQ (rig.rowOf (page, renamed), -1);
    expectPainted (a3apptest::snapshotAt (page, "zones-after-delete", a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4));
}

// End to end on the file system as the program builds the page (2026-10-31): the choices from spatialChoices (configDir),
// the user's variants under <configDir>/engine/spatial. A rename leaves no stale old name in the list, a delete no row.
TEST_F (Pages, ZonesRenameAndDeleteOnTheFileSystemAsTheProgramDoes)
{
    ZonesRig rig;
    const auto configDir = juce::File::createTempFile ("a3-config");
    const auto userRoot = configDir.getChildFile ("engine/spatial");
    userRoot.createDirectory();
    const auto spare = userRoot.getChildFile ("spare");
    rig.speakers.installed.root().copyDirectoryTo (spare);
    a3app::ZonesPage page (rig.speakers.view, a3app::spatialChoices (configDir), userRoot, rig.dialogs);
    ASSERT_GE (rig.rowOf (page, spare), 0);

    page.selectVariant (rig.rowOf (page, spare));
    page.askRename();
    const auto renamed = userRoot.getChildFile ("spare-renamed");
    EXPECT_TRUE (renamed.isDirectory());
    EXPECT_FALSE (spare.exists());
    EXPECT_GE (rig.rowOf (page, renamed), 0);
    EXPECT_EQ (rig.rowOf (page, spare), -1) << "the old name is gone from the list";
    EXPECT_EQ (page.selectedVariant(), renamed);

    page.askDelete();
    page.refresh();
    EXPECT_FALSE (renamed.exists());
    EXPECT_EQ (rig.rowOf (page, renamed), -1) << "the list follows a delete";
    configDir.deleteRecursively();
}

// LOAD (2026-10-31): the zones' choice written and applied now; another pending edit stays pending.
TEST_F (Pages, ZonesLoadAppliesTheZonesNow)
{
    ZonesRig rig;
    auto& view = rig.speakers.view;
    a3app::ZonesPage page (view, { rig.speakers.installed.root() }, rig.speakers.userRoot, rig.dialogs);
    juce::String error;
    view.cfg = *a3app::withChannels (view.cfg, 6, error);
    page.chooseZone (a3::SpeakerZone::booth, rig.rowOf (page, rig.spare));
    ASSERT_EQ (a3app::zoneSpatialDirectory (view.config(), a3::SpeakerZone::booth), rig.spare);
    const int channelsRunning = view.running().engine.channels;
    page.loadNow();
    EXPECT_EQ (view.applied, 1);
    EXPECT_EQ (a3app::zoneSpatialDirectory (view.running(), a3::SpeakerZone::booth), rig.spare) << "runs now";
    EXPECT_EQ (view.running().engine.channels, channelsRunning) << "only the zones were applied";
    EXPECT_EQ (view.config().engine.channels, 6) << "the other edit stays pending";
    EXPECT_TRUE (page.messageLabel().getText().isEmpty()) << page.messageLabel().getText();
}

// A rename the page refuses (a bad name) changes nothing and says why; renaming what the pending edit uses follows it.
TEST_F (Pages, ZonesRenameFollowsThePendingEditAndRefusesABadName)
{
    ZonesRig rig;
    rig.speakers.view.cfg.boothSpatialDirectory = rig.spare;   // pending: BOOTH on spare
    a3app::ZonesPage page (rig.speakers.view, { rig.speakers.installed.root() }, rig.speakers.userRoot, rig.dialogs);
    page.selectVariant (rig.rowOf (page, rig.spare));
    EXPECT_TRUE (page.deleteRefusal().contains ("pending")) << page.deleteRefusal();
    rig.answer = "../escape";
    page.askRename();
    EXPECT_TRUE (rig.spare.isDirectory());
    EXPECT_TRUE (page.messageLabel().getText().contains ("slash")) << page.messageLabel().getText();
    rig.answer = "dome";
    page.askRename();
    EXPECT_EQ (a3app::zoneSpatialDirectory (rig.speakers.view.cfg, a3::SpeakerZone::booth), rig.speakers.userRoot.getChildFile ("dome"));
    EXPECT_EQ (rig.speakers.view.cfg.spatialDirectory, rig.club);
}

// Portrait: MAIN, BOOTH and PHONES dropdowns first, then the list and the info viewer; RENAME and DELETE in
// the footer beside Save, on ZONES only.
TEST_F (Pages, ZonesDropdownsFirstAndRenameDeleteInTheFooter)
{
    ZonesRig rig;
    a3app::MainWindow window (rig.speakers.view, manager, { rig.speakers.installed.root() }, false, [] {}, {}, {}, rig.speakers.userRoot);
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    auto& content = *window.getContentComponent();
    window.showPage ("ZONES");
    auto* page = firstDescendant<a3app::ZonesPage> (*shownPage (window));
    ASSERT_NE (page, nullptr);
    const auto& mainBox = page->zoneChooser (a3::SpeakerZone::main);
    const auto& boothBox = page->zoneChooser (a3::SpeakerZone::booth);
    const auto& phonesBox = page->headphoneChooser();
    EXPECT_LT (mainBox.getY(), boothBox.getY());
    EXPECT_LT (boothBox.getY(), phonesBox.getY());
    auto* list = firstDescendant<juce::ListBox> (*page);
    ASSERT_NE (list, nullptr);
    EXPECT_GE (list->getY(), phonesBox.getBottom());   // the dropdowns come first
    for (const auto& name : { "RENAME", "DELETE" })
    {
        const auto keys = buttonsLabelled (content, name);
        ASSERT_EQ (keys.size(), 1u) << name;
        EXPECT_TRUE (keys.front()->isVisible()) << name;
        EXPECT_TRUE (window.bands().footer.contains (areaIn (content, *keys.front()))) << name;
    }
    page->selectVariant (rig.rowOf (*page, rig.club));
    window.refreshNow();
    auto* deleteKey = buttonsLabelled (content, "DELETE").front();
    EXPECT_TRUE (deleteKey->isEnabled()) << "a key that cannot act still answers (ZonesKeysAlwaysAnswer)";
    expectPainted (a3apptest::snapshotAt (content, "portrait-zones-refused", content.getWidth(), content.getHeight()));
    page->selectVariant (rig.rowOf (*page, rig.spare));
    window.refreshNow();
    EXPECT_TRUE (deleteKey->isEnabled());

    window.showPage ("STATUS");
    EXPECT_FALSE (buttonsLabelled (content, "RENAME").front()->isVisible());
    EXPECT_FALSE (deleteKey->isVisible());
}

namespace
{
// A JACK graph with eight capture and eight playback ports, as the engine's status reports it.
void giveHardwarePorts (FakeView& view)
{
    for (int i = 1; i <= 8; ++i)
    {
        view.state.capturePorts.add ("system:capture_" + juce::String (i));
        view.state.playbackPorts.add ("system:playback_" + juce::String (i));
    }
}

juce::Component* subKey (juce::Component& page, const juce::String& name)
{
    const auto keys = buttonsLabelled (page, name);
    return keys.empty() ? nullptr : keys.front();
}
}

// OUTPUT: every channel of MAIN, BOOTH, PHONES and REC has a dropdown of the playback ports ("none" first); a
// choice is the port's route; a port another channel already uses is marked, not refused. REC is free by default
// (for an analog recorder).
TEST_F (Pages, IoOutputHasADropdownPerZoneChannel)
{
    FakeView view;
    giveHardwarePorts (view);
    view.links = { { "a3-engine:main_top_1", "stranger:in" } };
    a3app::IoPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    ASSERT_NE (subKey (page, "INPUT"), nullptr);
    ASSERT_NE (subKey (page, "OUTPUT"), nullptr);
    page.showSub (a3app::IoPage::Sub::output);
    EXPECT_EQ (page.routeBox ("rec_L")->getSelectedItemIndex(), 0) << "REC: none until the user chooses";
    juce::String error;
    const auto ports = a3app::portNamesFor (view.cfg, error);
    ASSERT_TRUE (ports) << error;
    for (const auto& group : a3app::zoneOutputGroups (*ports))
        for (const auto& port : group.ports)
        {
            auto* box = page.routeBox (port);
            ASSERT_NE (box, nullptr) << port;
            EXPECT_TRUE (box->isShowing() || box->isVisible()) << port;
            EXPECT_EQ (box->getNumItems(), 9) << port;   // none + 8
            EXPECT_TRUE (box->getItemText (0).contains ("none")) << port;
        }
    page.chooseRoute ("booth_top_2", 3);   // system:playback_3
    EXPECT_EQ (view.cfg.io.jack.at ("booth_top_2"), std::vector<std::string> { "system:playback_3" });
    page.chooseRoute ("rec_L", 3);
    EXPECT_EQ (view.cfg.io.jack.at ("rec_L"), std::vector<std::string> { "system:playback_3" });   // twice: allowed
    page.refresh (true);
    EXPECT_TRUE (page.routeBox ("rec_L")->getItemText (3).contains ("booth_top_2")) << page.routeBox ("rec_L")->getItemText (3);
    page.chooseRoute ("rec_L", 0);
    EXPECT_EQ (view.cfg.io.jack.count ("rec_L"), 0u);
    EXPECT_EQ (view.links.size(), 1u);
    expectPainted (a3apptest::snapshotAt (page, "io-output", page.getWidth(), page.getHeight()));
}

// INPUT: per channel L and R dropdowns of the capture ports, the deck sources, the direct links.
TEST_F (Pages, IoInputHasLeftAndRightPerChannelAndTheDecks)
{
    FakeView view;
    giveHardwarePorts (view);
    a3app::IoPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    page.showSub (a3app::IoPage::Sub::input);
    for (const auto& channel : a3app::channelInputPorts (view.cfg.engine))
        for (const auto& port : { channel.left, channel.right })
        {
            ASSERT_TRUE (port.has_value());
            auto* box = page.routeBox (*port);
            ASSERT_NE (box, nullptr) << *port;
            EXPECT_TRUE (box->isVisible()) << *port;
            EXPECT_EQ (box->getNumItems(), 9) << *port;
        }
    page.chooseRoute ("analog_4", 6);
    EXPECT_EQ (view.cfg.io.jack.at ("analog_4"), std::vector<std::string> { "system:capture_6" });
    ASSERT_NE (page.deckSourceBox (1), nullptr);
    EXPECT_TRUE (page.deckSourceBox (1)->isVisible());
    expectPainted (a3apptest::snapshotAt (page, "io-input", page.getWidth(), page.getHeight()));

    page.showSub (a3app::IoPage::Sub::output);
    EXPECT_FALSE (page.deckSourceBox (1)->isShowing());
    EXPECT_FALSE (page.routeBox ("analog_4")->isShowing());

    view.runDevice();   // a device: the device's channels instead of ports
    view.state.deviceInputs = 4;
    view.state.deviceOutputs = 6;
    page.refresh (true);
    page.showSub (a3app::IoPage::Sub::input);
    EXPECT_EQ (page.routeBox ("analog_1")->getNumItems(), 5);   // none + 4
    page.chooseRoute ("analog_1", 2);
    EXPECT_EQ (view.cfg.io.deviceChannels.at ("analog_1"), 1);   // channel 2, 0-based in the map
}

// AUDIO: the measured round trip replaces the estimate; MEASURE asks the engine for a new run.
TEST_F (Pages, AudioShowsTheMeasuredRoundTripAndMeasuresOnRequest)
{
    FakeView view;
    view.cfg.io.roundTripLoop = view.runningCfg.io.roundTripLoop = a3app::RoundTripLoop {};
    view.state.sampleRate = 48000;
    view.state.roundTripMeasured.state = a3app::RoundTripReport::State::measured;
    view.state.roundTripMeasured.samples = 211;
    view.state.roundTripMeasured.atMillis = juce::Time (2026, 9, 30, 14, 5, 0, 0, true).toMilliseconds();
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    page.refresh();
    EXPECT_EQ (page.roundTripLabel().getText(), "4.4 ms   14:05");
    ASSERT_TRUE (page.measureButton().isVisible());
    EXPECT_TRUE (page.measureButton().isEnabled());
    EXPECT_TRUE (page.getLocalBounds().contains (page.measureButton().getBounds()));
    page.measureButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (view.roundTripRequests, 1);
    expectPainted (a3apptest::snapshotAt (page, "audio-roundtrip-measured", page.getWidth(), page.getHeight()));

    view.state.roundTripMeasured = {};
    view.state.roundTripMeasured.state = a3app::RoundTripReport::State::noLoop;
    view.state.roundTripMeasured.reason = "no loop configured";
    view.cfg.io.roundTripLoop = view.runningCfg.io.roundTripLoop = std::nullopt;
    page.refresh();
    EXPECT_EQ (page.roundTripLabel().getText(), "no digital loop (I/O page)");
    EXPECT_FALSE (page.measureButton().isEnabled());
}

// I/O: the loop's channels are offered in no dropdown, the notice names them and what a load unrouted, and the
// editor refuses them.
TEST_F (Pages, IoLeavesTheRoundTripLoopOut)
{
    FakeView view;
    for (int i = 1; i <= 14; ++i)
    {
        view.state.capturePorts.add ("system:capture_" + juce::String (i));
        view.state.playbackPorts.add ("system:playback_" + juce::String (i));
    }
    view.cfg.io.roundTripLoop = a3app::RoundTripLoop { 11, 11 };
    view.unroutedByLoop = { "booth_sub -> system:playback_11" };
    a3app::IoPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    page.showSub (a3app::IoPage::Sub::output);
    auto* box = page.routeBox ("booth_sub");
    ASSERT_NE (box, nullptr);
    EXPECT_EQ (box->getNumItems(), 14);   // none + 14 - the loop's one
    for (int i = 0; i < box->getNumItems(); ++i)
        EXPECT_FALSE (box->getItemText (i).endsWith (" out 11")) << box->getItemText (i);
    page.chooseRoute ("booth_sub", 11);   // past the loop: system:playback_12, free again
    EXPECT_EQ (view.cfg.io.jack.at ("booth_sub"), std::vector<std::string> { "system:playback_12" });
    EXPECT_TRUE (page.noticeLabel().getText().contains ("output 11 ")) << page.noticeLabel().getText();
    EXPECT_FALSE (page.noticeLabel().getText().contains ("11-12")) << page.noticeLabel().getText();
    EXPECT_TRUE (page.noticeLabel().getText().contains ("booth_sub -> system:playback_11")) << page.noticeLabel().getText();
    EXPECT_TRUE (page.noticeLabel().isVisible());

    view.runDevice();
    view.state.deviceInputs = 14;
    view.state.deviceOutputs = 14;
    page.refresh (true);
    page.showSub (a3app::IoPage::Sub::input);
    EXPECT_EQ (page.routeBox ("analog_1")->getNumItems(), 14);
    page.chooseRoute ("analog_1", 11);   // past the loop: channel 12
    EXPECT_EQ (view.cfg.io.deviceChannels.at ("analog_1"), 11);
    expectPainted (a3apptest::snapshotAt (page, "io-roundtrip-loop", page.getWidth(), page.getHeight()));
}

// AUDIO: the analog chain's line and its button, which says what to plug first and runs only on a click.
TEST_F (Pages, AudioMeasuresTheAnalogChainOnlyOnRequest)
{
    FakeView view;
    view.state.sampleRate = 48000;
    view.cfg.io.analogLoop = view.runningCfg.io.analogLoop = a3app::AnalogLoop { 6, 8, {} };
    view.cfg.io.roundTripLoop = view.runningCfg.io.roundTripLoop = a3app::RoundTripLoop {};   // measured against it
    view.state.roundTripMeasured.state = a3app::RoundTripReport::State::measured;
    view.state.roundTripMeasured.samples = 211;
    view.state.roundTripMeasured.analog = a3app::RoundTripReport::Analog::notMeasured;
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    page.refresh();
    EXPECT_EQ (page.analogChainLabel().getText(), "not measured yet: cable output 6 to input 8");   // what to plug first
    EXPECT_TRUE (page.analogButton().isEnabled());
    EXPECT_TRUE (page.getLocalBounds().contains (page.analogButton().getBounds()));
    EXPECT_EQ (view.analogRequests, 0);
    page.analogButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (view.analogRequests, 1);
    EXPECT_EQ (view.roundTripRequests, 0);

    view.state.roundTripMeasured.analog = a3app::RoundTripReport::Analog::measured;
    view.state.roundTripMeasured.converters = { 48, 48000.0, "2026-10-30" };
    page.refresh();
    EXPECT_EQ (page.analogChainLabel().getText(), "5.4 ms   2026-10-30");
    expectPainted (a3apptest::snapshotAt (page, "audio-analog-chain", page.getWidth(), page.getHeight()));

    view.state.roundTripMeasured.analogReason = "no analog test pair chosen (I/O page)";
    view.cfg.io.analogLoop.reset();
    view.runningCfg.io.analogLoop.reset();
    page.refresh();
    EXPECT_FALSE (page.analogButton().isEnabled());
}

// I/O: the analog test pair is chosen from free channels off the digital loop; the stored share stays with it.
TEST_F (Pages, IoChoosesTheAnalogTestPair)
{
    FakeView view;
    for (int i = 1; i <= 14; ++i)
    {
        view.state.capturePorts.add ("system:capture_" + juce::String (i));
        view.state.playbackPorts.add ("system:playback_" + juce::String (i));
    }
    view.cfg.io.roundTripLoop = a3app::RoundTripLoop { 11, 11 };
    a3app::IoPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    auto& output = page.analogOutputBox();
    auto& input = page.analogInputBox();
    EXPECT_EQ (output.getNumItems(), 14);   // none + 14 - the digital loop's one
    EXPECT_TRUE (output.isVisible());
    page.chooseAnalogPair (false, 6);
    ASSERT_TRUE (view.cfg.io.analogLoop.has_value());
    EXPECT_EQ (view.cfg.io.analogLoop->output, 6);
    EXPECT_EQ (view.cfg.io.analogLoop->input, 0);
    view.cfg.io.analogLoop->converters = a3app::ConverterShare { 40, 48000.0, "2026-10-30" };
    page.chooseAnalogPair (true, 8);
    EXPECT_EQ (view.cfg.io.analogLoop->input, 8);
    EXPECT_TRUE (view.cfg.io.analogLoop->converters.has_value());
    page.refresh (true);
    EXPECT_TRUE (output.getText().contains ("6")) << output.getText();
    EXPECT_TRUE (input.getText().contains ("8")) << input.getText();
    page.chooseAnalogPair (false, 0);
    page.chooseAnalogPair (true, 0);   // the cable is gone; the converters' share stays (it is the hardware's)
    ASSERT_TRUE (view.cfg.io.analogLoop.has_value());
    EXPECT_EQ (view.cfg.io.analogLoop->output, 0);
    EXPECT_TRUE (view.cfg.io.analogLoop->converters.has_value());
    view.cfg.io.analogLoop->converters.reset();
    page.chooseAnalogPair (true, 0);
    EXPECT_FALSE (view.cfg.io.analogLoop.has_value());   // nothing left to keep
}

// I/O: the digital loop is chosen here too (2026-10-30), beside the analog test cable; a channel in use is refused
// with its reason and the choice stays as it was.
TEST_F (Pages, IoChoosesTheDigitalLoop)
{
    FakeView view;
    for (int i = 1; i <= 14; ++i)
    {
        view.state.capturePorts.add ("system:capture_" + juce::String (i));
        view.state.playbackPorts.add ("system:playback_" + juce::String (i));
    }
    view.cfg.io.roundTripLoop = a3app::RoundTripLoop { 11, 11 };
    view.cfg.io.jack["main_top_4"] = { "system:playback_5" };
    a3app::IoPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    auto& output = page.digitalOutputBox();
    auto& input = page.digitalInputBox();
    EXPECT_TRUE (output.isVisible());
    ASSERT_NE (output.getParentComponent(), nullptr);   // a row of the OUTPUT list (IoMeasurementPairsAreTheListsLastRows)
    EXPECT_TRUE (output.getParentComponent()->getLocalBounds().contains (output.getBounds()));
    EXPECT_EQ (output.getNumItems(), 15);   // none + 14
    EXPECT_EQ (output.getText(), "output 11") << "one channel a side";
    EXPECT_EQ (input.getText(), "input 11");

    page.chooseDigitalLoop (false, 13);
    ASSERT_TRUE (view.cfg.io.roundTripLoop.has_value());
    EXPECT_EQ (view.cfg.io.roundTripLoop->output, 13);
    page.chooseDigitalLoop (false, 5);   // main_top_4 is on 5
    EXPECT_EQ (view.cfg.io.roundTripLoop->output, 13);
    EXPECT_TRUE (page.messageLabel().getText().contains ("main_top_4")) << page.messageLabel().getText();
    page.chooseDigitalLoop (true, 0);
    EXPECT_FALSE (view.cfg.io.roundTripLoop.has_value());
    expectPainted (a3apptest::snapshotAt (page, "io-digital-loop", page.getWidth(), page.getHeight()));
}

// I/O (2026-10-30): the dropdown is the whole row beside its port. It names the device's port as the user means it
// ("Scarlett 18i20 USB out 3"), and there is no text beside it, no list of ports and no free-text port field: the
// engine links exactly what the dropdowns say and removes anything else on its ports.
TEST_F (Pages, IoDropdownsNameTheDevicesPortsAndNothingStandsBesideThem)
{
    FakeView view;
    giveHardwarePorts (view);
    view.state.deviceName = "Scarlett 18i20 USB";
    view.cfg.io.jack["main_sub"] = { "system:playback_1" };
    view.links = { { "a3-engine:main_sub", "AES67 a3nuc1 out:send_CH1" } };
    a3app::IoPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    page.showSub (a3app::IoPage::Sub::output);
    auto* box = page.routeBox ("main_sub");
    ASSERT_NE (box, nullptr);
    EXPECT_EQ (box->getText(), "Scarlett 18i20 USB out 1");
    EXPECT_EQ (box->getItemText (3), "Scarlett 18i20 USB out 3");
    const auto* row = box->getParentComponent();
    for (auto* child : row->getChildren())
    {
        if (child == box || ! child->isVisible())
            continue;
        if (child->getBounds().getVerticalRange().intersects (box->getBounds().getVerticalRange()))
            EXPECT_LE (child->getRight(), box->getX()) << "only the port's caption, left of the box";
        if (auto* label = dynamic_cast<juce::Label*> (child))
            EXPECT_FALSE (label->getText().contains ("AES67")) << label->getText();
    }
    EXPECT_GE (box->getRight(), row->getWidth() - a3app::layout::tokens (*row).padding - 4) << "the box runs to its card's end";
    std::function<void (juce::Component&)> noEditors = [&] (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (child->isShowing())
            {
                EXPECT_EQ (dynamic_cast<juce::TextEditor*> (child), nullptr) << "no free-text field on OUTPUT";
                EXPECT_EQ (dynamic_cast<juce::ListBox*> (child), nullptr) << "no port list on OUTPUT";
            }
            noEditors (*child);
        }
    };
    noEditors (page);
    expectPainted (a3apptest::snapshotAt (page, "io-output-device-names", page.getWidth(), page.getHeight()));

    view.state.deviceName = {};
    page.refresh (true);
    EXPECT_EQ (box->getText(), "Device out 1");
}

namespace
{
// Every showing list of the page scrolled to its end: where the measurement rows are.
void scrollToTheEnd (juce::Component& page)
{
    for (auto* child : page.getChildren())
        if (auto* scroller = dynamic_cast<juce::Viewport*> (child); scroller != nullptr && scroller->isVisible())
            scroller->setViewPosition (0, scroller->getViewedComponent()->getHeight());
}
}

// I/O (2026-10-30): the two measurement pairs are rows at the end of each sub-page's list, in the rows' style: on
// OUTPUT their output side, on INPUT their input side; the same dropdowns and reservation rules.
TEST_F (Pages, IoMeasurementPairsAreTheListsLastRows)
{
    FakeView view;
    for (int i = 1; i <= 14; ++i)
    {
        view.state.capturePorts.add ("system:capture_" + juce::String (i));
        view.state.playbackPorts.add ("system:playback_" + juce::String (i));
    }
    view.cfg.io.roundTripLoop = a3app::RoundTripLoop { 11, 11 };
    a3app::IoPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);

    page.showSub (a3app::IoPage::Sub::output);
    const auto* lastOutput = page.routeBox ("rec_R");
    ASSERT_NE (lastOutput, nullptr);
    for (auto* box : { &page.digitalOutputBox(), &page.analogOutputBox() })
    {
        EXPECT_EQ (box->getParentComponent(), lastOutput->getParentComponent()) << "in the scrolled list";
        EXPECT_GT (box->getY(), lastOutput->getY());
        EXPECT_EQ (box->getX(), lastOutput->getX()) << "the rows' style";
        EXPECT_EQ (box->getWidth(), lastOutput->getWidth());
    }
    EXPECT_GT (page.analogOutputBox().getY(), page.digitalOutputBox().getY());
    EXPECT_TRUE (page.digitalOutputBox().getText().contains ("output 11")) << page.digitalOutputBox().getText();
    scrollToTheEnd (page);
    expectPainted (a3apptest::snapshotAt (page, "io-output-measure-rows", page.getWidth(), page.getHeight()));

    page.showSub (a3app::IoPage::Sub::input);
    const auto* firstInput = page.routeBox ("analog_1");
    ASSERT_NE (firstInput, nullptr);
    for (auto* box : { &page.digitalInputBox(), &page.analogInputBox() })
    {
        EXPECT_EQ (box->getParentComponent(), firstInput->getParentComponent());
        EXPECT_GT (box->getY(), page.deckSourceBox (1)->getY()) << "after the decks: the list's end";
        EXPECT_EQ (box->getWidth(), firstInput->getWidth());
    }
    EXPECT_TRUE (page.digitalInputBox().getText().contains ("input 11")) << page.digitalInputBox().getText();
    scrollToTheEnd (page);
    expectPainted (a3apptest::snapshotAt (page, "io-input-measure-rows", page.getWidth(), page.getHeight()));
}

namespace
{
juce::Component* findAlertIn (juce::Component& parent)
{
    for (auto* child : descendants (parent))
        if (dynamic_cast<juce::AlertWindow*> (child) != nullptr)
            return child;
    return nullptr;
}
}


// No tooltips anywhere (2026-10-30): no control of the window carries one, the hosted AllRADecoder's included (its own
// TooltipWindow then has nothing to show), on every page, with Core's pages too.
TEST_F (Pages, NoTooltipsAnywhere)
{
    SpeakerRig rig;
    CoreWindowRig core;
    a3app::MainWindow window (rig.view, manager, { rig.installed.root() }, false, [] {}, { core.client.get(), {} }, {}, rig.userRoot);
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    window.setMonitorShown (true);
    for (const auto& name : juce::StringArray { "AUDIO", "SPEAKERS", "ZONES", "INPUT", "OUTPUT", "DEPARTMENTS", "CALIBRATION", "RIG", "STATUS",
                                                juce::String::fromUTF8 ("A³ CORE"), "ABOUT", "BENCH" })
    {
        window.showPage (name);
        window.refreshNow();
    }
    int iem = 0;
    for (auto* child : descendants (*window.getContentComponent()))
    {
        if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (child))
            EXPECT_TRUE (client->getTooltip().isEmpty()) << child->getName() << ": " << client->getTooltip();
        iem += dynamic_cast<a3app::SpeakersPage*> (child) != nullptr ? 1 : 0;
    }
    EXPECT_EQ (iem, 1) << "the hosted editor was looked at";
}

// Every dropdown of the window is the touch list (Dropdown.h), on every page (the hosted IEM editor is IEM's).
TEST_F (Pages, EveryDropdownScrollsByTouch)
{
    SpeakerRig rig;
    a3app::MainWindow window (rig.view, manager, { rig.installed.root() }, false, [] {}, {}, {}, rig.userRoot);
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    window.refreshNow();
    auto& content = *window.getContentComponent();
    std::set<juce::Component*> iem;
    for (auto* child : descendants (content))
        if (auto* speakers = dynamic_cast<a3app::SpeakersPage*> (child); speakers != nullptr && speakers->editor() != nullptr)
            for (auto* part : descendants (*speakers->editor()))
                iem.insert (part);
    int boxes = 0;
    for (auto* child : descendants (content))
        if (dynamic_cast<juce::ComboBox*> (child) != nullptr && iem.count (child) == 0)
        {
            ++boxes;
            EXPECT_NE (dynamic_cast<a3app::Dropdown*> (child), nullptr) << child->getName() << " in "
                                                                       << child->getParentComponent()->getName();
        }
    EXPECT_GT (boxes, 10);
}

// ZONES (2026-10-30, the maintainer: RENAME and DELETE do not work). The rig's only own variant is the one that runs,
// so both keys were disabled for every row, the reason only in a tooltip that was off. Now the keys always answer: a
// refusal is said on the page, and a line under the list says what they would do for the picked variant.
TEST_F (Pages, ZonesKeysAlwaysAnswer)
{
    ZonesRig rig;
    a3app::MainWindow window (rig.speakers.view, manager, { rig.speakers.installed.root() }, false, [] {}, {}, {}, rig.speakers.userRoot);
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    auto& content = *window.getContentComponent();
    window.showPage ("ZONES");
    auto* page = firstDescendant<a3app::ZonesPage> (*shownPage (window));
    ASSERT_NE (page, nullptr);
    page->selectVariant (rig.rowOf (*page, rig.club));
    window.refreshNow();
    for (const auto& name : { "LOAD", "RENAME", "DELETE" })
        EXPECT_TRUE (buttonsLabelled (content, name).front()->isVisible()) << name;
    for (const auto& name : { "RENAME", "DELETE" })
    {
        auto* key = buttonsLabelled (content, name).front();
        EXPECT_TRUE (key->isEnabled()) << name;
        key->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        EXPECT_TRUE (page->messageLabel().getText().contains ("runs")) << name << ": " << page->messageLabel().getText();
        EXPECT_EQ (findAlertIn (window), nullptr) << name << ": refused before anything is asked";
    }
    EXPECT_TRUE (rig.club.isDirectory());
    page->selectVariant (rig.rowOf (*page, rig.spare));
    window.refreshNow();
    expectPainted (a3apptest::snapshotAt (content, "portrait-zones-keys-answer", content.getWidth(), content.getHeight()));
}

// The questions RENAME and DELETE ask open inside the window: as a desktop window of class a3-core, i3 would tile it
// beside the window on its workspace (for_window [class="a3-core"]).
TEST_F (Pages, ZonesDialogsOpenInsideTheWindow)
{
    // A JUCE AlertWindow makes a desktop peer as it is built (TopLevelWindow), before it is put into the window.
    const char* display = std::getenv ("DISPLAY");
    if (display == nullptr || *display == '\0')
        GTEST_SKIP() << "needs an X display (e.g. DISPLAY=:2)";
    ZonesRig rig;
    a3app::MainWindow window (rig.speakers.view, manager, { rig.speakers.installed.root() }, false, [] {}, {}, {}, rig.speakers.userRoot);
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    auto& content = *window.getContentComponent();
    window.showPage ("ZONES");
    auto* page = firstDescendant<a3app::ZonesPage> (*shownPage (window));
    ASSERT_NE (page, nullptr);
    page->selectVariant (rig.rowOf (*page, rig.spare));
    window.refreshNow();
    for (const auto& name : { "DELETE", "RENAME" })
    {
        buttonsLabelled (content, name).front()->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        auto* alert = findAlertIn (window);
        ASSERT_NE (alert, nullptr) << name;
        EXPECT_FALSE (alert->isOnDesktop()) << name;
        EXPECT_TRUE (content.getLocalBounds().contains (alert->getBounds())) << name;
        expectPainted (a3apptest::snapshotAt (window, "portrait-zones-dialog-" + juce::String (name).toLowerCase(),
                                              window.getWidth(), window.getHeight()));
        alert->exitModalState (0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
    EXPECT_TRUE (rig.spare.isDirectory()) << "cancelled: nothing changed";
}

// ZONES' info (2026-10-30, the maintainer: present it more nicely): per zone a grid of what the decoder is (label,
// value with unit) and its loudspeakers as a table, one row each, columns aligned.
TEST_F (Pages, ZonesInfoIsATable)
{
    FakeView view;
    a3app::ZonesPage page (view, { a3test::shippedSpatialDirectory });
    page.selectVariant (0);
    const auto info = a3app::zoneInfo (a3::SpeakerZone::main, a3test::shippedSpatialDirectory);
    EXPECT_EQ (info.zone, "MAIN");
    EXPECT_TRUE (info.error.isEmpty()) << info.error;
    const std::map<juce::String, juce::String> facts (info.facts.begin(), info.facts.end());
    EXPECT_TRUE (facts.at ("Decoder").contains ("AllRADecoder v0.9.5")) << facts.at ("Decoder");
    EXPECT_TRUE (facts.at ("Exported").startsWith ("28 Feb 2026")) << facts.at ("Exported");
    EXPECT_EQ (facts.at ("Order"), "3");
    EXPECT_EQ (facts.at ("Weights"), "inPhase");
    EXPECT_EQ (facts.at ("Sub"), "ch 1");
    EXPECT_EQ (facts.at ("Crossover"), "150 Hz");
    ASSERT_EQ (info.speakers.size(), 6u);
    EXPECT_EQ (info.speakers[0], (std::array<juce::String, 3> { "2", juce::String::fromUTF8 ("45\xc2\xb0"), juce::String::fromUTF8 ("0\xc2\xb0") }));
    bool imaginary = false;
    for (const auto& row : info.speakers)
        imaginary |= row[0] == "imaginary" && row[2] == juce::String::fromUTF8 ("90\xc2\xb0");
    EXPECT_TRUE (imaginary);
    const auto broken = a3app::zoneInfo (a3::SpeakerZone::booth, juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a3-none"));
    EXPECT_TRUE (broken.error.isNotEmpty());
    expectPainted (a3apptest::snapshotAt (page, "zones-info-table", a3apptest::portraitWidth, a3apptest::portraitHeight * 7 / 8));
}

// SPEAKERS tidy (2026-10-30): the bar above the editor in the rows of AUDIO and I/O: a title row with the zone keys
// and SAVE TO ZONE, then caption + control rows whose captions share one column and whose controls share one row
// height; the editor below with a gap.
TEST_F (Pages, SpeakersBarIsInTheRowsOfTheOtherPages)
{
    SpeakerRig rig;
    a3app::SpeakersPage page (rig.view, rig.userRoot);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 7 / 8);
    auto* main = buttonsLabelled (page, "MAIN").front();
    auto* booth = buttonsLabelled (page, "BOOTH").front();
    auto* save = buttonsLabelled (page, "SAVE TO ZONE").front();
    EXPECT_EQ (main->getHeight(), booth->getHeight());
    EXPECT_EQ (main->getHeight(), save->getHeight());
    EXPECT_EQ (main->getY(), save->getY());
    auto* sub = labelSaying (page, "Sub");
    auto* crossover = labelSaying (page, "Crossover");
    ASSERT_NE (sub, nullptr);
    ASSERT_NE (crossover, nullptr);
    EXPECT_EQ (sub->getX(), crossover->getX()) << "one caption column";
    EXPECT_LT (sub->getY(), crossover->getY()) << "a row each";
    EXPECT_EQ (sub->getHeight(), crossover->getHeight());
    auto* subBox = firstDescendant<juce::ComboBox> (page);
    auto* crossoverSlider = firstDescendant<juce::Slider> (page);
    ASSERT_NE (subBox, nullptr);
    ASSERT_NE (crossoverSlider, nullptr);
    EXPECT_EQ (subBox->getX(), crossoverSlider->getX()) << "one control column";
    EXPECT_EQ (subBox->getHeight(), crossoverSlider->getHeight());
    EXPECT_EQ (subBox->getHeight(), main->getHeight()) << "one row height";
    const auto editorTop = areaIn (page, *page.editor()).getY();
    EXPECT_GT (editorTop, page.messageLabel().getBottom());
    expectPainted (a3apptest::snapshotAt (page, "speakers-tidy", page.getWidth(), page.getHeight()));
}

// The hosted AllRADecoder draws no ground of its own (IEM fills 0xff2d2d2d): it sits on the page's surface.
TEST_F (Pages, TheAllRaEditorSitsOnThePagesGround)
{
    SpeakerRig rig;
    a3app::SpeakersPage page (rig.view, rig.userRoot);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 7 / 8);
    ASSERT_NE (page.editor(), nullptr);
    EXPECT_FALSE (page.editor()->isOpaque());
    juce::Image picture (juce::Image::ARGB, page.editor()->getWidth(), page.editor()->getHeight(), true);
    {
        juce::Graphics g (picture);
        page.editor()->paint (g);
    }
    int iemGrey = 0;
    for (int y = 0; y < picture.getHeight(); y += 7)
        for (int x = 0; x < picture.getWidth(); x += 7)
            iemGrey += picture.getPixelAt (x, y).getARGB() == 0xff2d2d2d ? 1 : 0;
    EXPECT_EQ (iemGrey, 0) << "the editor's own paint leaves its ground to the page";
    const auto shot = a3apptest::snapshotAt (page, "speakers-on-the-page-ground", page.getWidth(), page.getHeight());
    expectPainted (shot);
    const auto area = areaIn (page, *page.editor());
    EXPECT_EQ (shot.getPixelAt (area.getX() + 2, area.getY() + area.getHeight() / 2).getARGB(), a3app::palette::raised.getARGB())
        << "the margin beside IEM's table is the card's (third round: the editor sits on a card)";
}

// AUDIO (2026-10-30, second round): cards in the ZONES card's style, DRIVER (type, rate, block, periods: every setting
// a dropdown showing what is set), CHANNELS (N and decks, the CHANNELS page until then), MEASURE, CLOCK (the master
// and the one-word state). No info lines: what runs in detail is on STATUS. One text size.
TEST_F (Pages, AudioIsCards)
{
    FakeView view;
    view.state.timing.available = true;
    view.clockReport = clockReport (a3clock::Health::locked, "");
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 7 / 8);
    page.refresh();
    const auto cards = page.cardAreas();
    ASSERT_EQ (cards.size(), 4u);
    for (size_t i = 0; i < cards.size(); ++i)
    {
        EXPECT_TRUE (page.getLocalBounds().contains (cards[i])) << i;
        if (i > 0)
            EXPECT_GT (cards[i].getY(), cards[i - 1].getBottom()) << "a gap between the cards";
    }
    const auto in = [&] (size_t card, const juce::Component& c) { return cards[card].contains (c.getBounds()); };
    for (const auto* box : { &page.typeChooser(), &page.rateChooser(), &page.blockChooser(), &page.periodsChooser() })
    {
        EXPECT_TRUE (box->isVisible());
        EXPECT_TRUE (in (0, *box)) << "DRIVER";
        EXPECT_NE (box->getText(), "") << "the dropdown shows what is set";
    }
    EXPECT_TRUE (cards[1].contains (page.channelsCard().getBounds())) << "STEREO CHANNELS AND STEMDECKS";
    EXPECT_EQ (page.channelsCard().channelsChooser().getText(), "4");
    EXPECT_TRUE (in (2, page.roundTripLabel()) && in (2, page.measureButton())) << "MEASURE";
    EXPECT_EQ (page.clockPanel().getBounds(), cards[3].reduced (a3app::layout::tokens (page).padding));
    EXPECT_FALSE (page.clockPanel().stateLabel().isVisible()) << "the word is in the footer (third round)";
    EXPECT_TRUE (page.clockPanel().masterBox().isVisible());
    EXPECT_FALSE (page.clockPanel().countersLabel().isVisible()) << "the numbers are on STATUS";
    // 2026-10-30: "CHANNELS" was not understood; the card says what it counts.
    for (const auto* heading : { "DRIVER", "STEREO CHANNELS AND STEMDECKS", "MEASURE" })
        EXPECT_NE (labelSaying (page, heading), nullptr) << heading;
    for (const auto* caption : { "STEREO CHANNELS AND STEMDECKS", "Stereo channels", "StemDecks" })
        if (const auto* label = labelSaying (page, caption); label != nullptr)
            EXPECT_LE (juce::GlyphArrangement::getStringWidth (label->getFont(), caption),
                       (float) label->getBorderSize().subtractedFrom (label->getLocalBounds()).getWidth()) << caption << ": whole, not squeezed";
        else
            ADD_FAILURE() << caption;
    std::set<int> sizes;
    for (auto* child : descendants (page))
        if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->isVisible() && label->getText().isNotEmpty()
                                                               && dynamic_cast<juce::ComboBox*> (child->getParentComponent()) == nullptr)
        {
            sizes.insert (juce::roundToInt (label->getFont().getHeight() * 10.0f));
            for (const auto* gone : { "Bit depth", "DSP load", "xruns", "set by", "Sample rate   " })
                EXPECT_FALSE (label->getText().contains (gone)) << label->getText();
        }
    EXPECT_EQ (sizes.size(), 1u) << "one font size on the page";
    const auto shot = a3apptest::snapshotAt (page, "window2-audio", page.getWidth(), page.getHeight());
    expectPainted (shot);
    EXPECT_EQ (shot.getPixelAt (cards[1].getRight() - 3, cards[1].getCentreY()).getARGB(), a3app::palette::raised.getARGB())
        << "a card is the ZONES card's ground";
}

// STATUS has what AUDIO no longer shows (2026-10-30): rate and block x periods, where the rate comes from, the bit
// depth, DSP load and xruns, and the clock's detail.
TEST_F (Pages, StatusHasTheDriverAndTheClocksDetail)
{
    FakeView view;
    view.state.timing.available = true;
    view.state.dspLoadPercent = 23.0f;
    view.state.xruns = 2;
    view.clockReport = clockReport (a3clock::Health::locked, "");
    a3app::StatusPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 7 / 8);
    page.refresh();
    const auto driver = a3app::driverInfo (view);
    EXPECT_EQ (driver.rate, "44100 Hz");
    EXPECT_TRUE (driver.timing.contains ("3 periods")) << driver.timing;
    EXPECT_TRUE (driver.rateSource.isNotEmpty());
    EXPECT_TRUE (driver.bitDepth.contains ("32")) << driver.bitDepth;
    EXPECT_EQ (driver.load, "23.0 %");
    EXPECT_EQ (driver.xruns, "2");
    const auto& clock = page.clockPanel();
    EXPECT_TRUE (clock.isVisible());
    EXPECT_TRUE (page.getLocalBounds().contains (clock.getBounds()));
    EXPECT_TRUE (clock.countersLabel().isVisible());
    EXPECT_TRUE (clock.countersLabel().getText().contains ("corrections")) << clock.countersLabel().getText();
    EXPECT_TRUE (clock.stateLabel().getText().contains ("phase error")) << clock.stateLabel().getText();
    EXPECT_FALSE (clock.masterBox().isVisible()) << "the choice is AUDIO's";
    // 2026-10-30: three cards (what runs, the clock, the meters); MISSING and DROPPED went (RIG says what runs where).
    EXPECT_EQ (page.cardAreas().size(), 3u);
    // The clock's lines follow each other as the driver's do: no empty line, nothing spread over the card.
    std::vector<juce::Rectangle<int>> lines;
    for (const auto* label : { &clock.stateLabel(), &clock.roleLabel(), &clock.rateLabel(), &clock.countersLabel(), &clock.noteLabel() })
        if (label->isVisible())
        {
            EXPECT_TRUE (label->getText().isNotEmpty()) << "an empty line is not shown";
            lines.push_back (label->getBounds());
        }
    ASSERT_GE (lines.size(), 4u);
    for (size_t i = 1; i < lines.size(); ++i)
        EXPECT_EQ (lines[i].getY(), lines[i - 1].getBottom()) << i;
    EXPECT_EQ (lines.front().getHeight(), lines[1].getHeight()) << "one line height";
    EXPECT_LE (lines.back().getBottom(), clock.getHeight()) << "inside the panel";
    EXPECT_GE (lines.back().getBottom(), clock.getHeight() - lines.front().getHeight()) << "the card ends with its last line";
    expectPainted (a3apptest::snapshotAt (page, "window2-status", page.getWidth(), page.getHeight()));
}

// ENGINE's round trip and network (2026-10-31): four "Label: value" lines, values only; the AES67 latency is StemDeck's
// shipped sender (sess.latency 3 ms, packet 0.25 ms) plus the receiver's buffer read from the rendered config.
TEST_F (Pages, EngineSaysTheRoundTripAndTheAes67Latency)
{
    const auto conf = juce::File (A3_APP_SOURCE_DIR).getSiblingFile ("platform-config/debian-x86_64/a3-core/home/aaa/.local/share/a3-core/pipewire/pipewire-aes67.conf.in");
    const auto receiver = a3app::deckReceiveLatencyMs (conf.loadFileAsString());
    ASSERT_TRUE (receiver.has_value());
    EXPECT_DOUBLE_EQ (*receiver, 6.0) << "the deck rule's sess.latency.msec, not the host senders' 12";
    EXPECT_EQ (a3app::aes67LatencyText (receiver), "9.3 ms");
    EXPECT_FALSE (a3app::deckReceiveLatencyMs ("context.modules = []").has_value());

    a3app::RoundTrip estimate;
    estimate.sampleRate = 44100;
    estimate.captureSamples = 256;
    estimate.processingSamples = 128;
    estimate.playbackSamples = 384;
    a3app::RoundTripReport measured;
    auto lines = a3app::roundTripLines (estimate, measured, a3app::aes67LatencyText (receiver));
    EXPECT_EQ (lines, (juce::StringArray { "Estimated: 17.4 ms (768 samples)", "Measured digital: not measured",
                                           "Measured analog: not measured", "AES67 latency: 9.3 ms" }));
    measured.state = a3app::RoundTripReport::State::measured;
    measured.samples = 441;
    measured.atMillis = juce::Time (2026, 9, 31, 0, 12).toMilliseconds();
    measured.analog = a3app::RoundTripReport::Analog::measured;
    measured.converters = { 44, 44100.0, "2026-10-30" };
    lines = a3app::roundTripLines (estimate, measured, "9.3 ms");
    EXPECT_EQ (lines[1], "Measured digital: 10.0 ms  00:12");
    EXPECT_EQ (lines[2], "Measured analog: 11.0 ms  2026-10-30");
    for (const auto& line : lines)
        EXPECT_FALSE (line.containsAnyOf ("=+")) << "no parts: " << line;
}

// The window's keys (2026-10-30): CHANNELS, ZONES and DEPARTMENTS have none (AUDIO's card, SPEAKERS' and I/O's
// sub-pages), so six keys share the row beside the switch and their text is larger again. SYSTEM and ABOUT (the Canvas's
// content) are STATUS' sub-pages: as a seventh key SYSTEM made the text smaller than a quarter of the key (11.95 < 12.25 px).
TEST_F (Pages, SixPageKeysAndTheSubPages)
{
    SpeakerRig rig;
    CoreWindowRig core;
    a3app::MainWindow window (rig.view, manager, { rig.installed.root() }, false, [] {}, { core.client.get(), {} }, {}, rig.userRoot);
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    window.setWorkspaceRunner ([] (const std::vector<std::string>&) { return std::string(); });
    EXPECT_EQ (window.pageList(), (juce::StringArray { "AUDIO", "SPEAKERS", "I/O", "CALIBRATION", "RIG", "STATUS", "MONITOR" }));
    auto& content = *window.getContentComponent();
    juce::TextButton* calibration = nullptr;
    for (auto* key : buttonsLabelled (content, "CALIBRATION"))
        if (key->getParentComponent() == &content)
            calibration = key;
    ASSERT_NE (calibration, nullptr);
    const auto text = look.getTextButtonFont (*calibration, calibration->getHeight()).getHeight();
    EXPECT_GE (text, (float) calibration->getHeight() * 0.25f) << "the longest name, at a quarter of the key at least";

    window.showPage ("ZONES");
    EXPECT_EQ (window.currentPage(), "SPEAKERS");
    EXPECT_EQ (window.currentSubPage(), "ZONES");
    EXPECT_TRUE (buttonsLabelled (content, "RENAME").front()->isVisible());
    expectPainted (a3apptest::snapshotAt (content, "window2-zones", content.getWidth(), content.getHeight()));
    window.showPage ("SPEAKERS");
    EXPECT_EQ (window.currentSubPage(), "ZONES") << "the page keeps the sub-page last shown";
    auto* speakersKey = buttonsLabelled (*shownPage (window), "SPEAKERS").front();
    speakersKey->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_EQ (window.currentSubPage(), "SPEAKERS");
    EXPECT_FALSE (buttonsLabelled (content, "RENAME").front()->isVisible());
    expectPainted (a3apptest::snapshotAt (content, "window2-speakers", content.getWidth(), content.getHeight()));

    window.showPage ("DEPARTMENTS");
    EXPECT_EQ (window.currentPage(), "I/O");
    EXPECT_EQ (window.currentSubPage(), "DEPARTMENTS");
    expectPainted (a3apptest::snapshotAt (content, "window2-io-departments", content.getWidth(), content.getHeight()));
    window.showPage ("INPUT");
    expectPainted (a3apptest::snapshotAt (content, "window2-io-input", content.getWidth(), content.getHeight()));
    window.showPage ("AUDIO");
    expectPainted (a3apptest::snapshotAt (content, "window2-window-audio", content.getWidth(), content.getHeight()));
    window.showPage ("STATUS");
    expectPainted (a3apptest::snapshotAt (content, "window2-window-status", content.getWidth(), content.getHeight()));
}

TEST_F (Pages, TheWindowHasTheWorkspaceSwitch)
{
    FakeView view;
    a3app::MainWindow window (view, manager, {}, false, [] {});
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    std::vector<std::vector<std::string>> calls;
    window.setWorkspaceRunner ([&] (const std::vector<std::string>& argv) { calls.push_back (argv); return std::string(); });
    auto& content = *window.getContentComponent();
    auto* keys = window.workspaceSwitch();
    ASSERT_NE (keys, nullptr);
    const auto app = areaIn (content, keys->appKey()), arrow = areaIn (content, keys->arrowKey());
    EXPECT_TRUE (keys->appKey().isShowing() || keys->appKey().isVisible());
    EXPECT_TRUE (window.bands().navigation.contains (app)) << app.toString();
    EXPECT_TRUE (window.bands().navigation.contains (arrow)) << arrow.toString();
    EXPECT_GT (app.getX(), content.getWidth() / 2) << "at the right end";
    EXPECT_GE (app.getHeight(), 28) << "a fingertip";
    for (const auto& name : window.pageList())
        for (auto* key : buttonsLabelled (content, name))
            if (key->getParentComponent() == &content)
                EXPECT_FALSE (areaIn (content, *key).intersects (app.getUnion (arrow))) << name;
    keys->appKey().onTap();
    EXPECT_EQ (calls, (std::vector<std::vector<std::string>> { a3app::screen::goToCommand (2) })) << "StemDeck, whatever Core does";
    expectPainted (a3apptest::snapshotAt (content, "window-workspace-switch", content.getWidth(), content.getHeight()));
}

namespace
{
// Whether `c` sits in a scrolled list (its cards run the list's width, not the page's margin).
bool inViewport (juce::Component& c)
{
    for (auto* p = c.getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (dynamic_cast<juce::Viewport*> (p) != nullptr)
            return true;
    return false;
}

// One page's cards against the window's tokens: each card from margin to margin (a scrolled list's from edge to edge),
// neighbours one gap apart, and what is on a card one padding in from its edges.
void expectTheGrid (juce::Component& c, const a3app::CardPage& page, const a3app::layout::Tokens& t, const juce::String& where)
{
    auto cards = page.cardAreas();
    ASSERT_FALSE (cards.empty()) << where;
    const bool scrolled = inViewport (c);
    for (const auto& card : cards)
    {
        EXPECT_EQ (card.getX(), scrolled ? 0 : t.margin) << where << " " << card.toString();
        EXPECT_EQ (card.getRight(), c.getWidth() - (scrolled ? 0 : t.margin)) << where << " " << card.toString();
    }
    std::sort (cards.begin(), cards.end(), [] (auto a, auto b) { return a.getY() < b.getY(); });
    for (size_t i = 1; i < cards.size(); ++i)
        EXPECT_EQ (cards[i].getY() - cards[i - 1].getBottom(), t.gap) << where << ": between " << cards[i - 1].toString() << " and "
                                                                      << cards[i].toString();
    for (const auto& card : cards)
    {
        int left = std::numeric_limits<int>::max(), top = std::numeric_limits<int>::max(), parts = 0;
        for (auto* child : c.getChildren())
        {
            if (! child->isVisible() || dynamic_cast<a3app::DropdownList*> (child) != nullptr)
                continue;
            const auto area = c.getLocalArea (child, child->getLocalBounds());
            if (! area.intersects (card))
                continue;
            ++parts;
            EXPECT_TRUE (card.reduced (t.padding).contains (area)) << where << ": " << child->getName() << " " << area.toString()
                                                                   << " on " << card.toString();
            left = juce::jmin (left, area.getX());
            top = juce::jmin (top, area.getY());
        }
        if (parts == 0)
            continue;   // a card whose content is painted (STATUS' lines)
        EXPECT_EQ (left, card.getX() + t.padding) << where << " " << card.toString();
        EXPECT_EQ (top, card.getY() + t.padding) << where << " " << card.toString();
    }
}
}

// One design system (2026-10-30, the maintainer: "aren't there standards for what looks symmetric and correct?"):
// every page, every sub-page, is cards on the window's tokens (layout::Tokens): the same margin, padding and gap.
TEST_F (Pages, EveryPageIsCardsOnOneGrid)
{
    SpeakerRig rig;
    CoreWindowRig core;
    a3app::MainWindow window (rig.view, manager, { rig.installed.root() }, false, [] {}, { core.client.get(), {} }, {}, rig.userRoot);
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    auto& content = *window.getContentComponent();
    for (const auto& name : juce::StringArray { "AUDIO", "SPEAKERS", "ZONES", "INPUT", "OUTPUT", "DEPARTMENTS", "CALIBRATION", "RIG", "STATUS",
                                                juce::String::fromUTF8 ("A³ CORE"), "ABOUT" })
    {
        window.showPage (name);
        window.refreshNow();
        EXPECT_TRUE (window.currentPage() == name || window.currentSubPage() == name) << name << ": not reached";
        auto* page = shownPage (window);
        ASSERT_NE (page, nullptr) << name;
        const auto t = a3app::layout::tokens (*page);
        int pages = 0;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            if (! c.isVisible())
                return;
            if (auto* cards = dynamic_cast<a3app::CardPage*> (&c))
            {
                ++pages;
                expectTheGrid (c, *cards, t, juce::String (name) + " / " + c.getName());
            }
            for (auto* child : c.getChildren())
                walk (*child);
        };
        walk (*page);
        EXPECT_GT (pages, 0) << name << ": no cards";
        expectPainted (a3apptest::snapshotAt (content, "window3-" + juce::String (name).toLowerCase().removeCharacters ("/"),
                                              content.getWidth(), content.getHeight()));
    }
}

// RIG is a page of the key row again (2026-10-31; FILTER went to CALIBRATION), STATUS is ENGINE and ABOUT.
TEST_F (Pages, RigIsAPageAndStatusIsEngineAndAbout)
{
    SpeakerRig rig;
    a3app::SystemProbe probe ({}, false);
    a3app::MainWindow window (rig.view, manager, { rig.installed.root() }, false, [] {}, {}, {}, rig.userRoot, { &probe });
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    EXPECT_TRUE (window.pageList().contains ("RIG"));
    EXPECT_FALSE (window.pageList().contains ("FILTER"));
    auto& content = *window.getContentComponent();
    window.showPage ("STATUS");
    EXPECT_EQ (window.currentSubPage(), juce::String::fromUTF8 ("A³ CORE")) << "a3-core, not Engine (2026-10-31)";
    for (const auto& key : { juce::String::fromUTF8 ("A³ CORE"), juce::String ("ABOUT") })
        EXPECT_FALSE (buttonsLabelled (*shownPage (window), key).empty()) << key;
    EXPECT_TRUE (buttonsLabelled (*shownPage (window), "RIG").empty()) << "RIG is no sub-page";
    EXPECT_FALSE (probe.isWanted()) << "the machine is asked only while RIG or ABOUT is shown";
    window.refreshNow();
    expectPainted (a3apptest::snapshotAt (content, "system3-engine", content.getWidth(), content.getHeight()));
    window.showPage ("RIG");
    EXPECT_TRUE (probe.isWanted());
    EXPECT_EQ (window.currentPage(), "RIG");
    for (const auto* heading : { "ENTITIES" })
        EXPECT_NE (labelSaying (content, heading), nullptr) << heading;
    EXPECT_FALSE (buttonsLabelled (content, "Start all").empty());
    window.refreshNow();
    expectPainted (a3apptest::snapshotAt (content, "system3-window-rig", content.getWidth(), content.getHeight()));
    window.showPage ("ABOUT");
    EXPECT_EQ (window.currentPage(), "STATUS");
    EXPECT_EQ (window.currentSubPage(), "ABOUT");
    EXPECT_TRUE (probe.isWanted());
    window.refreshNow();
    expectPainted (a3apptest::snapshotAt (content, "system3-window-about", content.getWidth(), content.getHeight()));
    window.showPage ("AUDIO");
    EXPECT_FALSE (probe.isWanted());
}
