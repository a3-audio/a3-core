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

TEST_F (Pages, ChoosingTheBackendEditsTheConfig)
{
    FakeView view;
    a3app::AudioPage page (view, manager);
    page.chooseBackend (a3app::BackendSettings::Type::device);
    EXPECT_EQ (view.edited, 1);
    EXPECT_EQ (view.applied, 0);   // an edit, not a rebuild: Save applies
    EXPECT_EQ (view.cfg.backend.type, a3app::BackendSettings::Type::device);
    EXPECT_TRUE (page.showsDeviceSelector());
    page.chooseBackend (a3app::BackendSettings::Type::device);   // no change, no second edit
    EXPECT_EQ (view.edited, 1);
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

// An edit is not the engine: clicking "Device" on a running JACK engine changes the AUDIO page's
// layout and says "not saved", but STATUS and the read-only values keep describing what runs.
TEST_F (Pages, PendingEditDoesNotChangeWhatRuns)
{
    FakeView view;   // running JACK
    a3app::StatusPage status (view);
    a3app::AudioPage audio (view, manager);
    EXPECT_TRUE (audio.pendingLabel().getText().isEmpty());

    audio.chooseBackend (a3app::BackendSettings::Type::device);
    status.refresh();
    audio.refresh();

    EXPECT_EQ (view.runningCfg.backend.type, a3app::BackendSettings::Type::jack);
    EXPECT_EQ (status.modeLine(), "JACK, connecting");
    EXPECT_TRUE (audio.showsDeviceSelector());   // the edit is what the page edits
    EXPECT_NE (audio.pendingLabel().getText().indexOf ("not saved"), -1);
    EXPECT_EQ (audio.bitDepthLabel().getText().indexOf ("set by the device"), -1);   // 32 is JACK's value
    EXPECT_NE (audio.bitDepthLabel().getText().indexOf ("JACK"), -1);

    audio.chooseBackend (a3app::BackendSettings::Type::jack);   // back to what runs: nothing pending
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

TEST_F (Pages, ZonesPickADirectoryAndShowAFailure)
{
    FakeView view;
    a3test::ScratchDirectory copy;
    a3app::ZonesPage page (view, { a3test::shippedSpatialDirectory, copy.root(), juce::File ("/nonexistent/spatial") });
    EXPECT_EQ (page.rowCount(), 3);
    EXPECT_EQ (page.selectedRow(), 0);
    page.choose (1);
    EXPECT_EQ (view.cfg.spatialDirectory, copy.root());
    EXPECT_EQ (view.edited, 1);
    page.choose (2);   // does not load: stays on the last good one and says why
    EXPECT_EQ (view.cfg.spatialDirectory, copy.root());
    EXPECT_NE (page.messageLabel().getText().indexOf ("/nonexistent/spatial"), -1);
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

TEST_F (Pages, ForeignLinksAreShownNotEditable)
{
    FakeView view;
    view.cfg.io.jack["analog_1"] = { "system:capture_1" };
    view.links = { { "system:capture_1", "a3-engine:analog_1" }, { "stranger:out", "a3-engine:analog_1" } };
    a3app::IoPage page (view);
    ASSERT_GT (page.rows().size(), 10u);
    const auto& row = page.rows().front();
    ASSERT_EQ (row.port, "analog_1");
    EXPECT_EQ (row.foreign, (std::vector<std::string> { "stranger:out" }));
    EXPECT_EQ (page.editableText ("analog_1"), "system:capture_1");   // the foreign one is not in the editor
    EXPECT_NE (page.rowText (0).indexOf ("stranger:out"), -1);        // but it is on the row

    page.setPeers ("analog_1", "system:capture_1, system:capture_2");   // edit: add one
    EXPECT_EQ (view.cfg.io.jack["analog_1"], (std::vector<std::string> { "system:capture_1", "system:capture_2" }));
    page.setPeers ("analog_1", "");
    EXPECT_EQ (view.cfg.io.jack.count ("analog_1"), 0u);   // empty means no entry; the foreign link stays the other client's
    EXPECT_EQ (page.rows().front().foreign, (std::vector<std::string> { "system:capture_1", "stranger:out" }));   // live order
}

TEST_F (Pages, IoEditsDeviceChannelsAndDirectLinks)
{
    FakeView view;
    view.cfg.backend.type = a3app::BackendSettings::Type::device;
    a3app::IoPage page (view);
    page.setDeviceChannel ("analog_2", 5);
    EXPECT_EQ (view.cfg.io.deviceChannels["analog_2"], 5);
    page.setDeviceChannel ("analog_2", -1);   // unmapped
    EXPECT_EQ (view.cfg.io.deviceChannels.count ("analog_2"), 0u);

    page.setDirectLinks ("system:capture_9 -> beat-analyzer:in_1\n  \nsystem:capture_10 -> beat-analyzer:in_2");
    EXPECT_EQ (view.cfg.io.directLinks.size(), 2u);
    EXPECT_EQ (view.cfg.io.directLinks[1].second, "beat-analyzer:in_2");
    const auto edits = view.edited;
    page.setDirectLinks ("a -> b\nnot a link");   // refused as a whole
    EXPECT_EQ (view.edited, edits);
    EXPECT_EQ (view.cfg.io.directLinks.size(), 2u);
    EXPECT_NE (page.messageLabel().getText().indexOf ("not a link"), -1);
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

TEST_F (Pages, ZonesSummariseWhatEachZonePlays)
{
    FakeView view;
    a3app::ZonesPage page (view, { a3test::shippedSpatialDirectory });
    for (const auto zone : { a3::SpeakerZone::main, a3::SpeakerZone::booth })
    {
        const auto text = page.summaryText (zone);
        for (const auto* part : { "AllRADecoder v0.9.5", "28 Feb 2026", "order 3", "inPhase", "sub ch 1", "150 Hz", "ch 2 az 45 el 0",
                                  "ch 5 az 135 el 0", "imaginary az 0 el 90" })
            EXPECT_TRUE (text.contains (part)) << a3::zoneName (zone) << ": " << part << " in " << text;
    }
    EXPECT_TRUE (page.summaryText (a3::SpeakerZone::main).startsWith ("MAIN"));
    EXPECT_TRUE (page.summaryText (a3::SpeakerZone::booth).startsWith ("BOOTH"));
    expectPainted (snapshot (page, "zones-summary"));

    a3test::ScratchDirectory broken;
    broken.root().getChildFile ("booth.decoder.json").replaceWithText ("{");
    view.runningCfg.spatialDirectory = broken.root();
    page.refresh();
    EXPECT_TRUE (page.summaryText (a3::SpeakerZone::booth).contains ("booth.decoder.json")) << page.summaryText (a3::SpeakerZone::booth);
    EXPECT_TRUE (page.summaryText (a3::SpeakerZone::main).contains ("order 3"));
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
    EXPECT_EQ (rig.view.cfg.spatialDirectory, variant);
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
    EXPECT_NE (rig.view.cfg.spatialDirectory, first);
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

// The pages' long help is a tooltip on the title (and the controls it explains), never a label taking space.
TEST_F (Pages, HelpIsATooltipNotALabel)
{
    SpeakerRig rig;
    a3app::SpeakersPage speakers (rig.view, rig.userRoot);
    a3app::ZonesPage zones (rig.view, { rig.installed.root() });
    a3app::ChannelsPage channels (rig.view);
    a3app::CalibrationPage calibration (rig.view);
    a3app::FilterPage filter (rig.view);
    const std::vector<std::pair<juce::Component*, juce::String>> pages {
        { &speakers, "SPEAKERS" }, { &zones, "ZONES" }, { &channels, "CHANNELS" }, { &calibration, "CALIBRATION" }, { &filter, "FILTER" } };
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
