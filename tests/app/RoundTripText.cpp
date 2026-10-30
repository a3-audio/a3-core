// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The measured round trip as the backend runs it (RoundTripMeter: once after the start, on request) and as the
// AUDIO page says it: measured over the loop, else the estimate, labelled and with the reason.
#include <gtest/gtest.h>
#include "AudioBackend.h"
#include "EngineControlApi.h"
#include "RoundTripProbe.h"

namespace
{
using a3app::RoundTripReport;
using State = a3app::RoundTripReport::State;

juce::int64 at (int hours, int minutes) { return juce::Time (2026, 9, 30, hours, minutes, 0, 0, true).toMilliseconds(); }

a3app::RoundTrip estimate()
{
    a3app::RoundTrip trip;
    trip.sampleRate = 48000;
    trip.captureSamples = 256;
    trip.processingSamples = 128;
    trip.playbackSamples = 384;
    return trip;
}

// Drives the meter like a backend: the loop is `delay` samples, blocks of 128, the control tick between blocks.
void runLoop (a3app::RoundTripMeter& meter, double& now, int delay, bool patched = true)
{
    std::vector<float> line ((size_t) delay, 0.0f), in (128, 0.0f), out (128, 0.0f);
    for (int block = 0; block < 400; ++block)
    {
        meter.process (in.data(), out.data(), 128);
        for (size_t i = 0; i < out.size(); ++i)
        {
            line.push_back (out[i]);
            in[i] = patched ? line.front() : 0.0f;
            line.erase (line.begin());
        }
        now += 128.0 / 48000.0;
        meter.service (now, at (14, 5));
    }
}
}

TEST (RoundTripText, MeasuredSaysSoWithSamplesAndTime)
{
    RoundTripReport measured;
    measured.state = State::measured;
    measured.samples = 211;
    measured.atMillis = at (14, 5);
    const auto text = a3app::roundTripText (estimate(), measured);
    EXPECT_EQ (text, "Round trip (measured, digital loop)   4.4 ms (211 samples), 14:05");
    measured.measuring = true;
    EXPECT_TRUE (a3app::roundTripText (estimate(), measured).endsWith ("14:05, measuring again")) << a3app::roundTripText (estimate(), measured);
}

TEST (RoundTripText, TheEstimateIsTheFallbackWithItsReason)
{
    RoundTripReport report;
    report.state = State::noLoop;
    report.reason = "no loop configured";
    const auto none = a3app::roundTripText (estimate(), report);
    EXPECT_TRUE (none.startsWith ("Round trip (estimated, no loop configured)   16.0 ms (768 samples) = capture 5.3")) << none;

    report.state = State::noLoopFound;
    report.reason = "no loop found on output 11 -> input 11";
    EXPECT_TRUE (a3app::roundTripText (estimate(), report).contains ("(estimated, no loop found on output 11 -> input 11)"));

    report = {};
    report.state = State::notMeasured;
    report.measuring = true;
    EXPECT_TRUE (a3app::roundTripText (estimate(), report).contains ("(estimated, measuring)"));

    // An engine before the measurement reports nothing: the estimate as it always read.
    EXPECT_TRUE (a3app::roundTripText (estimate(), RoundTripReport {}).startsWith ("Round trip (estimated)   16.0 ms"));
    EXPECT_EQ (a3app::roundTripText (a3app::RoundTrip {}, report), "Round trip   no engine runs");
}

TEST (RoundTripText, TheStatusCarriesTheMeasurement)
{
    a3app::EngineStatusReport status;
    status.backend.roundTripMeasured.state = State::measured;
    status.backend.roundTripMeasured.samples = 211;
    status.backend.roundTripMeasured.atMillis = at (14, 5);
    status.backend.roundTripMeasured.measuring = true;
    status.loopDropped = { "booth_sub -> system:playback_11" };
    const auto back = a3app::statusFromJson (a3app::toJson (status));
    ASSERT_TRUE (back.has_value());
    EXPECT_EQ (back->backend.roundTripMeasured.state, State::measured);
    EXPECT_EQ (back->backend.roundTripMeasured.samples, 211);
    EXPECT_EQ (back->backend.roundTripMeasured.atMillis, at (14, 5));
    EXPECT_TRUE (back->backend.roundTripMeasured.measuring);
    EXPECT_EQ (back->loopDropped, status.loopDropped);

    status.backend.roundTripMeasured = {};
    status.backend.roundTripMeasured.state = State::noLoopFound;
    status.backend.roundTripMeasured.reason = "no loop found on output 11 -> input 11";
    const auto failed = a3app::statusFromJson (a3app::toJson (status));
    EXPECT_EQ (failed->backend.roundTripMeasured.state, State::noLoopFound);
    EXPECT_EQ (failed->backend.roundTripMeasured.reason, status.backend.roundTripMeasured.reason);
}

TEST (RoundTripText, TheMeterMeasuresOnceAfterTheStartAndOnRequest)
{
    a3app::RoundTripMeter meter (a3app::RoundTripLoop { 11, 11, 2 });
    double now = 100.0;
    meter.service (now, at (14, 0));
    EXPECT_EQ (meter.report().state, State::notMeasured);
    EXPECT_FALSE (meter.report().measuring);
    now += a3app::RoundTripMeter::autoAfterSeconds;
    runLoop (meter, now, 300);
    EXPECT_EQ (meter.report().state, State::measured);
    EXPECT_EQ (meter.report().samples, 300 + 128);
    EXPECT_EQ (meter.report().atMillis, at (14, 5));
    EXPECT_FALSE (meter.report().measuring);

    runLoop (meter, now, 300);   // nothing asked: no second run
    meter.request();
    EXPECT_TRUE (meter.report().measuring);
    runLoop (meter, now, 300, false);
    EXPECT_EQ (meter.report().state, State::noLoopFound);
    EXPECT_EQ (meter.report().reason, "no loop found on output 11 -> input 11");
}

TEST (RoundTripText, WithoutALoopTheMeterSaysWhy)
{
    a3app::RoundTripMeter none (std::nullopt);
    none.request();
    EXPECT_EQ (none.report().state, State::noLoop);
    EXPECT_EQ (none.report().reason, "no loop configured");
    EXPECT_FALSE (none.report().measuring);

    a3app::RoundTripMeter absent (a3app::RoundTripLoop { 11, 11, 2 }, "the device has no output 11");
    absent.request();
    EXPECT_EQ (absent.report().state, State::noLoop);
    EXPECT_EQ (absent.report().reason, "the device has no output 11");
}
