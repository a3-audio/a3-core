// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The one program's command line: roles, their flags, and the core role's defaults as B1's
// argparse took them (a3-core 7a7b244, a3-core.py's main block; a3_screencast_fanout.main).
#include <gtest/gtest.h>

#include <algorithm>

#include "ConfigFile.h"
#include "ConnectionPlan.h"
#include "FakePlatform.h"
#include "PackagedEngine.h"
#include "ProgramArgs.h"
#include "Roles.h"
#include "Truth.h"

using a3app::Role;

namespace
{
std::optional<a3app::ProgramArgs> parse (std::initializer_list<const char*> words, juce::String& error)
{
    juce::StringArray list;
    for (auto* w : words)
        list.add (w);
    return a3app::parseProgramArgs (list, error);
}

std::optional<a3app::ProgramArgs> parse (std::initializer_list<const char*> words)
{
    juce::String error;
    return parse (words, error);
}

bool refused (std::initializer_list<const char*> words, const juce::String& named)
{
    juce::String error;
    const auto args = parse (words, error);
    EXPECT_FALSE (args.has_value()) << juce::StringArray (words.begin(), static_cast<int> (words.size())).joinIntoString (" ");
    EXPECT_TRUE (error.contains (named)) << error;
    return ! args.has_value();
}

a3core::Truth truthCopy() { return std::get<a3core::Truth> (a3core::Truth::load (juce::File (A3_TRUTH_COPY), std::nullopt)); }
}

TEST (ProgramArgs, DefaultIsTheWindow)
{
    const auto args = parse ({});
    ASSERT_TRUE (args);
    EXPECT_EQ (args->role, Role::window);
    EXPECT_EQ (parse ({ "--role", "window" })->role, Role::window);
    EXPECT_EQ (parse ({ "--headless" })->role, Role::supervisor);
    EXPECT_EQ (parse ({ "--role", "i3-bar" })->role, Role::i3Bar);
}

TEST (ProgramArgs, CoreFlags)
{
    const auto args = parse ({ "--role", "core", "--ip", "10.1.2.3", "--port", "0", "--mixer", "mixer.local:1234",
                               "--motion", "host:with:colons:99", "--subscriber", "light=10.0.0.9:7000",
                               "--subscriber", "video=10.0.0.8:7001", "--print-osc", "--web-bind", "127.0.0.1:0", "--no-web" });
    ASSERT_TRUE (args);
    EXPECT_EQ (args->role, Role::core);
    EXPECT_EQ (args->coreGiven.ip, std::optional<std::string> ("10.1.2.3"));
    EXPECT_EQ (args->coreGiven.port, std::optional<int> (0));
    ASSERT_TRUE (args->coreGiven.mixer);
    EXPECT_EQ (*args->coreGiven.mixer, (a3core::Endpoint { "mixer.local", 1234 }));
    ASSERT_TRUE (args->coreGiven.motion);
    EXPECT_EQ (*args->coreGiven.motion, (a3core::Endpoint { "host:with:colons", 99 }));   // rpartition(":")
    EXPECT_EQ (args->core.core.subscribers, (std::vector<std::string> { "light=10.0.0.9:7000", "video=10.0.0.8:7001" }));
    EXPECT_TRUE (args->core.core.printOsc);
    EXPECT_EQ (args->core.webBind, "127.0.0.1:0");
    EXPECT_TRUE (args->core.noWeb);

    const auto bare = parse ({ "--role", "core" });
    ASSERT_TRUE (bare);
    EXPECT_FALSE (bare->coreGiven.ip || bare->coreGiven.port || bare->coreGiven.mixer || bare->coreGiven.motion);
    EXPECT_TRUE (bare->core.core.subscribers.empty());
    EXPECT_FALSE (bare->core.core.printOsc || bare->core.noWeb);
    EXPECT_TRUE (bare->core.webBind.empty());

    EXPECT_TRUE (refused ({ "--role", "core", "--port", "seven" }, "--port"));
    EXPECT_TRUE (refused ({ "--role", "core", "--mixer", "nohost" }, "--mixer"));
    EXPECT_TRUE (refused ({ "--role", "core", "--motion", "host:" }, "--motion"));
    EXPECT_TRUE (refused ({ "--role", "core", "--ip" }, "--ip"));
}

// Carried (a): B1's defaults come from the truth, the truth and network files from A3_OSC_TRUTH and
// A3_NETWORK, the state from XDG_STATE_HOME, the host name from the machine -- all through the platform.
TEST (ProgramArgs, CoreDefaultsComeFromTheTruthAndTheEnvironment)
{
    fake::FakePlatform platform;
    platform.env["A3_OSC_TRUTH"] = A3_TRUTH_COPY;
    platform.env["XDG_STATE_HOME"] = "/var/xdg";
    const auto truth = truthCopy();

    const auto options = a3app::coreOptions (*parse ({ "--role", "core" }), platform);
    ASSERT_TRUE (std::holds_alternative<a3core::ServiceOptions> (options)) << std::get<std::string> (options);
    const auto& o = std::get<a3core::ServiceOptions> (options);
    EXPECT_EQ (o.core.ip, truth.host ("any"));
    EXPECT_EQ (o.core.port, truth.port ("core", "osc"));
    EXPECT_EQ (o.core.mixer, truth.endpoint ("mixer", "osc"));
    EXPECT_EQ (o.core.motion, truth.endpoint ("motion", "osc"));
    EXPECT_TRUE (o.webBind.empty());   // CoreService then takes the truth's core.web (default_bind)
    EXPECT_EQ (o.home, platform.homeDir);
    EXPECT_EQ (o.stateDir, juce::File ("/var/xdg"));   // Core puts its files in <this>/a3-core
    EXPECT_EQ (o.packageShare, platform.homeDir.getChildFile (".local/share/a3-core"));
    EXPECT_EQ (o.hostname, "test-host");
    EXPECT_EQ (o.truthPath, std::optional<juce::File> (juce::File (A3_TRUTH_COPY)));
    EXPECT_EQ (o.networkPath, std::nullopt);   // an explicit truth never reads the maintainer's file
    EXPECT_EQ (o.departmentsFile, platform.configDir().getChildFile ("core/departments.json"));   // the window's list, Core's own config

    const auto given = a3app::coreOptions (*parse ({ "--role", "core", "--ip", "10.1.2.3", "--port", "0", "--mixer", "m:1",
                                                     "--motion", "n:2", "--web-bind", "w:3", "--subscriber", "a=b:4" }),
                                           platform);
    const auto& g = std::get<a3core::ServiceOptions> (given);
    EXPECT_EQ (g.core.ip, "10.1.2.3");
    EXPECT_EQ (g.core.port, 0);
    EXPECT_EQ (g.core.mixer, (a3core::Endpoint { "m", 1 }));
    EXPECT_EQ (g.core.motion, (a3core::Endpoint { "n", 2 }));
    EXPECT_EQ (g.webBind, "w:3");
    EXPECT_EQ (g.core.subscribers, std::vector<std::string> { "a=b:4" });
}

TEST (ProgramArgs, CoreFilesAsB1)
{
    fake::FakePlatform platform;
    EXPECT_EQ (a3app::coreFiles (platform).truth, juce::File ("/usr/share/a3/a3-osc.json"));
    EXPECT_EQ (a3app::coreFiles (platform).network, std::optional<juce::File> (platform.homeDir.getChildFile (".config/a3/network.json")));

    platform.env["A3_NETWORK"] = "/elsewhere/network.json";
    EXPECT_EQ (a3app::coreFiles (platform).network, std::optional<juce::File> (juce::File ("/elsewhere/network.json")));

    platform.env.erase ("A3_NETWORK");
    platform.env["A3_OSC_TRUTH"] = "/checkout/a3-osc.json";
    EXPECT_EQ (a3app::coreFiles (platform).truth, juce::File ("/checkout/a3-osc.json"));
    EXPECT_EQ (a3app::coreFiles (platform).network, std::nullopt);

    platform.env["A3_OSC_TRUTH"] = "";   // `os.environ.get(...) or DEFAULT_PATH`: empty is unset
    EXPECT_EQ (a3app::coreFiles (platform).truth, juce::File ("/usr/share/a3/a3-osc.json"));
    EXPECT_EQ (a3app::coreFiles (platform).network, std::optional<juce::File> (platform.homeDir.getChildFile (".config/a3/network.json")));
}

TEST (ProgramArgs, ATruthThatDoesNotLoadIsCoresRefusal)
{
    fake::FakePlatform platform;
    platform.env["A3_OSC_TRUTH"] = "/nonexistent/a3-osc.json";
    const auto options = a3app::coreOptions (*parse ({ "--role", "core" }), platform);
    ASSERT_TRUE (std::holds_alternative<std::string> (options));
    EXPECT_EQ (std::get<std::string> (options).rfind ("a3-core: ", 0), 0u) << std::get<std::string> (options);
}

// Carried (f): Plan 4's engine flags under --role engine; --no-window and --service are accepted
// and do nothing (the engine role has no window).
TEST (ProgramArgs, EngineKeepsPlan4Flags)
{
    const auto args = parse ({ "--role", "engine", "--config", "/c/engine.json", "--spatial", "/s", "--truth", "/t/a3-osc.json",
                               "--no-connect", "--timing", "--check-config", "--no-window", "--service" });
    ASSERT_TRUE (args);
    EXPECT_EQ (args->role, Role::engine);
    EXPECT_EQ (args->engine.config, "/c/engine.json");
    EXPECT_EQ (args->engine.spatial, "/s");
    EXPECT_EQ (args->engine.truth, "/t/a3-osc.json");
    EXPECT_TRUE (args->engine.noConnect);
    EXPECT_TRUE (args->engine.timing);
    EXPECT_TRUE (args->engine.checkConfig);

    const auto bare = parse ({ "--role", "engine" });
    ASSERT_TRUE (bare);
    EXPECT_EQ (bare->engine.truth, a3app::AppArgs().truth);
    EXPECT_FALSE (bare->engine.noConnect || bare->engine.timing || bare->engine.checkConfig);
    EXPECT_TRUE (refused ({ "--role", "engine", "--config" }, "--config"));
}

TEST (ProgramArgs, FlagOfAnotherRoleIsRefused)
{
    EXPECT_TRUE (refused ({ "--role", "engine", "--port", "1" }, "--port"));
    EXPECT_TRUE (refused ({ "--role", "core", "--config", "x" }, "--config"));
    EXPECT_TRUE (refused ({ "--role", "core", "--linger", "1" }, "--linger"));
    EXPECT_TRUE (refused ({ "--role", "screencast-fanout", "--mixer", "m:1", "--port", "5", "--", "cat" }, "--mixer"));
    EXPECT_TRUE (refused ({ "--no-web" }, "--no-web"));
    EXPECT_TRUE (refused ({ "--headless", "--no-connect" }, "--no-connect"));
    EXPECT_TRUE (refused ({ "--role", "i3-bar", "--print-osc" }, "--print-osc"));
    EXPECT_TRUE (refused ({ "--role", "core", "--bogus" }, "--bogus"));
    EXPECT_TRUE (refused ({ "--role", "nothing" }, "nothing"));
    EXPECT_TRUE (refused ({ "--role" }, "--role"));
    EXPECT_TRUE (refused ({ "--headless", "--role", "core" }, "--headless"));
    EXPECT_TRUE (refused ({ "--role", "core", "--role", "engine" }, "--role"));
    EXPECT_TRUE (refused ({ "--render-truth", "user", "--role", "core" }, "--render-truth"));
    EXPECT_TRUE (refused ({ "--role", "core", "--", "x" }, "--"));
}

TEST (ProgramArgs, FanoutTakesTheCommandAfterDashes)
{
    const auto args = parse ({ "--role", "screencast-fanout", "--port", "5", "--linger", "2.5", "--", "ffmpeg", "-i", "x", "--port", "3", "--role" });
    ASSERT_TRUE (args);
    EXPECT_EQ (args->role, Role::screencastFanout);
    EXPECT_EQ (args->fanoutPort, 5);
    EXPECT_DOUBLE_EQ (args->fanoutLinger, 2.5);
    EXPECT_EQ (args->encoder, juce::StringArray ({ "ffmpeg", "-i", "x", "--port", "3", "--role" }));

    const auto bare = parse ({ "--role", "screencast-fanout" });
    ASSERT_TRUE (bare);
    EXPECT_EQ (bare->fanoutPort, std::nullopt);
    EXPECT_DOUBLE_EQ (bare->fanoutLinger, 10.0);
    EXPECT_TRUE (bare->encoder.isEmpty());
    EXPECT_TRUE (refused ({ "--role", "screencast-fanout", "--linger", "soon" }, "--linger"));
}

// Carried (e): B1's main refused a port of 0 and a missing command with parser.error (exit 2); the
// port's default is A3_SCREENCAST_PORT.
TEST (ProgramArgs, FanoutRefusesWhatB1Refused)
{
    const juce::StringArray command ({ "ffmpeg", "-i", "x" });
    EXPECT_EQ (a3app::fanoutRefusal (5, command), std::nullopt);
    EXPECT_EQ (a3app::fanoutRefusal (0, command), std::optional<std::string> ("needs --port (or A3_SCREENCAST_PORT) and -- <encoder command>"));
    EXPECT_EQ (a3app::fanoutRefusal (5, {}), std::optional<std::string> ("needs --port (or A3_SCREENCAST_PORT) and -- <encoder command>"));

    fake::FakePlatform platform;
    EXPECT_EQ (a3app::fanoutPort (*parse ({ "--role", "screencast-fanout" }), platform), 0);
    platform.env["A3_SCREENCAST_PORT"] = "5901";
    EXPECT_EQ (a3app::fanoutPort (*parse ({ "--role", "screencast-fanout" }), platform), 5901);
    EXPECT_EQ (a3app::fanoutPort (*parse ({ "--role", "screencast-fanout", "--port", "7" }), platform), 7);
    EXPECT_EQ (a3app::fanoutPort (*parse ({ "--role", "screencast-fanout", "--port", "0" }), platform), 0);   // given: no default
    platform.env["A3_SCREENCAST_PORT"] = "not a port";
    EXPECT_EQ (a3app::fanoutPort (*parse ({ "--role", "screencast-fanout" }), platform), 0);
}

TEST (ProgramArgs, Subcommands)
{
    for (const auto* what : { "user", "network-file", "network" })
    {
        const auto args = parse ({ "--render-truth", what });
        ASSERT_TRUE (args);
        EXPECT_EQ (args->role, Role::renderTruth);
        EXPECT_EQ (args->subcommand, juce::StringArray (what));
    }
    EXPECT_TRUE (refused ({ "--render-truth", "everything" }, "everything"));
    EXPECT_TRUE (refused ({ "--render-truth" }, "--render-truth"));

    const auto host = parse ({ "--truth", "host", "core" });
    ASSERT_TRUE (host);
    EXPECT_EQ (host->role, Role::truthQuery);
    EXPECT_EQ (host->subcommand, juce::StringArray ({ "host", "core" }));
    const auto port = parse ({ "--truth", "port", "core", "osc" });
    ASSERT_TRUE (port);
    EXPECT_EQ (port->role, Role::truthQuery);
    EXPECT_EQ (port->subcommand, juce::StringArray ({ "port", "core", "osc" }));
    EXPECT_TRUE (refused ({ "--truth", "host" }, "--truth"));
    EXPECT_TRUE (refused ({ "--truth", "port", "core" }, "--truth"));
    EXPECT_TRUE (refused ({ "--truth", "host", "core", "extra" }, "--truth"));
    EXPECT_TRUE (refused ({ "--truth", "/some/a3-osc.json" }, "--truth"));   // a file is the engine's --truth
}

TEST (ProgramArgs, LinksPrintsTheConnections)
{
    const auto args = parse ({ "--links" });
    ASSERT_TRUE (args);
    EXPECT_EQ (args->role, Role::links);
    EXPECT_TRUE (refused ({ "--links", "--headless" }, "cannot be combined"));
    EXPECT_TRUE (refused ({ "--links", "--role", "core" }, "cannot be combined"));
    EXPECT_TRUE (refused ({ "--links", "--port", "7" }, "--port"));
    EXPECT_TRUE (refused ({ "--links", "--links" }, "twice"));
}

TEST (ProgramArgs, ClockFindsItsPhcFromTheAudioPortByDefault)
{
    const auto args = parse ({ "--role", "clock" });
    ASSERT_TRUE (args);
    EXPECT_EQ (args->role, Role::clock);
    EXPECT_EQ (args->clockDevice, "");   // resolved from the truth's audio port
    EXPECT_EQ (args->clockScarlett, "alsa_input.usb-Focusrite_Scarlett_18i20");
    EXPECT_FALSE (args->clockObserve);
    EXPECT_EQ (a3app::roleWord (Role::clock), "clock");
}

TEST (ProgramArgs, ClockTakesADeviceAndObserve)
{
    const auto args = parse ({ "--role", "clock", "--device", "/dev/ptp1", "--observe", "--scarlett", "alsa_input.usb-Other" });
    ASSERT_TRUE (args);
    EXPECT_EQ (args->clockDevice, "/dev/ptp1");
    EXPECT_TRUE (args->clockObserve);
    EXPECT_EQ (args->clockScarlett, "alsa_input.usb-Other");
    refused ({ "--role", "clock", "--device" }, "--device needs a value");
    refused ({ "--role", "clock", "--config", "x" }, "--config is not a flag of --role clock");
    refused ({ "--role", "engine", "--observe" }, "--observe is not a flag of --role engine");
    EXPECT_TRUE (a3app::usage != nullptr && juce::String (a3app::usage).contains ("--role clock [--device PATH] [--scarlett NODE] [--observe]"));
}

TEST (ProgramArgs, TheClocksUnitIsSystemds)
{
    EXPECT_EQ (a3app::unitOf (Role::clock), "a3-clock.service");
}

// The Core's AES67 senders (2026-11-06): the truth's table against the layout the engine starts with.
TEST (ProgramArgs, CoreOptionsResolveTheFiveStreamsFromTheShippedConfig)
{
    const a3apptest::PackagedEngine packaged;
    fake::FakePlatform platform;
    platform.env["A3_OSC_TRUTH"] = A3_PACKAGE_TRUTH;
    auto args = *parse ({ "--role", "core" });
    args.engine.packageConfig = packaged.config;

    const auto options = a3app::coreOptions (args, platform);
    ASSERT_TRUE (std::holds_alternative<a3core::ServiceOptions> (options)) << std::get<std::string> (options);
    const auto& o = std::get<a3core::ServiceOptions> (options);
    ASSERT_TRUE (o.aes67Streams.has_value());
    ASSERT_EQ (o.aes67Streams->size(), 5u);
    EXPECT_EQ ((*o.aes67Streams)[0].session, "AES67 test-host out");
    EXPECT_EQ ((*o.aes67Streams)[1].key, "core-main");
    EXPECT_EQ ((*o.aes67Streams)[1].channelNames, (std::vector<std::string> { "SUB", "T1", "T2", "T3", "T4" }));   // Default 4.1
    EXPECT_EQ (o.aes67LegacyOnlyBecause, "");
}

TEST (ProgramArgs, CoreOptionsFallBackToTheLegacyStreamWhenTheLayoutCannotBeLoaded)
{
    const a3apptest::PackagedEngine packaged;
    fake::FakePlatform platform;
    platform.env["A3_OSC_TRUTH"] = A3_PACKAGE_TRUTH;
    auto args = *parse ({ "--role", "core" });

    args.engine.packageConfig = packaged.dir.getChildFile ("missing.json");
    const auto missing = std::get<a3core::ServiceOptions> (a3app::coreOptions (args, platform));
    ASSERT_TRUE (missing.aes67Streams.has_value());
    ASSERT_EQ (missing.aes67Streams->size(), 1u);
    EXPECT_TRUE ((*missing.aes67Streams)[0].legacy);
    EXPECT_NE (missing.aes67LegacyOnlyBecause.find ("missing.json"), std::string::npos) << missing.aes67LegacyOnlyBecause;

    // A config whose spatial directory is not there.
    const auto noSpatial = packaged.dir.getChildFile ("no-spatial.json");
    noSpatial.replaceWithText ("{\"spatial\": \"nowhere\"}");
    args.engine.packageConfig = noSpatial;
    const auto unread = std::get<a3core::ServiceOptions> (a3app::coreOptions (args, platform));
    ASSERT_TRUE (unread.aes67Streams.has_value());
    EXPECT_EQ (unread.aes67Streams->size(), 1u);
    EXPECT_NE (unread.aes67LegacyOnlyBecause.find ("nowhere"), std::string::npos) << unread.aes67LegacyOnlyBecause;
}

namespace
{
const a3core::Truth& packageTruth()
{
    static const auto truth = std::get<a3core::Truth> (a3core::Truth::load (juce::File (A3_PACKAGE_TRUTH), std::nullopt));
    return truth;
}

// The engine started with --spatial on the REAPER booth copy (a 6.1 booth): not the package config's 4.1.
a3app::AppArgs engineWithSixOneBooth (const a3apptest::PackagedEngine& packaged)
{
    a3app::AppArgs args;
    args.config = packaged.config.getFullPathName();
    args.truth = A3_TRUTH_COPY;
    args.spatial = a3test::reaperBoothSpatialDirectory.getFullPathName();
    args.hostname = "a3nuc1";
    args.aes67 = a3app::Aes67Render { packageTruth(), packaged.dir, juce::File (A3_PACKAGE_SHARE).getChildFile ("pipewire/pipewire-aes67.conf.in"),
                                      "a3nuc1" };
    return args;
}

const a3core::StreamSpec* streamNamed (const std::vector<a3core::StreamSpec>& streams, const std::string& key)
{
    for (const auto& spec : streams)
        if (spec.key == key)
            return &spec;
    return nullptr;
}
}

// F1 (2026-11-06): the engine resolves its streams from the layout it really runs (--config/--spatial as loaded), at
// the start as at every Save, so the two agree.
TEST (EngineStreams, StartAndSaveAgreeForANonDefaultLayout)
{
    const a3apptest::PackagedEngine packaged;
    const auto args = engineWithSixOneBooth (packaged);
    juce::String error;
    const auto loaded = a3app::loadStartupData (args, packaged.dir, error);
    ASSERT_TRUE (loaded.has_value()) << error;

    const auto atStart = a3app::engineStreamsFor (args, loaded->spatial, {});
    const auto* booth = streamNamed (atStart.streams, "core-booth");
    ASSERT_NE (booth, nullptr);
    EXPECT_EQ (booth->channelNames.size(), 7u) << "the --spatial 6.1 booth, not the package config's 4.1";
    EXPECT_TRUE (atStart.said.empty());

    const auto saved = a3app::loadZonesSpatial (loaded->config, error);   // what restart() reads for the same layout
    ASSERT_TRUE (saved.has_value()) << error;
    const auto afterSave = a3app::engineStreamsFor (args, *saved, atStart.streams);
    EXPECT_EQ (a3core::streamsSignature (afterSave.streams), a3core::streamsSignature (atStart.streams));
    EXPECT_EQ (a3core::streamsSignature (atStart.streams), a3core::streamsSignature (a3core::resolveTxStreams (packageTruth(), loaded->spatial, "a3nuc1")));
}

TEST (EngineStreams, WithoutTheCoresTruthTheReasonIsSaidAndOnlyTheLegacyRecIsLinked)
{
    const a3apptest::PackagedEngine packaged;
    auto args = engineWithSixOneBooth (packaged);
    args.aes67.reset();
    args.aes67Missing = "a3-osc.json: not there";
    juce::String error;
    const auto loaded = a3app::loadStartupData (args, packaged.dir, error);
    ASSERT_TRUE (loaded.has_value()) << error;

    const auto atStart = a3app::engineStreamsFor (args, loaded->spatial, {});
    EXPECT_TRUE (atStart.streams.empty());
    ASSERT_EQ (atStart.said.size(), 1u);
    EXPECT_EQ (atStart.said[0], "AES67: the legacy stream only: a3-osc.json: not there");

    const auto linked = a3app::linkMapFor (loaded->config, args.hostname, atStart.streams);
    for (const auto& [port, peers] : linked.jack)
        for (const auto& peer : peers)
            EXPECT_EQ (peer.rfind ("AES67 Core ", 0), std::string::npos) << port << " -> " << peer;
    const auto& recL = linked.jack.at ("rec_L");
    EXPECT_NE (std::find (recL.begin(), recL.end(), "AES67 a3nuc1 out:send_CH1"), recL.end());
}

TEST (EngineStreams, ATruthThatDoesNotLoadGivesTheReasonInsteadOfARender)
{
    fake::FakePlatform platform;
    platform.env["A3_OSC_TRUTH"] = "/nowhere/a3-osc.json";
    const auto render = a3app::engineAes67Render (platform);
    ASSERT_TRUE (std::holds_alternative<std::string> (render));
    EXPECT_NE (std::get<std::string> (render).find ("/nowhere/a3-osc.json"), std::string::npos) << std::get<std::string> (render);
}

// F2: a zone above the packet limit is refused alone; the engine says which, as the Core does.
TEST (EngineStreams, AStreamAboveThePacketLimitIsSaidAndLeftOut)
{
    const a3apptest::PackagedEngine packaged;
    const auto args = engineWithSixOneBooth (packaged);
    juce::String error;
    const auto loaded = a3app::loadStartupData (args, packaged.dir, error);
    ASSERT_TRUE (loaded.has_value()) << error;
    auto spatial = loaded->spatial;
    spatial.main.layout.subChannel = 1;
    spatial.main.layout.topChannels.clear();
    for (int k = 2; k <= 23; ++k)   // 22 tops and the sub: 23 channels at 0.5 ms
        spatial.main.layout.topChannels.push_back (k);

    const auto resolved = a3app::engineStreamsFor (args, spatial, {});
    EXPECT_EQ (streamNamed (resolved.streams, "core-main"), nullptr);
    EXPECT_NE (streamNamed (resolved.streams, "core-booth"), nullptr);
    ASSERT_EQ (resolved.said.size(), 1u);
    EXPECT_EQ (resolved.said[0].rfind ("AES67: aes67_streams: 'core-main'", 0), 0u) << resolved.said[0];

    const auto core = a3app::streamsFor (packageTruth(), spatial, "a3nuc1");
    ASSERT_EQ (core.problems.size(), 1u);
    EXPECT_EQ ("AES67: " + core.problems[0], resolved.said[0]);
}
