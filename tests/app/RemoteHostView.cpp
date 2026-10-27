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

    rig.server.reset();   // nothing answers any more: the getters still do, at once
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 2000; ++i)
    {
        (void) rig.remote->status();
        (void) rig.remote->meters();
        (void) rig.remote->config();
        (void) rig.remote->liveLinks();
        (void) rig.remote->engineProblem();
    }
    EXPECT_LT (std::chrono::steady_clock::now() - start, std::chrono::milliseconds (250));
    EXPECT_EQ (rig.remote->status().xruns, 7);
}

TEST (RemoteHostView, NoEngineIsSaid)
{
    int port = 0;
    {
        RemoteRig rig;
        port = rig.server->port();
        rig.tick();
        rig.remote->refreshNow();
        EXPECT_TRUE (rig.remote->engineProblem().isEmpty());
        EXPECT_EQ (rig.remote->status().blockSize, 128);
    }   // the server is gone

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
