// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The analog chain (analog in -> AD -> engine -> DA -> analog out): a one-time measurement over a temporary cable
// from a free output to an input, run with the digital loop in the same run. The converters' share (analog
// minus digital) is stored with its date in engine.json; the AUDIO page adds it to the live digital loop.
#include <gtest/gtest.h>
#include "AppTruth.h"
#include "AudioBackend.h"
#include "ConfigFile.h"
#include "EngineControlApi.h"
#include "RoundTripProbe.h"
#include "SpatialTestData.h"

namespace
{
using State = a3app::RoundTripReport::State;
using Analog = a3app::RoundTripReport::Analog;

juce::int64 at (int day, int hours, int minutes) { return juce::Time (2026, 9, day, hours, minutes, 0, 0, true).toMilliseconds(); }

a3app::IoMap parsedIo (const juce::String& json, juce::String& error)
{
    return a3app::parseIoMap (juce::JSON::parse (json), { { "analog_1", "analog_2" }, { "main_sub", "main_top_1" } }, error)
        .value_or (a3app::IoMap {});
}

// Both loops as a backend drives them: blocks of 128, the control tick between blocks.
void run (a3app::RoundTripMeter& meter, double& now, int digitalDelay, int analogDelay, bool analogPatched = true, int blocks = 400)
{
    std::vector<float> digitalLine ((size_t) digitalDelay, 0.0f), analogLine ((size_t) analogDelay, 0.0f);
    std::vector<float> dIn (128, 0.0f), dOut (128, 0.0f), aIn (128, 0.0f), aOut (128, 0.0f);
    for (int block = 0; block < blocks; ++block)
    {
        meter.process (dIn.data(), dOut.data(), 128);
        meter.processAnalog (aIn.data(), aOut.data(), 128);
        for (size_t i = 0; i < 128; ++i)
        {
            digitalLine.push_back (dOut[i]);
            dIn[i] = digitalLine.front();
            digitalLine.erase (digitalLine.begin());
            analogLine.push_back (aOut[i]);
            aIn[i] = analogPatched ? analogLine.front() : 0.0f;
            analogLine.erase (analogLine.begin());
        }
        now += 128.0 / 48000.0;
        meter.service (now, at (30, 14, 5), 48000.0);
    }
}

a3app::RoundTripReport measuredNow (int digitalSamples)
{
    a3app::RoundTripReport report;
    report.state = State::measured;
    report.samples = digitalSamples;
    report.atMillis = at (30, 14, 5);
    return report;
}

a3app::RoundTrip estimate48k()
{
    a3app::RoundTrip trip;
    trip.sampleRate = 48000;
    trip.processingSamples = 128;
    return trip;
}
}

TEST (AnalogChain, ThePairAndTheStoredShareRoundTripThroughTheConfig)
{
    juce::String error;
    EXPECT_FALSE (parsedIo ("{}", error).analogLoop.has_value()) << error;   // optional: no default pair
    const auto chosen = parsedIo ("{\"analogLoop\":{\"output\":6,\"input\":8}}", error);
    ASSERT_TRUE (chosen.analogLoop.has_value()) << error;
    EXPECT_EQ (chosen.analogLoop->output, 6);
    EXPECT_EQ (chosen.analogLoop->input, 8);
    EXPECT_FALSE (chosen.analogLoop->converters.has_value());

    a3app::IoMap map;
    map.analogLoop = a3app::AnalogLoop { 6, 8, a3app::ConverterShare { 41, 48000.0, "2026-10-30" } };
    const auto back = parsedIo (juce::JSON::toString (a3app::toJson (map)), error);
    ASSERT_TRUE (back.analogLoop.has_value()) << error;
    EXPECT_EQ (*back.analogLoop, *map.analogLoop);

    for (const auto* broken : { "{\"analogLoop\":{\"output\":6}}", "{\"analogLoop\":{\"output\":6,\"input\":8,\"converterSamples\":41}}",
                                "{\"analogLoop\":{\"output\":-1,\"input\":8}}" })
    {
        parsedIo (broken, error);
        EXPECT_TRUE (error.contains ("analogLoop")) << broken << ": " << error;
    }
}

TEST (AnalogChain, OnlyAFreePairOffTheDigitalLoopCanBeMeasured)
{
    a3app::IoMap map;
    map.roundTripLoop = a3app::RoundTripLoop { 11, 11, 2 };
    EXPECT_EQ (a3app::analogLoopRefusal (map), "no analog test pair chosen (I/O page)");
    map.analogLoop = a3app::AnalogLoop { 6, 0, {} };
    EXPECT_EQ (a3app::analogLoopRefusal (map), "no analog test input chosen (I/O page)");
    map.analogLoop = a3app::AnalogLoop { 6, 8, {} };
    EXPECT_TRUE (a3app::analogLoopRefusal (map).isEmpty());
    map.analogLoop = a3app::AnalogLoop { 12, 8, {} };
    EXPECT_TRUE (a3app::analogLoopRefusal (map).contains ("output 12")) << a3app::analogLoopRefusal (map);
    map.analogLoop = a3app::AnalogLoop { 6, 8, {} };
    map.jack["main_top_1"] = { "system:playback_6" };
    EXPECT_EQ (a3app::analogLoopRefusal (map), "output 6 is in use (main_top_1)");
    map.jack.clear();
    map.deviceChannels["analog_2"] = 7;   // device input 8
    EXPECT_EQ (a3app::analogLoopRefusal (map), "input 8 is in use (analog_2)");
}

TEST (AnalogChain, OneRunMeasuresBothLoopsAndYieldsTheConverterShareOnce)
{
    a3app::RoundTripMeter meter (a3app::RoundTripLoop { 11, 11, 2 }, {}, a3app::AnalogLoop { 6, 8, {} }, {});
    double now = 0.0;
    run (meter, now, 300, 300, true, 1000);   // the automatic digital run: the analog loop is never run on its own
    EXPECT_EQ (meter.report().state, State::measured);
    EXPECT_EQ (meter.report().analog, Analog::notMeasured);
    EXPECT_FALSE (meter.takeNewConverterShare().has_value());

    meter.requestAnalog();
    EXPECT_TRUE (meter.report().analogMeasuring);
    run (meter, now, 300, 345);
    const auto report = meter.report();
    EXPECT_EQ (report.analog, Analog::measured);
    EXPECT_EQ (report.converters.samples, 45);
    EXPECT_EQ (report.converters.sampleRate, 48000.0);
    EXPECT_EQ (report.converters.measuredOn, "2026-10-30");   // juce::Time counts months from 0
    EXPECT_FALSE (report.analogMeasuring);
    const auto fresh = meter.takeNewConverterShare();
    ASSERT_TRUE (fresh.has_value());
    EXPECT_EQ (*fresh, report.converters);
    EXPECT_FALSE (meter.takeNewConverterShare().has_value());
}

TEST (AnalogChain, NoCableIsNoResultAndTheStoredShareStays)
{
    const a3app::ConverterShare stored { 40, 48000.0, "2026-09-01" };
    a3app::RoundTripMeter meter (a3app::RoundTripLoop { 11, 11, 2 }, {}, a3app::AnalogLoop { 6, 8, stored }, {});
    EXPECT_EQ (meter.report().analog, Analog::measured);
    double now = 0.0;
    meter.requestAnalog();
    run (meter, now, 300, 345, false);
    EXPECT_EQ (meter.report().analogReason, "no analog loop found on output 6 -> input 8: is the cable plugged in?");
    EXPECT_EQ (meter.report().converters, stored);
    EXPECT_FALSE (meter.takeNewConverterShare().has_value());
}

TEST (AnalogChain, ARefusedPairIsNeverRun)
{
    a3app::RoundTripMeter meter (a3app::RoundTripLoop { 11, 11, 2 }, {}, a3app::AnalogLoop { 6, 8, {} }, "output 6 is in use (main_top_1)");
    meter.requestAnalog();
    EXPECT_FALSE (meter.report().analogMeasuring);
    EXPECT_EQ (meter.report().analogReason, "output 6 is in use (main_top_1)");
    a3app::RoundTripMeter noDigital (std::nullopt, {}, a3app::AnalogLoop { 6, 8, {} }, {});
    noDigital.requestAnalog();
    EXPECT_FALSE (noDigital.report().analogMeasuring);
    EXPECT_TRUE (noDigital.report().analogReason.contains ("digital loop")) << noDigital.report().analogReason;
}

TEST (AnalogChain, ThePageAddsTheStoredConvertersToTheLiveDigitalLoop)
{
    auto report = measuredNow (211);   // 4.396 ms
    report.analog = Analog::measured;
    report.converters = { 48, 48000.0, "2026-10-30" };   // 1.0 ms
    EXPECT_EQ (a3app::analogChainText (estimate48k(), report),
               "Analog chain   5.4 ms (measured: digital loop now + converters measured on 2026-10-30)");

    report.state = State::noLoopFound;
    EXPECT_EQ (a3app::analogChainText (estimate48k(), report),
               "Analog chain   digital loop not measured now; converters 1.0 ms (measured on 2026-10-30)");

    auto none = measuredNow (211);
    none.analog = Analog::notMeasured;
    EXPECT_EQ (a3app::analogChainText (estimate48k(), none), "Analog chain   converters not measured yet");
    none.analogReason = "no analog test pair chosen (I/O page)";
    EXPECT_EQ (a3app::analogChainText (estimate48k(), none),
               "Analog chain   converters not measured yet (no analog test pair chosen (I/O page))");
    none.analogMeasuring = true;
    EXPECT_TRUE (a3app::analogChainText (estimate48k(), none).endsWith (", measuring")) << a3app::analogChainText (estimate48k(), none);
}

TEST (AnalogChain, TheStatusCarriesTheAnalogResult)
{
    a3app::EngineStatusReport status;
    auto& analog = status.backend.roundTripMeasured;
    analog.analog = Analog::measured;
    analog.converters = { 48, 48000.0, "2026-10-30" };
    analog.analogMeasuring = true;
    analog.analogReason = "why";
    const auto back = a3app::statusFromJson (a3app::toJson (status));
    ASSERT_TRUE (back.has_value());
    EXPECT_EQ (back->backend.roundTripMeasured.analog, Analog::measured);
    EXPECT_EQ (back->backend.roundTripMeasured.converters, analog.converters);
    EXPECT_TRUE (back->backend.roundTripMeasured.analogMeasuring);
    EXPECT_EQ (back->backend.roundTripMeasured.analogReason, "why");
}

// The engine keeps the share in its config file: only io.analogLoop changes, the rest stays as written.
TEST (AnalogChain, TheShareIsStoredInTheConfigFile)
{
    auto dir = juce::File::createTempFile ("a3-analog-chain");
    dir.createDirectory();
    const auto spatial = juce::JSON::toString (juce::var (a3test::shippedSpatialDirectory.getFullPathName()));
    const auto running = dir.getChildFile ("package.json"), saved = dir.getChildFile ("user/engine.json");
    running.replaceWithText ("{\"engine\":{\"channels\":3},\"io\":{\"jack\":{\"main_sub\":[\"system:playback_1\"]},"
                             "\"analogLoop\":{\"output\":6,\"input\":8}},\"spatial\":" + spatial + "}");
    const a3app::ConverterShare share { 47, 44100.0, "2026-10-30" };
    const auto stored = a3app::storeConverterShare (running, saved, share, a3apptest::truthDecks());
    ASSERT_TRUE (stored.wasOk()) << stored.getErrorMessage();
    juce::String error;
    const auto loaded = a3app::loadAppConfig (saved, error, a3apptest::truthDecks());
    ASSERT_TRUE (loaded.has_value()) << error;
    ASSERT_TRUE (loaded->io.analogLoop.has_value());
    EXPECT_EQ (loaded->io.analogLoop->converters, std::optional<a3app::ConverterShare> (share));
    EXPECT_EQ (loaded->io.analogLoop->output, 6);
    EXPECT_EQ (loaded->engine.channels, 3);
    EXPECT_EQ (loaded->io.jack.at ("main_sub"), std::vector<std::string> { "system:playback_1" });
    EXPECT_FALSE (juce::JSON::parse (saved)["io"].getDynamicObject()->hasProperty ("deviceChannels"));   // not rewritten whole
    dir.deleteRecursively();
}
