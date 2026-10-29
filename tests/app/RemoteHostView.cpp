// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <gtest/gtest.h>
#include "RemoteRig.h"

using a3apptest::RemoteRig;

TEST (RemoteHostView, ReadsTheCacheNotTheNetwork)
{
    RemoteRig rig;
    rig.fake.state.xruns = 7;
    rig.fake.links = { { "system:capture_1", "a3-engine:analog_1" } };
    rig.tick();
    rig.tick();
    rig.remote->refreshNow();
    EXPECT_EQ (rig.remote->status().xruns, 7);
    EXPECT_EQ (rig.remote->outputNames().size(), 6u);
    EXPECT_EQ (rig.remote->meters().size(), 6u);
    EXPECT_EQ (rig.remote->config().engine.channels, 4);
    EXPECT_TRUE (a3app::sameConfig (rig.remote->running(), rig.fake.runningCfg));
    EXPECT_EQ (rig.remote->configFile(), rig.fake.file);
    EXPECT_EQ (rig.remote->liveLinks().size(), 1u);
    EXPECT_TRUE (rig.remote->engineProblem().isEmpty());

    // Nothing answers any more: the port accepts and stays silent, so one network read would wait out its 500 ms.
    const auto port = rig.server->port();
    rig.server.reset();
    a3apptest::StalledPort stalled;
    ASSERT_TRUE (stalled.open (port));
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i)
    {
        (void) rig.remote->status();
        (void) rig.remote->meters();
        (void) rig.remote->config();
        (void) rig.remote->liveLinks();
        (void) rig.remote->engineProblem();
    }
    EXPECT_LT (std::chrono::steady_clock::now() - start, std::chrono::milliseconds (100));
    EXPECT_EQ (rig.remote->status().xruns, 7);
}

TEST (RemoteHostView, NoEngineIsSaid)
{
    int port = 0;
    std::unique_ptr<a3app::RemoteHostView> wasConnected;   // one that has seen the engine
    {
        RemoteRig rig;
        port = rig.server->port();
        rig.tick();
        rig.remote->refreshNow();
        EXPECT_TRUE (rig.remote->engineProblem().isEmpty());
        EXPECT_EQ (rig.remote->status().blockSize, 128);
        wasConnected = std::make_unique<a3app::RemoteHostView> ("127.0.0.1", port, false);
        wasConnected->refreshNow();
        EXPECT_TRUE (wasConnected->engineProblem().isEmpty());
    }   // the server is gone

    wasConnected->refreshNow();
    EXPECT_EQ (wasConnected->engineProblem(), "no engine at http://127.0.0.1:" + juce::String (port));
    EXPECT_EQ (wasConnected->status().blockSize, 0);

    a3app::RemoteHostView remote ("127.0.0.1", port, false);
    const juce::String said = "no engine at http://127.0.0.1:" + juce::String (port);
    EXPECT_EQ (remote.engineProblem(), said);   // before the first poll too
    remote.refreshNow();
    EXPECT_EQ (remote.engineProblem(), said);
    EXPECT_EQ (remote.status().blockSize, 0);   // nothing runs, as the local view says
    EXPECT_FALSE (remote.coreHeard());
    EXPECT_TRUE (remote.meters().empty());
}

TEST (RemoteHostView, TheEngineGoingAwayIsSaidAndComingBackIsHeard)
{
    RemoteRig rig;
    rig.tick();
    rig.remote->refreshNow();
    EXPECT_TRUE (rig.remote->engineProblem().isEmpty());
    const auto port = rig.server->port();
    rig.server.reset();
    rig.remote->refreshNow();
    EXPECT_NE (rig.remote->engineProblem().indexOf ("no engine at"), -1);

    rig.server = std::make_unique<a3core::HttpServer> ("127.0.0.1:" + std::to_string (port), rig.api.routes());
    ASSERT_TRUE (rig.server->start ([] (const std::string&, bool) {}));
    rig.remote->refreshNow();
    EXPECT_TRUE (rig.remote->engineProblem().isEmpty());
}

TEST (RemoteHostView, AnEngineProblemIsPassedOn)
{
    RemoteRig rig;
    rig.fake.problem = "NO ENGINE RUNNING: the device is gone";
    rig.tick();
    rig.remote->refreshNow();
    EXPECT_EQ (rig.remote->engineProblem(), rig.fake.problem);
}

TEST (RemoteHostView, ApplyIsSynchronousAndASaidRefusalComesBack)
{
    RemoteRig rig;
    rig.tick();
    rig.remote->refreshNow();
    juce::String error;
    const auto six = a3app::withChannels (rig.remote->config(), 6, error);
    ASSERT_TRUE (six) << error;

    rig.fake.applyResult = juce::Result::fail ("cannot open the device");
    auto refused = rig.settle ([&] { return rig.remote->apply (*six); });
    EXPECT_TRUE (refused.failed());
    EXPECT_EQ (refused.getErrorMessage(), "cannot open the device");
    EXPECT_EQ (rig.remote->running().engine.channels, 4);

    rig.fake.applyResult = juce::Result::ok();
    auto done = rig.settle ([&] { return rig.remote->apply (*six); });
    EXPECT_TRUE (done.wasOk());
    EXPECT_EQ (rig.fake.runningCfg.engine.channels, 6);
    EXPECT_EQ (rig.remote->running().engine.channels, 6);
}

TEST (RemoteHostView, AnEditIsKeptAgainstTheOlderPollAndReachesTheEngine)
{
    RemoteRig rig;
    rig.tick();
    rig.remote->refreshNow();
    juce::String error;
    const auto six = a3app::withChannels (rig.remote->config(), 6, error);
    ASSERT_TRUE (six) << error;
    rig.remote->edit (*six);
    EXPECT_EQ (rig.remote->config().engine.channels, 6);   // at once, before anything is posted
    EXPECT_EQ (rig.fake.edited, 0);
    rig.sync();
    EXPECT_EQ (rig.fake.cfg.engine.channels, 6);
    EXPECT_EQ (rig.remote->config().engine.channels, 6);
    EXPECT_EQ (rig.remote->running().engine.channels, 4);

    rig.remote->setFilterKind (a3::FilterKind::ladder);
    rig.sync();
    EXPECT_EQ (rig.fake.filterSets, 1);
    EXPECT_EQ (rig.remote->config().engine.filterKind, a3::FilterKind::ladder);
}

TEST (RemoteHostView, ThePollingThreadFillsTheCache)
{
    RemoteRig rig;
    rig.fake.state.xruns = 4;
    rig.tick();
    a3app::RemoteHostView threaded (rig.server->host(), rig.server->port());
    for (int i = 0; i < 200 && threaded.status().xruns != 4; ++i)
        rig.settle ([] { std::this_thread::sleep_for (std::chrono::milliseconds (10)); return 0; });
    EXPECT_EQ (threaded.status().xruns, 4);
    EXPECT_TRUE (threaded.engineProblem().isEmpty());
}

TEST (RemoteHostView, AnApplyNobodyAnswersHasItsOwnMessage)
{
    a3apptest::StalledPort stalled;
    ASSERT_TRUE (stalled.open());
    a3app::RemoteHostView remote (a3core::loopbackAddress(), stalled.port(), false);
    remote.setApplyTimeoutMillis (300);
    const auto result = remote.apply (a3app::AppConfig {});
    ASSERT_TRUE (result.failed());
    EXPECT_NE (result.getErrorMessage().indexOf ("did not answer"), -1);
    EXPECT_EQ (result.getErrorMessage().indexOf ("no engine at"), -1);   // it is there, it is only slow

    a3app::RemoteHostView nobody (a3core::loopbackAddress(), 1, false);   // nothing listens on port 1
    EXPECT_NE (nobody.apply (a3app::AppConfig {}).getErrorMessage().indexOf ("no engine at"), -1);
}

TEST (RemoteHostView, ARefusedEditLeavesNoPendingStateBehind)
{
    RemoteRig rig;
    rig.tick();
    rig.remote->refreshNow();
    const auto original = rig.remote->config();
    auto bad = original;
    bad.spatialDirectory = juce::File ("/nonexistent/spatial");   // the engine refuses this body (400)
    rig.remote->edit (bad);
    EXPECT_EQ (rig.remote->config().spatialDirectory, bad.spatialDirectory);   // shown at once

    rig.remote->refreshNow();   // posts it, is refused, reads the engine's state again
    EXPECT_EQ (rig.remote->config().spatialDirectory, original.spatialDirectory);
    EXPECT_EQ (rig.fake.edited, 0);
}

// The STATUS page's DROPPED rows and the truth's note reach the window over the control API.
TEST (RemoteHostView, SaysTheDroppedLinksAndTheTruthNote)
{
    RemoteRig rig;
    rig.fake.dropped = { "deckA_cue_L -> x:y", "deckB_aux_R -> p:q" };
    rig.fake.ignored = { "engine.returnAnalogPair" };
    rig.fake.note = "the truth has no engine.deck-audio: deck audio reports are off";
    rig.tick();
    rig.remote->refreshNow();
    EXPECT_EQ (rig.remote->droppedLinks(), rig.fake.dropped);
    EXPECT_EQ (rig.remote->ignoredKeys(), rig.fake.ignored);
    EXPECT_EQ (rig.remote->truthNote(), rig.fake.note);

    rig.fake.dropped.clear();   // a Save wrote the file without them
    rig.fake.ignored.clear();
    rig.fake.note = {};
    rig.tick();
    rig.remote->refreshNow();
    EXPECT_TRUE (rig.remote->droppedLinks().empty());
    EXPECT_TRUE (rig.remote->ignoredKeys().empty());
    EXPECT_TRUE (rig.remote->truthNote().isEmpty());
}

// A8: a config that names no deck count crosses to the window without one, and the window reads it with
// the engine's truth count, so the I/O page shows the engine's slots and a Save leaves the count out.
TEST (RemoteHostView, AnUnnamedDeckCountTakesTheEnginesTruthCount)
{
    RemoteRig rig;
    rig.fake.cfg.engine.decks = rig.fake.runningCfg.engine.decks = rig.fake.truthDeckCount;
    rig.fake.cfg.engine.decksNamed = rig.fake.runningCfg.engine.decksNamed = false;
    rig.tick();
    rig.remote->refreshNow();
    EXPECT_EQ (rig.remote->truthDecks(), rig.fake.truthDeckCount);
    EXPECT_EQ (rig.remote->config().engine.decks, rig.fake.truthDeckCount);
    EXPECT_FALSE (rig.remote->config().engine.decksNamed);
    EXPECT_EQ (rig.remote->running().engine.decks, rig.fake.truthDeckCount);
}

TEST (RemoteHostView, NoiseReachesTheEnginesZone)
{
    RemoteRig rig;
    a3app::NoiseRequest burst;
    burst.zone = a3::SpeakerZone::booth;
    burst.ambisonic = true;
    burst.azimuth = -135.0f;
    rig.remote->playNoise (burst);
    rig.sync();
    ASSERT_EQ (rig.fake.noises.size(), 1u);
    EXPECT_EQ (rig.fake.noises[0].zone, a3::SpeakerZone::booth);
    EXPECT_TRUE (rig.fake.noises[0].ambisonic);
    EXPECT_FLOAT_EQ (rig.fake.noises[0].azimuth, -135.0f);
}
