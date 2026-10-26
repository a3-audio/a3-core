// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdlib>
#include <gtest/gtest.h>
#include <set>
#include "EngineFixture.h"
#include "AudioPage.h"
#include "Look.h"
#include "MainWindow.h"
#include "StatusPage.h"

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
    juce::Result apply (const a3app::AppConfig& c) override { cfg = c; ++applied; return juce::Result::ok(); }

    void runDevice() { cfg.backend.type = runningCfg.backend.type = a3app::BackendSettings::Type::device; }
    a3app::AppConfig cfg, runningCfg;   // the pending edit and what runs
    a3app::BackendStatus state;
    std::vector<a3::Engine::OutputMeter> meterValues;
    std::vector<std::string> names;
    bool heard = true, open = true;
    double sinceCore = 0.4;
    int applied = 0;
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

TEST_F (Pages, ChoosingTheBackendAppliesTheEditedConfig)
{
    FakeView view;
    a3app::AudioPage page (view, manager);
    page.chooseBackend (a3app::BackendSettings::Type::device);
    EXPECT_EQ (view.applied, 1);
    EXPECT_EQ (view.cfg.backend.type, a3app::BackendSettings::Type::device);
    EXPECT_TRUE (page.showsDeviceSelector());
    page.chooseBackend (a3app::BackendSettings::Type::device);   // no change, no second apply
    EXPECT_EQ (view.applied, 1);
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
        a3app::MainWindow window (view, manager, true, [&] { ++quits; });
        window.setVisible (true);   // shown on the desktop only by open(); the flag is what the test reads
        EXPECT_TRUE (window.isVisible());
        window.closeButtonPressed();
        EXPECT_EQ (quits, 0);
        EXPECT_FALSE (window.isVisible());
    }
    a3app::MainWindow window (view, manager, false, [&] { ++quits; });
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
