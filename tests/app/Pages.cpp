// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdlib>
#include <gtest/gtest.h>
#include <set>
#include "EngineFixture.h"
#include "AudioPage.h"
#include "CalibrationPage.h"
#include "ChannelsPage.h"
#include "ConfigEdits.h"
#include "FilterPage.h"
#include "IoPage.h"
#include "Look.h"
#include "MainWindow.h"
#include "SaveBar.h"
#include "SpatialTestData.h"
#include "StatusPage.h"
#include "ZonesPage.h"

namespace
{
// An audio device type with no devices: the page's selector must not probe ALSA in a test.
class NoDevices final : public juce::AudioIODeviceType
{
public:
    NoDevices() : juce::AudioIODeviceType ("none") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override { return {}; }
    int getDefaultDeviceIndex (bool) const override { return -1; }
    int getIndexOfDevice (juce::AudioIODevice*, bool) const override { return -1; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override { return nullptr; }
};

struct FakeView final : a3app::HostView
{
    FakeView()
    {
        cfg.engine = a3test::engineConfig (4, 2);
        cfg.spatialDirectory = a3test::shippedSpatialDirectory;
        file = juce::File::createTempFile ("a3-pages-config");
        runningCfg = cfg;
        state.sampleRate = 44100;
        state.blockSize = 128;
        state.rateSource = "set by the JACK server";
        state.bitDepth = 32;
        state.connecting = true;
        for (int i = 0; i < 6; ++i) { meterValues.push_back ({ 0.1f * (float) i, 0.05f * (float) i }); names.push_back ("out_" + std::to_string (i)); }
    }
    a3app::BackendStatus status() const override { return state; }
    std::vector<a3::Engine::OutputMeter> meters() override { return meterValues; }
    std::vector<std::string> outputNames() const override { return names; }
    bool coreHeard() const override { return heard; }
    double secondsSinceCore() const override { return sinceCore; }
    bool gateOpen() const override { return open; }
    a3::GateLevels gateLevels() const override { a3::GateLevels g; for (auto& v : g.gain) v = open ? 1.0f : 0.0f; return g; }
    a3app::AppConfig config() const override { return cfg; }
    a3app::AppConfig running() const override { return runningCfg; }
    juce::File configFile() const override { return file; }
    a3app::LiveLinks liveLinks() const override { return links; }
    void edit (const a3app::AppConfig& c) override { cfg = c; ++edited; }
    juce::Result apply (const a3app::AppConfig& c) override
    {
        ++applied;
        fileExistedAtApply = file.existsAsFile();
        cfg = runningCfg = c;
        return applyResult;
    }
    void setFilterKind (a3::FilterKind kind) override { cfg.engine.filterKind = runningCfg.engine.filterKind = kind; ++filterSets; }
    ~FakeView() override { file.deleteFile(); }

    void runDevice() { cfg.backend.type = runningCfg.backend.type = a3app::BackendSettings::Type::device; }
    a3app::AppConfig cfg, runningCfg;   // the pending edit and what runs
    a3app::BackendStatus state;
    std::vector<a3::Engine::OutputMeter> meterValues;
    std::vector<std::string> names;
    bool heard = true, open = true;
    double sinceCore = 0.4;
    juce::File file;
    a3app::LiveLinks links;
    juce::Result applyResult = juce::Result::ok();
    bool fileExistedAtApply = false;
    int edited = 0, applied = 0, filterSets = 0;
};

// Paints a page to an image: the snapshot of one visual state. A page that cannot paint
// (a crash on first paint, an empty component) fails here, not on the rig.
juce::Image snapshot (juce::Component& page, const juce::String& name)
{
    page.setSize (900, 640);
    auto image = page.createComponentSnapshot (page.getLocalBounds(), false, 1.0f);
    if (const char* dir = std::getenv ("A3_SNAPSHOT_DIR"))
    {
        juce::File out (juce::String (dir) + "/" + name + ".png");
        juce::FileOutputStream stream (out);
        if (stream.openedOk()) { stream.setPosition (0); stream.truncate(); juce::PNGImageFormat().writeImageToStream (image, stream); }
    }
    return image;
}

int distinctColours (const juce::Image& image)
{
    std::set<juce::uint32> seen;
    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            seen.insert (image.getPixelAt (x, y).getARGB());
    return (int) seen.size();
}

void expectPainted (const juce::Image& image)
{
    ASSERT_TRUE (image.isValid());
    EXPECT_GT (image.getWidth(), 0);
    EXPECT_GT (distinctColours (image), 4);   // text and faces over the ground, not a flat fill
}

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

TEST_F (Pages, EveryPagePaints)
{
    FakeView view;
    view.cfg.engine.trims.booth.reset();   // a zone that has no trim yet
    view.runningCfg = view.cfg;
    view.links = { { "system:capture_1", "a3-engine:analog_1" }, { "stranger:out", "a3-engine:analog_1" } };
    a3test::ScratchDirectory copy;
    {
        a3app::ChannelsPage page (view);
        expectPainted (snapshot (page, "channels"));
    }
    {
        a3app::ZonesPage page (view, { a3test::shippedSpatialDirectory, a3test::reaperBoothSpatialDirectory, copy.root() });
        expectPainted (snapshot (page, "zones"));
    }
    {
        a3app::IoPage page (view);
        expectPainted (snapshot (page, "io-jack"));
        view.cfg.backend.type = a3app::BackendSettings::Type::device;
        view.cfg.io.deviceChannels["analog_1"] = 0;
        page.refresh (true);
        expectPainted (snapshot (page, "io-device"));
        view.cfg.backend.type = a3app::BackendSettings::Type::jack;
    }
    {
        a3app::CalibrationPage page (view);
        expectPainted (snapshot (page, "calibration"));
    }
    {
        a3app::FilterPage page (view);
        expectPainted (snapshot (page, "filter"));
    }
    {
        view.cfg.engine.channels = 5;   // a pending edit: the bar says so
        a3app::SaveBar bar (view);
        bar.refresh();
        expectPainted (snapshot (bar, "savebar"));
    }
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
    const auto loaded = a3app::loadAppConfig (view.file, error);
    ASSERT_TRUE (loaded) << error;
    EXPECT_EQ (loaded->engine.channels, 6);
    EXPECT_TRUE (a3app::sameConfig (view.cfg, view.runningCfg));
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
