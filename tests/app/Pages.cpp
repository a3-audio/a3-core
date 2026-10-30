// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdlib>
#include <gtest/gtest.h>
#include <set>
#include <juce_opengl/juce_opengl.h>
#include "AppTruth.h"
#include "EngineFixture.h"
#include "FakeView.h"
#include "PageSnapshot.h"
#include "RemoteRig.h"
#include "AudioPage.h"
#include "CalibrationPage.h"
#include "ChannelsPage.h"
#include "ConfigEdits.h"
#include "FilterPage.h"
#include "IoPage.h"
#include "Look.h"
#include "MainWindow.h"
#include "SaveBar.h"
#include "SpeakersPage.h"
#include "SpatialTestData.h"
#include "StatusPage.h"
#include "ZonesPage.h"
#include "Dropdown.h"
#include "TouchTooltips.h"
#include <array>
#include <map>

namespace
{
using a3apptest::FakeView;
using a3apptest::RemoteRig;
using a3apptest::snapshot;
using a3apptest::expectPainted;
using a3apptest::NoDevices;

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
    EXPECT_NE (page.bitDepthLabel().getText().indexOf ("set by the device"), -1);
}

// AUDIO shows the estimated round trip and its parts, for JACK and for a device, and follows a new quantum.
TEST_F (Pages, AudioShowsTheEstimatedRoundTrip)
{
    FakeView view;
    view.state.captureLatency = 256;
    view.state.playbackLatency = 384;
    {
        a3app::AudioPage page (view, manager);
        page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
        const auto text = page.roundTripLabel().getText();
        EXPECT_TRUE (text.contains ("estimated")) << text;
        EXPECT_TRUE (text.contains ("768 samples")) << text;   // 256 + 128 + 384
        EXPECT_TRUE (page.roundTripLabel().isVisible());
        EXPECT_TRUE (page.getLocalBounds().contains (page.roundTripLabel().getBounds()));
        expectPainted (a3apptest::snapshotAt (page, "audio-roundtrip-jack", page.getWidth(), page.getHeight()));
        view.state.blockSize = 256;
        page.refresh();
        EXPECT_TRUE (page.roundTripLabel().getText().contains ("896 samples")) << page.roundTripLabel().getText();
    }
    view.runDevice();
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    EXPECT_TRUE (page.roundTripLabel().isVisible());
    EXPECT_TRUE (page.getLocalBounds().contains (page.roundTripLabel().getBounds()));
    expectPainted (a3apptest::snapshotAt (page, "audio-roundtrip-device", page.getWidth(), page.getHeight()));
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
TEST_F (Pages, AudioShowsTheClockInEveryState)
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
        a3app::AudioPage page (view, manager);
        page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
        page.refresh();
        const auto& clock = page.clockPanel();
        EXPECT_TRUE (clock.stateLabel().getText().startsWith (c.word)) << c.name << ": " << clock.stateLabel().getText();
        EXPECT_EQ (clock.warns(), ! c.warning.isTransparent()) << c.name;
        EXPECT_TRUE (page.getLocalBounds().contains (clock.getBounds())) << c.name;
        EXPECT_GT (clock.getHeight(), 0) << c.name;
        const auto image = a3apptest::snapshotAt (page, juce::String ("audio-clock-") + c.name, page.getWidth(), page.getHeight());
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
        EXPECT_GE (clock.getHeight(), layout.height / 5) << layout.name;
        EXPECT_GT (clock.stateLabel().getHeight(), 0) << layout.name;
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
}

// The PTP role is its own line under the state: neutral while not read yet, a warning only when
// known and not MASTER.
TEST_F (Pages, ThePtpRoleIsASecondLine)
{
    FakeView view;
    view.clockReport = clockReport (a3clock::Health::locked, "");
    a3app::AudioPage page (view, manager);
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
    EXPECT_TRUE (clock.noteLabel().getText().contains ("external PTP grandmaster")) << clock.noteLabel().getText();
}

TEST_F (Pages, TheClockSaysItsNumbers)
{
    FakeView view;
    view.clockReport = clockReport (a3clock::Health::locked, "");
    a3app::AudioPage page (view, manager);
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

TEST_F (Pages, JackRateIsReadOnly)
{
    FakeView view;
    a3app::AudioPage page (view, manager);
    EXPECT_FALSE (page.rateLabel().isEditable());
    EXPECT_FALSE (page.quantumLabel().isEditable());
    EXPECT_NE (page.rateLabel().getText().indexOf ("44100"), -1);
    EXPECT_NE (page.quantumLabel().getText().indexOf ("128"), -1);
    EXPECT_NE (page.rateSourceLabel().getText().indexOf ("JACK server"), -1);
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
    EXPECT_TRUE (page.blockChooser().isVisible());
    page.chooseDeviceType ("none");   // no change, no second edit
    EXPECT_EQ (view.edited, 1);

    page.chooseDeviceType (a3app::AudioPage::pipewireType);
    EXPECT_EQ (view.cfg.backend.type, a3app::BackendSettings::Type::jack);
    EXPECT_FALSE (page.showsDeviceSelector());
}

// The block on PipeWire: the choices, and nothing applied before APPLY; a new block runs live. No periods
// (2026-10-30): nothing on the page names them.
TEST_F (Pages, PipeWireBlockAppliesOnlyOnApply)
{
    FakeView view;
    view.state.timing.available = true;
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    page.refresh();
    auto& block = page.blockChooser();
    ASSERT_EQ (block.getNumItems(), 6);
    for (const auto& [index, text] : { std::pair { 0, "32 frames" }, { 2, "128 frames" }, { 5, "1024 frames" } })
        EXPECT_EQ (block.getItemText (index), text);
    EXPECT_EQ (block.getText(), "128 frames");
    for (auto* child : page.getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
            EXPECT_FALSE (label->getText().containsIgnoreCase ("period")) << label->getText();
    EXPECT_FALSE (page.applyButton().isVisible());

    page.chooseBlock (64);
    EXPECT_TRUE (view.timings.empty()) << "a choice is not applied on its own";
    EXPECT_TRUE (page.applyButton().isVisible());
    EXPECT_TRUE (page.confirmLabel().getText().contains ("live")) << page.confirmLabel().getText();
    page.cancelButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    EXPECT_TRUE (view.timings.empty());
    EXPECT_EQ (block.getText(), "128 frames");
    EXPECT_FALSE (page.applyButton().isVisible());

    page.chooseBlock (256);
    EXPECT_TRUE (page.getLocalBounds().contains (page.applyButton().getBounds()));
    expectPainted (a3apptest::snapshotAt (page, "audio-block-confirm", page.getWidth(), page.getHeight()));
    page.applyButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    ASSERT_EQ (view.timings.size(), 1u);
    EXPECT_EQ (view.timings[0], (a3app::AudioTiming { 256 }));
    EXPECT_FALSE (page.applyButton().isVisible());
}

// After a change the page says what runs (block, the measured round trip, DSP load and xruns), and REVERT goes back
// to the previous block at once.
TEST_F (Pages, PipeWireSaysWhatRunsAndReverts)
{
    FakeView view;
    view.state.blockSize = 64;
    view.state.dspLoadPercent = 41.0f;
    view.state.xruns = 3;
    view.state.timing.available = true;
    view.state.timing.applied = { 64 };
    view.state.timing.previous = a3app::AudioTiming { 128 };
    a3app::AudioPage page (view, manager);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    page.refresh();
    EXPECT_EQ (page.quantumLabel().getText(), juce::String ("Block 64 frames"));
    EXPECT_TRUE (page.loadLabel().getText().contains ("41")) << page.loadLabel().getText();
    EXPECT_TRUE (page.loadLabel().getText().contains ("xruns 3")) << page.loadLabel().getText();
    EXPECT_EQ (page.blockChooser().getText(), "64 frames");
    ASSERT_TRUE (page.revertButton().isEnabled());
    page.revertButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    ASSERT_EQ (view.timings.size(), 1u);
    EXPECT_EQ (view.timings[0], (a3app::AudioTiming { 128 }));

    view.state.timing.previous.reset();
    page.refresh();
    EXPECT_FALSE (page.revertButton().isEnabled());
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
        for (const auto* info : { &page.rateLabel(), &page.quantumLabel(), &page.rateSourceLabel(), &page.bitDepthLabel(),
                                  &page.roundTripLabel(), &page.analogChainLabel() })
        {
            EXPECT_TRUE (info->isVisible()) << device;
            EXPECT_GE (info->getY(), settingsBottom) << info->getText() << (device ? " (device)" : " (PipeWire)");
        }
        EXPECT_LE (page.analogChainLabel().getBottom(), page.clockPanel().getY());
    }
}

// MEASURE and MEASURE ANALOG CHAIN stay on the page; without their pair set on the I/O page they are off and say so.
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
    EXPECT_FALSE (page.measureHintLabel().isVisible());
    EXPECT_FALSE (page.analogButton().isEnabled());
    EXPECT_EQ (page.analogHintLabel().getText(), "set the analog test pair on the I/O page");

    view.cfg.io.roundTripLoop.reset();
    view.runningCfg.io.roundTripLoop.reset();
    view.state.roundTripMeasured = {};
    view.state.roundTripMeasured.state = a3app::RoundTripReport::State::noLoop;
    page.refresh();
    EXPECT_FALSE (page.measureButton().isEnabled());
    EXPECT_TRUE (page.measureHintLabel().isVisible());
    EXPECT_EQ (page.measureHintLabel().getText(), "set the digital loop on the I/O page");
    EXPECT_TRUE (page.getLocalBounds().contains (page.measureHintLabel().getBounds()));
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
    EXPECT_EQ (audio.bitDepthLabel().getText().indexOf ("set by the device"), -1);   // 32 is JACK's value
    EXPECT_NE (audio.bitDepthLabel().getText().indexOf ("JACK"), -1);

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
        a3app::FilterPage page (view);
        expectPainted (snapshot (page, "filter" + suffix));
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
    EXPECT_NE (page.loadLabel().getText().indexOf ("12.5"), -1);
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
    a3app::FilterPage page (view);
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
    EXPECT_NE (page.modeLine().indexOf ("NO ENGINE RUNNING"), -1);
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
    EXPECT_TRUE (page.messageLabel().getText().contains ("Save")) << page.messageLabel().getText();
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
// KEYS, OSC MONITOR, Tooltips and Save in one footer at the bottom; nothing of one band in another.
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
        if (name == "OSC MONITOR")
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
    auto* tooltips = toggleLabelled (content, "Tooltips");
    ASSERT_NE (tooltips, nullptr);
    EXPECT_TRUE (bands.footer.contains (areaIn (content, *tooltips)));

    for (const auto& name : window.pageList())
    {
        if (name == "OSC MONITOR")
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
        auto* page = dynamic_cast<a3app::SpeakersPage*> (shownPage (window));
        ASSERT_NE (page, nullptr);
        auto* editor = page->editor();
        ASSERT_NE (editor, nullptr);
        EXPECT_EQ (editor->getParentComponent(), page) << "no viewport: nothing to scroll";
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
    auto* page = dynamic_cast<a3app::SpeakersPage*> (shownPage (window));
    ASSERT_NE (page, nullptr);
    // Portrait: the editor fills the width (it is the width that limits it there).
    EXPECT_GE (areaIn (*page, *page->editor()).getWidth(), page->getWidth() * 9 / 10);
}

// The pages' long help is a tooltip on the title (and the controls it explains), never a label taking space; except
// CALIBRATION, whose page has the room (CalibrationHelpIsTextInTheFreeSpace).
TEST_F (Pages, HelpIsATooltipNotALabel)
{
    SpeakerRig rig;
    a3app::SpeakersPage speakers (rig.view, rig.userRoot);
    a3app::ZonesPage zones (rig.view, { rig.installed.root() });
    a3app::ChannelsPage channels (rig.view);
    a3app::FilterPage filter (rig.view);
    const std::vector<std::pair<juce::Component*, juce::String>> pages {
        { &speakers, "SPEAKERS" }, { &zones, "ZONES" }, { &channels, "CHANNELS" }, { &filter, "FILTER" } };
    for (const auto& [page, title] : pages)
    {
        page->setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
        auto* titleLabel = labelSaying (*page, title);
        ASSERT_NE (titleLabel, nullptr) << title;
        const auto help = titleLabel->getTooltip();
        EXPECT_GT (help.length(), 40) << title;
        for (auto* child : descendants (*page))
            if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->isVisible())
                EXPECT_NE (label->getText(), help) << title;
    }
}

// The Tooltips switch: off by default, a TooltipWindow only while on, remembered in the window's settings.
TEST_F (Pages, TooltipsSwitchIsOffByDefaultAndRemembered)
{
    FakeView view;
    const auto home = juce::File::createTempFile ("a3-window-home");
    const auto settings = a3app::windowSettingsFile (home);
    {
        a3app::MainWindow window (view, manager, {}, false, [] {}, { nullptr, settings });
        EXPECT_FALSE (window.tooltipsShown());
        EXPECT_FALSE (window.hasTooltipWindow());
        EXPECT_FALSE (settings.exists());   // nothing is written without a flip
        window.setTooltipsShown (true);
        EXPECT_TRUE (window.tooltipsShown());
        EXPECT_TRUE (window.hasTooltipWindow());
        EXPECT_TRUE (a3app::readTooltipSwitch (settings));
        EXPECT_FALSE (a3app::readMonitorSwitch (settings));
    }
    {
        a3app::MainWindow window (view, manager, {}, false, [] {}, { nullptr, settings });
        EXPECT_TRUE (window.tooltipsShown());   // remembered
        EXPECT_TRUE (window.hasTooltipWindow());
        window.setTooltipsShown (false);
        EXPECT_FALSE (window.hasTooltipWindow());
    }
    a3app::writeMonitorSwitch (settings, true);
    a3app::writeTooltipSwitch (settings, true);
    EXPECT_TRUE (a3app::readMonitorSwitch (settings));   // one switch's write keeps the other
    EXPECT_TRUE (a3app::readTooltipSwitch (settings));
    home.deleteRecursively();
}

// The keys say what each filter sounds like; one visible line says what the choice is for and why decks keep SVF.
TEST_F (Pages, FilterKeysDescribeTheFiltersAndALineSaysWhy)
{
    FakeView view;
    a3app::FilterPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    const auto svf = buttonsLabelled (page, juce::String::fromUTF8 ("SVF \xc2\xb7 clean, 12 dB/oct"));
    const auto ladder = buttonsLabelled (page, juce::String::fromUTF8 ("LADDER \xc2\xb7 warm, 24 dB/oct, drives"));
    ASSERT_EQ (svf.size(), 1u);
    ASSERT_EQ (ladder.size(), 1u);
    auto* why = labelSaying (page, "The DJ filter's character on the analog channels. Decks always use SVF "
                                   "(B-format: the ladder's drive would bend the 3D image).");
    ASSERT_NE (why, nullptr);
    EXPECT_TRUE (why->isVisible());
    EXPECT_GE (why->getY(), svf.front()->getBottom());   // under the keys
    EXPECT_TRUE (page.getLocalBounds().contains (why->getBounds()));
    for (auto* key : { svf.front(), ladder.front() })
        EXPECT_LE (juce::GlyphArrangement::getStringWidth (look.getTextButtonFont (*key, key->getHeight()), key->getButtonText()),
                   (float) key->getWidth()) << key->getButtonText();
    EXPECT_FLOAT_EQ (look.getTextButtonFont (*svf.front(), svf.front()->getHeight()).getHeight(),
                     look.getTextButtonFont (*ladder.front(), ladder.front()->getHeight()).getHeight());   // one size
    expectPainted (a3apptest::snapshotAt (page, "filter-described", page.getWidth(), page.getHeight()));
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
    auto* page = dynamic_cast<a3app::ZonesPage*> (shownPage (window));
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
    EXPECT_EQ (page.roundTripLabel().getText(), "Round trip (measured, digital loop)   4.4 ms (211 samples), 14:05");
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
    EXPECT_TRUE (page.roundTripLabel().getText().contains ("(estimated, no loop configured)")) << page.roundTripLabel().getText();
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
    EXPECT_EQ (page.analogChainLabel().getText(), "Analog chain   converters not measured yet");
    const auto hint = page.analogHintLabel().getText();
    EXPECT_TRUE (hint.contains ("output 6") && hint.contains ("input 8") && hint.contains ("cable")) << hint;
    EXPECT_TRUE (hint.contains ("not in use")) << hint;
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
    EXPECT_EQ (page.analogChainLabel().getText(), "Analog chain   5.4 ms (measured: digital loop now + converters measured on 2026-10-30)");
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
    EXPECT_GE (box->getRight(), row->getWidth() - 4) << "the box runs to the row's end";
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

// CALIBRATION (2026-10-30, the maintainer: the info pops up although there is room below): its help is text in the
// free space under the rows, and nothing on the page carries a tooltip.
TEST_F (Pages, CalibrationHelpIsTextInTheFreeSpace)
{
    FakeView view;
    a3app::CalibrationPage page (view);
    page.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight * 3 / 4);
    juce::Label* help = nullptr;
    for (auto* child : descendants (page))
        if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->getText().contains ("only proposes"))
            help = label;
    ASSERT_NE (help, nullptr);
    EXPECT_TRUE (help->isVisible());
    EXPECT_TRUE (page.getLocalBounds().contains (help->getBounds()));
    EXPECT_GE (help->getY(), page.messageLabel().getBottom()) << "below the rows and the message";
    for (auto* child : descendants (page))
        if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (child))
            EXPECT_TRUE (client->getTooltip().isEmpty()) << child->getName() << ": " << client->getTooltip();
    expectPainted (a3apptest::snapshotAt (page, "calibration-help-as-text", page.getWidth(), page.getHeight()));
}

// Tooltips off: nothing pops up anywhere. No tip window of ours, and the hosted AllRADecoder's own TooltipWindow has
// nothing to show (its controls' tips are the window's). On: one tip window, the window's.
TEST_F (Pages, TooltipsOffMeansNoPopupAnywhere)
{
    SpeakerRig rig;
    a3app::MainWindow window (rig.view, manager, { rig.installed.root() }, false, [] {}, {}, {}, rig.userRoot);
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    window.showPage ("SPEAKERS");
    window.refreshNow();
    auto& content = *window.getContentComponent();
    EXPECT_FALSE (window.hasTooltipWindow());
    for (auto* child : descendants (content))
    {
        EXPECT_EQ (dynamic_cast<juce::TooltipWindow*> (child), nullptr);
        EXPECT_EQ (dynamic_cast<a3app::TouchTooltips*> (child), nullptr);
    }
    auto* page = dynamic_cast<a3app::SpeakersPage*> (shownPage (window));
    ASSERT_NE (page, nullptr);
    ASSERT_NE (page->editor(), nullptr);
    int adopted = 0;
    for (auto* part : descendants (*page->editor()))
    {
        if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (part))
            EXPECT_TRUE (client->getTooltip().isEmpty()) << part->getName() << ": " << client->getTooltip();
        juce::Component* owner = nullptr;
        adopted += a3app::helpFor (part, owner).isNotEmpty() && owner == part ? 1 : 0;
    }
    EXPECT_GT (adopted, 0) << "IEM's help is kept for the window's tip";
    window.setTooltipsShown (true);
    EXPECT_TRUE (window.hasTooltipWindow());
    int ours = 0;
    for (auto* child : descendants (content))
        ours += dynamic_cast<a3app::TouchTooltips*> (child) != nullptr ? 1 : 0;
    EXPECT_EQ (ours, 1);
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
    auto* page = dynamic_cast<a3app::ZonesPage*> (shownPage (window));
    ASSERT_NE (page, nullptr);
    page->selectVariant (rig.rowOf (*page, rig.club));
    window.refreshNow();
    EXPECT_TRUE (page->actionsLabel().isVisible());
    EXPECT_TRUE (page->actionsLabel().getText().contains ("runs")) << page->actionsLabel().getText();
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
    EXPECT_TRUE (page->actionsLabel().getText().contains ("RENAME")) << page->actionsLabel().getText();
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
    auto* page = dynamic_cast<a3app::ZonesPage*> (shownPage (window));
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
        EXPECT_TRUE (window.getLocalBounds().contains (alert->getBounds())) << name;
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
    EXPECT_EQ (shot.getPixelAt (area.getX() + 2, area.getY() + area.getHeight() / 2).getARGB(), a3app::palette::surface.getARGB())
        << "the margin beside IEM's table is the page's surface";
}
