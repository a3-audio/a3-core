// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <limits>
#include "AppTruth.h"
#include "RemoteRig.h"

namespace
{
juce::var parse (const std::string& text)
{
    juce::var out;
    juce::JSON::parse (juce::String (text), out);
    return out;
}

std::string configBody (const a3app::AppConfig& config) { return juce::JSON::toString (a3app::toJson (config), true).toStdString(); }
}

using a3apptest::RemoteRig;

TEST (EngineControlApi, StatusAsTheHostViewSaysIt)
{
    RemoteRig rig;
    rig.fake.state.dspLoadPercent = 12.5f;
    rig.fake.state.xruns = 3;
    rig.fake.state.missingPeers = { "system:capture_9" };
    rig.fake.heard = true;
    rig.fake.sinceCore = 1.25;
    rig.fake.open = false;
    rig.fake.problem = "NO ENGINE RUNNING: the device is gone";
    rig.tick();

    const auto reply = httpclient::get (rig.server->port(), "/api/engine/status");
    ASSERT_EQ (reply.status, 200);
    const auto report = a3app::statusFromJson (parse (reply.body));
    ASSERT_TRUE (report);
    EXPECT_EQ (report->backend.sampleRate, 44100);
    EXPECT_EQ (report->backend.blockSize, 128);
    EXPECT_EQ (report->backend.rateSource, "set by the JACK server");
    EXPECT_FLOAT_EQ (report->backend.dspLoadPercent, 12.5f);
    EXPECT_EQ (report->backend.xruns, 3);
    EXPECT_EQ (report->backend.bitDepth, 32);
    EXPECT_TRUE (report->backend.connecting);
    ASSERT_EQ (report->backend.missingPeers.size(), 1u);
    EXPECT_EQ (report->backend.missingPeers[0], "system:capture_9");
    EXPECT_TRUE (report->coreHeard);
    EXPECT_DOUBLE_EQ (report->secondsSinceCore, 1.25);
    EXPECT_FALSE (report->gateOpen);
    EXPECT_EQ (report->gateLevels.gain[0], 0.0f);
    EXPECT_EQ (report->engineProblem, rig.fake.problem);
}

// P47: a retired config key the loaded file named is listed beside the dropped links; an engine
// that sends no such list (older) has none.
TEST (EngineControlApi, TheIgnoredRetiredKeysAreServed)
{
    RemoteRig rig;
    rig.fake.ignored = { "engine.returnAnalogPair" };
    rig.tick();
    const auto reply = httpclient::get (rig.server->port(), "/api/engine/status");
    ASSERT_EQ (reply.status, 200);
    const auto report = a3app::statusFromJson (parse (reply.body));
    ASSERT_TRUE (report);
    EXPECT_EQ (report->ignoredKeys, (std::vector<std::string> { "engine.returnAnalogPair" }));

    auto older = parse (reply.body);
    older.getDynamicObject()->removeProperty ("ignoredKeys");
    const auto fromOlder = a3app::statusFromJson (older);
    ASSERT_TRUE (fromOlder);
    EXPECT_TRUE (fromOlder->ignoredKeys.empty());
}

TEST (EngineControlApi, NothingIsServedBeforeTheFirstTick)
{
    a3apptest::FakeView fake;
    a3app::EngineControlApi api (fake);
    auto server = a3app::serveEngineControl (api, a3apptest::truthOnAnyFreePort(), [] (const std::string&, bool) {});
    ASSERT_TRUE (server);
    EXPECT_EQ (httpclient::get (server->port(), "/api/engine/status").status, 503);
    EXPECT_EQ (httpclient::get (server->port(), "/api/engine/nothing").status, 404);
    api.close();
}

TEST (EngineControlApi, MetersAreTakenOncePerRequest)
{
    RemoteRig rig;
    rig.fake.meterValues = { { 0.9f, 0.2f }, { 0.1f, 0.05f } };
    rig.fake.names = { "main_L", "main_R" };
    rig.tick();
    rig.fake.meterValues = { { 0.3f, 0.25f }, { 0.4f, 0.06f } };   // lower than the first peak of channel 0
    rig.tick();

    auto first = a3app::metersFromJson (parse (httpclient::get (rig.server->port(), "/api/engine/meters").body));
    ASSERT_TRUE (first);
    EXPECT_EQ (first->outputNames, (std::vector<std::string> { "main_L", "main_R" }));
    EXPECT_FLOAT_EQ (first->meters[0].peak, 0.9f);   // the peak of both ticks, not of the last
    EXPECT_FLOAT_EQ (first->meters[0].rms, 0.25f);
    EXPECT_FLOAT_EQ (first->meters[1].peak, 0.4f);

    auto second = a3app::metersFromJson (parse (httpclient::get (rig.server->port(), "/api/engine/meters").body));
    ASSERT_TRUE (second);
    EXPECT_FLOAT_EQ (second->meters[0].peak, 0.0f);   // taken: nothing since
    rig.tick();
    auto third = a3app::metersFromJson (parse (httpclient::get (rig.server->port(), "/api/engine/meters").body));
    EXPECT_FLOAT_EQ (third->meters[0].peak, 0.3f);
}

TEST (EngineControlApi, ConfigAndLinksAreServed)
{
    RemoteRig rig;
    rig.fake.links = { { "system:capture_1", "a3-engine:analog_1" } };
    rig.fake.cfg.engine.channels = 5;   // pending differs from what runs
    rig.tick();

    const auto config = parse (httpclient::get (rig.server->port(), "/api/engine/config").body);
    juce::String error;
    const auto pending = a3app::parseAppConfig (config["pending"], rig.fake.file.getParentDirectory(), error, a3apptest::truthDecks());
    const auto running = a3app::parseAppConfig (config["running"], rig.fake.file.getParentDirectory(), error, a3apptest::truthDecks());
    ASSERT_TRUE (pending && running) << error;
    EXPECT_EQ (pending->engine.channels, 5);
    EXPECT_EQ (running->engine.channels, 4);
    EXPECT_EQ (config["file"].toString(), rig.fake.file.getFullPathName());
    const auto links = a3app::linksFromJson (parse (httpclient::get (rig.server->port(), "/api/engine/links").body));
    ASSERT_TRUE (links);
    ASSERT_EQ (links->size(), 1u);
    EXPECT_EQ ((*links)[0].second, "a3-engine:analog_1");
}

TEST (EngineControlApi, ApplyRoundTrip)
{
    RemoteRig rig;
    juce::String error;
    const auto six = a3app::withChannels (rig.fake.cfg, 6, error);
    ASSERT_TRUE (six) << error;
    const auto reply = rig.settle ([&] { return httpclient::post (rig.server->port(), "/api/engine/apply", configBody (*six)); });
    EXPECT_EQ (reply.status, 200);
    EXPECT_EQ (rig.fake.applied, 1);
    EXPECT_EQ (rig.fake.runningCfg.engine.channels, 6);
}

TEST (EngineControlApi, EditAndFilterKindWaitForTheControlThread)
{
    RemoteRig rig;
    juce::String error;
    const auto six = a3app::withChannels (rig.fake.cfg, 6, error);
    ASSERT_TRUE (six) << error;
    EXPECT_EQ (httpclient::post (rig.server->port(), "/api/engine/edit", configBody (*six)).status, 200);
    EXPECT_EQ (httpclient::post (rig.server->port(), "/api/engine/filter-kind", "{\"kind\":\"ladder\"}").status, 200);
    EXPECT_EQ (rig.fake.edited, 0);       // the HTTP thread touched nothing
    EXPECT_EQ (rig.fake.filterSets, 0);
    rig.tick();
    EXPECT_EQ (rig.fake.edited, 1);
    EXPECT_EQ (rig.fake.cfg.engine.channels, 6);
    EXPECT_EQ (rig.fake.runningCfg.engine.channels, 4);   // an edit is only pending
    EXPECT_EQ (rig.fake.filterSets, 1);
    EXPECT_EQ (rig.fake.cfg.engine.filterKind, a3::FilterKind::ladder);

    EXPECT_EQ (httpclient::post (rig.server->port(), "/api/engine/filter-kind", "{\"kind\":\"moog\"}").status, 400);
    EXPECT_EQ (httpclient::post (rig.server->port(), "/api/engine/edit", "not json").status, 400);
    EXPECT_EQ (httpclient::post (rig.server->port(), "/api/engine/edit", "{\"engine\":{\"channels\":0}}").status, 400);
}

TEST (EngineControlApi, RefusedConfigIs409)
{
    RemoteRig rig;
    rig.fake.applyResult = juce::Result::fail ("cannot open the device");
    const auto reply = rig.settle ([&] { return httpclient::post (rig.server->port(), "/api/engine/apply", configBody (rig.fake.cfg)); });
    EXPECT_EQ (reply.status, 409);
    EXPECT_EQ (parse (reply.body)["problem"].toString(), "cannot open the device");
    EXPECT_EQ (rig.fake.applied, 1);
}

TEST (EngineControlApi, LoopbackOnly)
{
    auto truth = a3apptest::engineTruth();
    std::string why;
    EXPECT_EQ (a3app::controlBind (truth, why), std::optional<std::string> ("127.0.0.1:9005"));

    truth.controlHost = "0.0.0.0";   // the truth's wildcard: every interface
    EXPECT_FALSE (a3app::controlBind (truth, why));
    EXPECT_NE (why.find ("loopback"), std::string::npos);
    a3apptest::FakeView fake;
    a3app::EngineControlApi api (fake);
    std::vector<std::string> said;
    EXPECT_EQ (a3app::serveEngineControl (api, truth, [&] (const std::string& line, bool) { said.push_back (line); }), nullptr);
    EXPECT_EQ (said.size(), 1u);

    truth.controlHost = "192.168.8.10";   // a LAN address is no better
    EXPECT_FALSE (a3app::controlBind (truth, why));

    RemoteRig rig;   // the shipped host: bound on loopback and nowhere else
    EXPECT_EQ (rig.server->host(), a3core::loopbackAddress());
}

TEST (EngineControlApi, ABusyPortIsSaidAndTheEngineRunsOn)
{
    RemoteRig rig;
    auto truth = a3apptest::engineTruth();
    truth.controlPort = rig.server->port();
    a3app::EngineControlApi second (rig.fake);
    std::vector<std::string> said;
    const auto server = a3app::serveEngineControl (second, truth, [&] (const std::string& line, bool) { said.push_back (line); });
    EXPECT_EQ (server, nullptr);
    ASSERT_FALSE (said.empty());
    EXPECT_NE (said.back().find ("runs on"), std::string::npos);
    second.close();
}

TEST (EngineControlApi, AnApplyTheControlThreadNeverRunsIsAnsweredBeforeTheWindowGivesUp)
{
    static_assert (a3app::EngineControlApi::applyWaitSeconds * 1000 < a3app::RemoteHostView::applyTimeoutMillis, "server first");
    RemoteRig rig;
    rig.api.setApplyWaitSeconds (0.2);
    const auto reply = httpclient::post (rig.server->port(), "/api/engine/apply", configBody (rig.fake.cfg));   // nobody ticks
    EXPECT_EQ (reply.status, 503);
    EXPECT_NE (parse (reply.body)["problem"].toString().indexOf ("did not answer"), -1);
}

TEST (EngineControlApi, NoiseWaitsForTheControlThreadInEitherZone)
{
    RemoteRig rig;
    const auto port = rig.server->port();
    EXPECT_EQ (httpclient::post (port, "/api/engine/noise", "{\"zone\":\"booth\",\"channel\":4}").status, 200);
    EXPECT_EQ (httpclient::post (port, "/api/engine/noise", "{\"zone\":\"main\",\"azimuth\":45,\"elevation\":-10.5}").status, 200);
    EXPECT_TRUE (rig.fake.noises.empty());   // the HTTP thread touched nothing
    rig.tick();
    ASSERT_EQ (rig.fake.noises.size(), 2u);
    EXPECT_EQ (rig.fake.noises[0].zone, a3::SpeakerZone::booth);
    EXPECT_FALSE (rig.fake.noises[0].ambisonic);
    EXPECT_EQ (rig.fake.noises[0].channel, 4);
    EXPECT_EQ (rig.fake.noises[1].zone, a3::SpeakerZone::main);
    EXPECT_TRUE (rig.fake.noises[1].ambisonic);
    EXPECT_FLOAT_EQ (rig.fake.noises[1].azimuth, 45.0f);
    EXPECT_FLOAT_EQ (rig.fake.noises[1].elevation, -10.5f);

    for (const auto* refused : { "{\"zone\":\"phones\",\"channel\":1}", "{\"zone\":\"main\",\"channel\":0}",
                                 "{\"zone\":\"main\",\"channel\":65}", "{\"zone\":\"main\"}", "{\"zone\":\"main\",\"azimuth\":1}", "nope" })
        EXPECT_EQ (httpclient::post (port, "/api/engine/noise", refused).status, 400) << refused;
    rig.tick();
    EXPECT_EQ (rig.fake.noises.size(), 2u);
}

TEST (EngineControlApi, NoiseRequestsRoundTrip)
{
    a3app::NoiseRequest burst;
    burst.zone = a3::SpeakerZone::booth;
    burst.channel = 7;
    juce::String why;
    const auto back = a3app::noiseFromJson (a3app::toJson (burst), why);
    ASSERT_TRUE (back) << why;
    EXPECT_EQ (back->zone, a3::SpeakerZone::booth);
    EXPECT_EQ (back->channel, 7);
    EXPECT_FALSE (back->ambisonic);
}

namespace
{
juce::var noiseBody (std::initializer_list<std::pair<const char*, juce::var>> members)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("zone", "main");
    for (const auto& m : members)
        o->setProperty (m.first, m.second);
    return juce::var (o);
}
}

// S4: only a whole channel number and finite angles; the elevation is clamped to the sphere.
TEST (EngineControlApi, NoiseRefusesWhatIsNoBurst)
{
    const auto inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
    juce::String why;
    EXPECT_FALSE (a3app::noiseFromJson (noiseBody ({ { "azimuth", inf }, { "elevation", 0.0 } }), why));
    EXPECT_FALSE (a3app::noiseFromJson (noiseBody ({ { "azimuth", 0.0 }, { "elevation", nan } }), why));
    EXPECT_FALSE (a3app::noiseFromJson (noiseBody ({ { "channel", true } }), why));
    EXPECT_FALSE (a3app::noiseFromJson (noiseBody ({ { "channel", "3" } }), why));
    EXPECT_FALSE (a3app::noiseFromJson (noiseBody ({ { "channel", 2.5 } }), why));
    EXPECT_FALSE (a3app::noiseFromJson (noiseBody ({ { "channel", inf } }), why));
    EXPECT_FALSE (why.isEmpty());

    const auto up = a3app::noiseFromJson (noiseBody ({ { "azimuth", 30.0 }, { "elevation", 120.0 } }), why);
    ASSERT_TRUE (up) << why;
    EXPECT_FLOAT_EQ (up->elevation, 90.0f);
    const auto down = a3app::noiseFromJson (noiseBody ({ { "azimuth", 30.0 }, { "elevation", -100.0 } }), why);
    ASSERT_TRUE (down) << why;
    EXPECT_FLOAT_EQ (down->elevation, -90.0f);
    const auto whole = a3app::noiseFromJson (noiseBody ({ { "channel", 3.0 } }), why);
    ASSERT_TRUE (whole) << why;
    EXPECT_EQ (whole->channel, 3);

    RemoteRig rig;
    for (const auto* refused : { "{\"zone\":\"main\",\"channel\":true}", "{\"zone\":\"main\",\"channel\":\"3\"}",
                                 "{\"zone\":\"main\",\"azimuth\":1e999,\"elevation\":0}" })
        EXPECT_EQ (httpclient::post (rig.server->port(), "/api/engine/noise", refused).status, 400) << refused;
}
