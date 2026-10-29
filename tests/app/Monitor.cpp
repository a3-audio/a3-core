// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The window's OSC monitor (replacing the Python Core's HTML page): its pages over the client of Core's
// real routes on a real HTTP server, the switch that shows them, and the bench's answers.
#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "CoreWindowRig.h"
#include "FakeView.h"
#include "Look.h"
#include "MainWindow.h"
#include "MonitorBenchPage.h"
#include "MonitorRegisterPage.h"
#include "MonitorTrafficPage.h"
#include "MonitorTruthPage.h"
#include "MonitorUnknownPage.h"
#include "PageSnapshot.h"

namespace
{
using a3apptest::CoreWindowRig;
using a3apptest::expectPainted;
using a3apptest::snapshot;

struct Monitor : ::testing::Test
{
    juce::MessageManagerLock lock;   // the test runner's dispatch thread must stay out of component code
    a3app::Look look;
    Monitor() { juce::LookAndFeel::setDefaultLookAndFeel (&look); }
    ~Monitor() override { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
};

// Everything the monitor shows, fetched once: the stream's first event, the summary, unknown and register.
void fetchEverything (CoreWindowRig& rig)
{
    rig.fill();
    rig.client->setMonitorShown (true);
    rig.client->requestUnknown();
    rig.client->requestRegister();
    rig.client->refreshNow();
}

std::vector<std::string> addressesOf (const std::vector<a3app::TrafficRow>& rows)
{
    std::vector<std::string> out;
    for (const auto& row : rows)
        out.push_back (row.direction + " " + row.address);
    std::sort (out.begin(), out.end());
    return out;
}
}

TEST_F (Monitor, EveryPagePaints)
{
    CoreWindowRig rig;
    fetchEverything (rig);
    ASSERT_TRUE (rig.client->traffic().answered);

    {
        a3app::MonitorTrafficPage page (*rig.client);
        page.refresh();
        EXPECT_EQ (page.rowsShown().size(), 3u);
        expectPainted (snapshot (page, "monitor-traffic"));
    }
    {
        a3app::MonitorTruthPage page (*rig.client);
        page.refresh();
        const auto lines = page.lines();
        EXPECT_TRUE (lines.contains ("motion: a3-osc.json is Core's")) << lines.joinIntoString (" | ");
        EXPECT_TRUE (lines.contains ("stemdeck: a3-osc.json DIFFERS from Core's (0123456789ab)")) << lines.joinIntoString (" | ");
        EXPECT_TRUE (lines.contains ("audio engine: engine")) << lines.joinIntoString (" | ");
        EXPECT_TRUE (lines.contains ("Core's a3-osc.json: " + juce::String (rig.truth.fingerprint()).substring (0, 12)))
            << lines.joinIntoString (" | ");
        expectPainted (snapshot (page, "monitor-truth"));
    }
    {
        a3app::MonitorUnknownPage page (*rig.client);
        page.refresh();
        ASSERT_EQ (page.rowsShown().size(), 1u);
        EXPECT_EQ (page.rowsShown()[0].address, "/nobody/knows");
        expectPainted (snapshot (page, "monitor-unknown"));
    }
    {
        a3app::MonitorRegisterPage page (*rig.client);
        page.refresh();
        EXPECT_EQ (page.entriesShown().size(), 3u);
        EXPECT_NE (page.noteText().indexOf ("2 of 3 seen"), -1) << page.noteText();
        page.setOnlyDead (true);
        ASSERT_EQ (page.entriesShown().size(), 1u);
        EXPECT_EQ (page.entriesShown()[0].address, "/channel/{ch}/cue");
        page.setOnlyDead (false);
        page.setDevice ("motion");
        ASSERT_EQ (page.entriesShown().size(), 1u);
        EXPECT_EQ (page.entriesShown()[0].address, "/channel/{ch}/azimuth");
        expectPainted (snapshot (page, "monitor-register"));
    }
    {
        a3app::MonitorBenchPage page (*rig.client);
        EXPECT_EQ (page.recallLabel(), juce::String (rig.truth.pattern ("state.recall")));
        expectPainted (snapshot (page, "monitor-bench"));
    }
}

// The pages that have nothing yet (no Core, or nothing fetched) still paint and say so.
TEST_F (Monitor, EveryPagePaintsWithoutCore)
{
    a3app::CoreClient nobody (a3core::loopbackAddress(), 1, "/state/recall", false);
    a3app::MonitorTrafficPage traffic (nobody);
    traffic.refresh();
    EXPECT_NE (traffic.statusText().indexOf ("no Core at"), -1) << traffic.statusText();
    expectPainted (snapshot (traffic, "monitor-traffic-none"));
    a3app::MonitorTruthPage truth (nobody);
    truth.refresh();
    expectPainted (snapshot (truth, "monitor-truth-none"));
    a3app::MonitorUnknownPage unknown (nobody);
    unknown.refresh();
    expectPainted (snapshot (unknown, "monitor-unknown-none"));
    a3app::MonitorRegisterPage reg (nobody);
    reg.refresh();
    expectPainted (snapshot (reg, "monitor-register-none"));
}

TEST_F (Monitor, SwitchHidesAndRemembers)
{
    CoreWindowRig rig;
    a3apptest::FakeView view;
    juce::AudioDeviceManager manager;
    manager.addAudioDeviceType (std::make_unique<a3apptest::NoDevices>());
    const auto home = juce::File::createTempFile ("a3-window-home");
    const auto settings = a3app::windowSettingsFile (home.getChildFile (".config/a3"));
    EXPECT_EQ (settings, home.getChildFile (".config/a3/core/window.settings"));
    const juce::StringArray monitorPages { "TRAFFIC", "TRUTH & DEVICES", "NOT UNDERSTOOD", "REGISTER", "BENCH" };

    {
        a3app::MainWindow window (view, manager, {}, false, [] {}, { rig.client.get(), settings });
        EXPECT_FALSE (window.monitorShown());   // no file: off
        EXPECT_FALSE (rig.client->monitorShown());
        auto list = window.pageList();
        EXPECT_EQ (list[list.size() - 1], "OSC MONITOR");   // the switch is at the bottom of the list
        for (const auto& name : monitorPages)
            EXPECT_FALSE (list.contains (name)) << name;
        EXPECT_TRUE (list.contains ("STATUS"));
        EXPECT_FALSE (settings.exists());   // nothing is written without a flip

        window.setMonitorShown (true);
        list = window.pageList();
        for (const auto& name : monitorPages)
            EXPECT_TRUE (list.contains (name)) << name;
        EXPECT_EQ (list[list.size() - 1], "OSC MONITOR");
        EXPECT_TRUE (rig.client->monitorShown());
        EXPECT_TRUE (settings.existsAsFile());
        EXPECT_TRUE (a3app::readMonitorSwitch (settings));
        expectPainted (snapshot (*window.getContentComponent(), "window-monitor"));
    }
    {
        settings.replaceWithText ("{ \"monitor\" :  true }\n", false, false, "\n");   // as no write of ours would lay it out
        {
            a3app::MainWindow opened (view, manager, {}, false, [] {}, { rig.client.get(), settings });
            EXPECT_TRUE (opened.monitorShown());
        }
        EXPECT_EQ (settings.loadFileAsString(), "{ \"monitor\" :  true }\n");   // opening and closing write nothing
        a3app::MainWindow window (view, manager, {}, false, [] {}, { rig.client.get(), settings });
        EXPECT_TRUE (window.monitorShown());   // remembered
        EXPECT_TRUE (window.pageList().contains ("TRAFFIC"));
        window.showPage ("BENCH");
        EXPECT_EQ (window.currentPage(), "BENCH");
        window.setMonitorShown (false);
        EXPECT_FALSE (window.pageList().contains ("BENCH"));
        EXPECT_NE (window.currentPage(), "BENCH");   // a hidden page is not left showing
        EXPECT_FALSE (rig.client->monitorShown());
        EXPECT_FALSE (a3app::readMonitorSwitch (settings));
    }
    {
        a3app::MainWindow window (view, manager, {}, false, [] {}, {});   // no Core client: no switch at all
        EXPECT_FALSE (window.pageList().contains ("OSC MONITOR"));
        EXPECT_FALSE (window.pageList().contains ("TRAFFIC"));
    }
    home.deleteRecursively();
}

// Portrait, monitor on: the engine's keys in the first row, the monitor's in a second, the switch in the footer.
TEST_F (Monitor, MonitorKeysTakeASecondRowAndTheSwitchSitsInTheFooter)
{
    CoreWindowRig rig;
    a3apptest::FakeView view;
    juce::AudioDeviceManager manager;
    manager.addAudioDeviceType (std::make_unique<a3apptest::NoDevices>());
    a3app::MainWindow window (view, manager, {}, false, [] {}, { rig.client.get(), {} });
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    auto& content = *window.getContentComponent();
    const auto oneRow = window.bands().navigation.getHeight();
    window.setMonitorShown (true);
    const auto bands = window.bands();
    EXPECT_GT (bands.navigation.getHeight(), oneRow * 3 / 2);
    EXPECT_FALSE (bands.navigation.intersects (bands.page));
    const auto keyOf = [&] (const juce::String& name) -> juce::Component*
    {
        for (auto* child : content.getChildren())
            if (auto* key = dynamic_cast<juce::TextButton*> (child); key != nullptr && key->getButtonText() == name)
                return key;
        return nullptr;
    };
    auto* status = keyOf ("STATUS");
    auto* traffic = keyOf ("TRAFFIC");
    auto* bench = keyOf ("BENCH");
    ASSERT_NE (status, nullptr);
    ASSERT_NE (traffic, nullptr);
    ASSERT_NE (bench, nullptr);
    EXPECT_GE (traffic->getY(), status->getBottom());   // the monitor's row under the engine's
    EXPECT_EQ (traffic->getY(), bench->getY());
    for (auto* key : { status, traffic, bench })
        EXPECT_TRUE (bands.navigation.contains (key->getBounds())) << key->getBounds().toString();
    juce::Component* monitorSwitch = nullptr;
    for (auto* child : content.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child); toggle != nullptr && toggle->getButtonText() == "OSC MONITOR")
            monitorSwitch = toggle;
    ASSERT_NE (monitorSwitch, nullptr);
    EXPECT_TRUE (bands.footer.contains (monitorSwitch->getBounds()));
    expectPainted (a3apptest::snapshotAt (content, "portrait-monitor", content.getWidth(), content.getHeight()));
}

TEST_F (Monitor, BenchShowsTheRefusal)
{
    CoreWindowRig rig;
    rig.client->setMonitorShown (true);
    a3app::MonitorBenchPage page (*rig.client);

    page.setTo ("mixer");
    page.setAddress ("/channel/1/cue");
    page.setValue ("1");
    page.sendNow();
    EXPECT_TRUE (page.answerIsRefusal());
    EXPECT_NE (page.answerText().indexOf ("the mixer has no address yet"), -1) << page.answerText();
    EXPECT_TRUE (rig.sent.empty());
    expectPainted (snapshot (page, "monitor-bench-refused"));

    page.setTo ("self");
    page.setAddress ("channel/1/cue");
    page.sendNow();
    EXPECT_TRUE (page.answerIsRefusal());
    EXPECT_NE (page.answerText().indexOf ("an address starts with /"), -1) << page.answerText();

    page.setAddress ("/channel/1/cue");
    page.sendNow();
    EXPECT_FALSE (page.answerIsRefusal()) << page.answerText();
    EXPECT_NE (page.answerText().indexOf ("/channel/1/cue 1"), -1) << page.answerText();
    ASSERT_EQ (rig.sent.size(), 1u);
    EXPECT_EQ (rig.sent[0].first, "self");

    page.recallNow();
    EXPECT_FALSE (page.answerIsRefusal()) << page.answerText();
    ASSERT_EQ (rig.sent.size(), 2u);
    EXPECT_EQ (rig.sent[1].first, "self");
    EXPECT_EQ (rig.sent[1].second.address, rig.truth.pattern ("state.recall"));
    ASSERT_EQ (rig.sent[1].second.args.size(), 1u);
    EXPECT_EQ (rig.sent[1].second.args[0].tag, 'i');

    a3app::CoreClient nobody (a3core::loopbackAddress(), 1, "/state/recall", false);
    a3app::MonitorBenchPage alone (nobody);
    alone.setTo ("self");
    alone.setAddress ("/channel/1/cue");
    alone.setValue ("1");
    alone.sendNow();
    EXPECT_TRUE (alone.answerIsRefusal());
    EXPECT_NE (alone.answerText().indexOf ("no Core at"), -1) << alone.answerText();
}

TEST_F (Monitor, TrafficFilters)
{
    CoreWindowRig rig;
    rig.fill();
    rig.client->setMonitorShown (true);
    rig.client->refreshNow();
    a3app::MonitorTrafficPage page (*rig.client);
    page.refresh();
    EXPECT_EQ (addressesOf (page.rowsShown()),
               (std::vector<std::string> { "in /channel/1/azimuth", "in /channel/1/volume", "out /vu/1" }));

    page.setFilter ({ "volume", false, "" });
    EXPECT_EQ (addressesOf (page.rowsShown()), (std::vector<std::string> { "in /channel/1/volume" }));

    page.setFilter ({ "", true, "" });   // the azimuth is 30 s old: not active
    EXPECT_EQ (addressesOf (page.rowsShown()), (std::vector<std::string> { "in /channel/1/volume", "out /vu/1" }));

    page.setFilter ({ "", false, "motion" });
    EXPECT_EQ (addressesOf (page.rowsShown()), (std::vector<std::string> { "in /channel/1/azimuth", "out /vu/1" }));

    page.setFilter ({ "", true, "motion" });
    EXPECT_EQ (addressesOf (page.rowsShown()), (std::vector<std::string> { "out /vu/1" }));
    expectPainted (snapshot (page, "monitor-traffic-filtered"));

    page.setFilter ({ "volume", false, "" });
    page.selectRow (0);
    const auto history = page.historyShown();
    ASSERT_EQ (history.size(), 2u);
    EXPECT_EQ (history[0].value, "0.6");   // newest first
    EXPECT_EQ (history[1].value, "0.5");
    EXPECT_EQ (history[0].address, "/channel/1/volume");

    // The stream brings only what is new: the history grows, it is not sent again.
    rig.clock.mono = rig.clock.mono + 1.0;   // later than the stream's last look
    rig.seen (a3core::Direction::in, "/channel/1/volume", 0.7, "mixer");
    for (int round = 0; round < 40 && page.rowsShown().size() == 1 && page.rowsShown()[0].count < 3; ++round)
    {
        rig.client->refreshNow (100);   // events made before the message may still be queued: read on
        page.refresh();
    }
    EXPECT_EQ (page.historyShown().size(), 3u);
    EXPECT_EQ (page.historyShown()[0].value, "0.7");
    ASSERT_EQ (page.rowsShown().size(), 1u);
    EXPECT_EQ (page.rowsShown()[0].count, 3);
}

// The client's own thread holds its round only while it works: a refresh asked on another thread (the
// window asks once before its first paint) gets its turn while the traffic stream is open. It once did
// not -- the window never appeared.
TEST (MonitorClient, RefreshIsNotStarvedByThePoller)
{
    CoreWindowRig rig;
    rig.fill();
    a3app::CoreClient polling (rig.server->host(), rig.server->port(), rig.truth.pattern ("state.recall"), true);
    polling.setMonitorShown (true);
    std::this_thread::sleep_for (std::chrono::milliseconds (300));   // the stream is open and read
    std::atomic<bool> done { false };
    std::thread asker ([&] {
        for (int i = 0; i < 3; ++i)
            polling.refreshNow (0);
        done = true;
    });
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (! done && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    EXPECT_TRUE (done) << "refreshNow waited more than 5 s for the poller's round";
    polling.setMonitorShown (false);   // lets a starved asker finish, so the test ends either way
    asker.join();
    EXPECT_TRUE (polling.traffic().answered || ! polling.monitorShown());
}

// A hidden monitor asks Core nothing: no stream, no summary. The window turns it on with the switch.
TEST (MonitorClient, HiddenMonitorAsksCoreNothing)
{
    CoreWindowRig rig;
    rig.fill();
    EXPECT_FALSE (rig.client->monitorShown());   // off from the start
    rig.client->refreshNow (300);
    EXPECT_FALSE (rig.client->traffic().answered);
    EXPECT_FALSE (rig.client->summary().answered);
    EXPECT_EQ (rig.client->trafficGeneration(), 0u);
    EXPECT_EQ (rig.client->summaryGeneration(), 0u);
    rig.client->setMonitorShown (true);
    rig.client->refreshNow();
    EXPECT_TRUE (rig.client->traffic().answered);
    EXPECT_TRUE (rig.client->summary().answered);
}

// Each cache's generation moves with news and only then; a glance carries at most the history asked for.
TEST (MonitorClient, GenerationsMoveOnlyWithNews)
{
    CoreWindowRig rig;
    rig.fill();
    for (int i = 0; i < 200; ++i)
        rig.seen (a3core::Direction::in, "/channel/3/volume", i * 0.001, "mixer");
    auto& client = *rig.client;
    client.setMonitorShown (true);
    client.refreshNow();
    const auto traffic = client.trafficGeneration(), summary = client.summaryGeneration();
    EXPECT_GT (traffic, 0u);
    EXPECT_GT (summary, 0u);
    EXPECT_EQ (client.unknownGeneration(), 0u);
    EXPECT_EQ (client.registerGeneration(), 0u);
    for (int i = 0; i < 3; ++i)   // nobody asked Core anything: nothing moves
    {
        EXPECT_EQ (client.trafficGeneration(), traffic);
        EXPECT_EQ (client.summaryGeneration(), summary);
    }
    client.requestUnknown();
    client.refreshNow (0);
    EXPECT_EQ (client.unknownGeneration(), 1u);
    EXPECT_EQ (client.registerGeneration(), 0u);
    client.requestRegister();
    client.refreshNow (0);
    EXPECT_EQ (client.registerGeneration(), 1u);
    EXPECT_EQ (client.unknownGeneration(), 1u);

    const auto all = client.glance ("", "", 120);
    EXPECT_EQ (all.history.size(), 120u);   // the ring holds 204; a page gets a page's worth
    EXPECT_EQ (all.rows.size(), 4u);
    const auto one = client.glance ("in", "/channel/1/volume", 120);
    ASSERT_EQ (one.history.size(), 2u);
    EXPECT_EQ (one.history[0].value, "0.6");
    EXPECT_EQ (one.generation, client.trafficGeneration());
}

// A page refreshed at 20 Hz copies from the client only when the client has news (Important 1, review 26).
TEST_F (Monitor, PagesCopyOnlyWhatChanged)
{
    CoreWindowRig rig;
    fetchEverything (rig);
    a3app::MonitorTrafficPage traffic (*rig.client);
    a3app::MonitorTruthPage truth (*rig.client);
    a3app::MonitorUnknownPage unknown (*rig.client);
    a3app::MonitorRegisterPage reg (*rig.client);
    const int trafficCopies = traffic.copies(), truthCopies = truth.copies(), unknownCopies = unknown.copies(), regCopies = reg.copies();
    EXPECT_EQ (trafficCopies, 1);
    EXPECT_EQ (truthCopies, 1);
    EXPECT_EQ (unknownCopies, 1);
    EXPECT_EQ (regCopies, 1);
    for (int tick = 0; tick < 5; ++tick)
    {
        traffic.refresh();
        truth.refresh();
        unknown.refresh();
        reg.refresh();
    }
    EXPECT_EQ (traffic.copies(), trafficCopies);
    EXPECT_EQ (truth.copies(), truthCopies);
    EXPECT_EQ (unknown.copies(), unknownCopies);
    EXPECT_EQ (reg.copies(), regCopies);

    traffic.selectRow (0);   // a pick asks for that row's history: one glance
    EXPECT_EQ (traffic.copies(), trafficCopies + 1);
    rig.client->refreshNow();   // news
    traffic.refresh();
    truth.refresh();
    EXPECT_EQ (traffic.copies(), trafficCopies + 2);
    EXPECT_EQ (truth.copies(), truthCopies + 1);
}

// Keys, toggles and the quit dialog in the window's look; text a share of the key, larger than JUCE's.
TEST_F (Monitor, LookCoversKeysTogglesAndTheDialog)
{
    juce::TextButton key ("STATUS");
    juce::LookAndFeel_V4 plain;
    for (const int height : { 60, 120 })   // a tall key (the page list): the share, past JUCE's 16 px cap
    {
        const auto ours = look.getTextButtonFont (key, height).getHeight();
        EXPECT_NEAR (ours, height * a3app::keyTextShare, 0.5f) << height;
        EXPECT_GT (ours, plain.getTextButtonFont (key, height).getHeight()) << height;
    }
    // A short key (the Save bar) keeps at least JUCE's size: the share alone would shrink it.
    EXPECT_GE (look.getTextButtonFont (key, 30).getHeight(), plain.getTextButtonFont (key, 30).getHeight());
    EXPECT_EQ (look.findColour (juce::AlertWindow::backgroundColourId), a3app::palette::raised);
    EXPECT_EQ (look.findColour (juce::AlertWindow::textColourId), a3app::palette::text);

    juce::ToggleButton toggle ("OSC MONITOR");
    toggle.setSize (240, 60);
    toggle.setLookAndFeel (&look);
    const auto image = toggle.createComponentSnapshot (toggle.getLocalBounds(), false, 1.0f);
    // The text reaches well into the toggle's height (JUCE's own stops at 15 px).
    int top = image.getHeight(), bottom = -1;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = image.getHeight(); x < image.getWidth(); ++x)
            if (image.getPixelAt (x, y).getBrightness() > 0.5f)
            {
                top = juce::jmin (top, y);
                bottom = juce::jmax (bottom, y);
            }
    toggle.setLookAndFeel (nullptr);
    EXPECT_GT (bottom - top, 60 / 4) << top << ".." << bottom;

    // In a narrow list the whole word is drawn, smaller, never cut to "OSC M...".
    EXPECT_FLOAT_EQ (a3app::fittedTextHeight ("OSC MONITOR", 20.0f, 1000.0f), 20.0f);
    const auto narrow = a3app::fittedTextHeight ("OSC MONITOR", 30.0f, 100.0f);
    EXPECT_LT (narrow, 30.0f);
    EXPECT_LE (juce::GlyphArrangement::getStringWidth (juce::FontOptions (narrow), "OSC MONITOR"), 100.0f);
    EXPECT_GT (juce::GlyphArrangement::getStringWidth (juce::FontOptions (narrow), "OSC MONITOR"), 85.0f);

    // A narrow key (a row of page keys on the portrait screen): its text fits inside the key, whole.
    juce::TextButton narrowKey ("CALIBRATION");
    narrowKey.setSize (90, 57);
    const auto fitted = look.getTextButtonFont (narrowKey, narrowKey.getHeight()).getHeight();
    EXPECT_LT (fitted, 57 * a3app::keyTextShare);
    EXPECT_LE (juce::GlyphArrangement::getStringWidth (juce::FontOptions (fitted), "CALIBRATION"), 90.0f * a3app::keyTextWidthShare + 0.5f);
}
