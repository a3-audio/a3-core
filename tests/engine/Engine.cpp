// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include "EngineFixture.h"
#include "Laws.h"

using namespace a3test;

namespace
{
constexpr size_t oneSecond = 44100;
constexpr size_t settled = 22050;           // decoders' crossovers and the 10 ms fades are long past
constexpr size_t echoAfter = 5512;          // DualDelay at 120 BPM, delayMult 4: 60 / (120 * 4) s
constexpr size_t afterTheCrossfade = 1024;  // a source switch's 10 ms (441 samples) are over

std::vector<const std::vector<float>*> channelsOf (const OutputSignals& out, const std::vector<size_t>& indices)
{
    std::vector<const std::vector<float>*> list;
    for (auto i : indices)
        list.push_back (&out.channels[i]);
    return list;
}

std::vector<const std::vector<float>*> stereo (const Stereo& s) { return { &s.l, &s.r }; }

void fillDeck (DeckSignals& deck, size_t frames, float level, uint32_t seed)
{
    for (int ch = 0; ch < deckChannels; ++ch)
        deck.bformat[(size_t) ch] = noise (frames, level, seed + (uint32_t) ch).l;
}

void wOnly (DeckSignals& deck, const std::vector<float>& w)
{
    deck.bformat[0] = w;
}

// Every analog pair below the return carries its own pink noise.
void pinkOnAnalogPairs (EngineRig& rig)
{
    for (int pair = 1; pair < rig.config.returnAnalogPair && 2 * pair <= rig.config.analogInputs; ++pair)
        rig.setAnalogPair (pair, pinkStereo (rig.frames, (uint32_t) pair));
}

Stereo both (const std::vector<float>& x) { return { x, x }; }

std::vector<float> encodedAt (const a3::SpatialConfig& spatial, const std::vector<float>& signal, float azimuth, int channel)
{
    auto parameters = spatial.encoderParameters.clone();
    auto* object = parameters.getDynamicObject();
    object->setProperty ("inputSetting", 1);
    object->setProperty ("azimuth0", azimuth);
    object->setProperty ("elevation0", 0.0f);
    object->setProperty ("gain0", 0.0f);
    auto node = a3::IemNode::create (a3::IemKind::multiEncoder, 1, deckChannels);
    EXPECT_TRUE (node->setParameters (parameters).wasOk());
    node->prepare (engineRate, engineBlock);
    std::vector<float> out (signal.size());
    juce::AudioBuffer<float> io (deckChannels, engineBlock);
    for (size_t start = 0; start < signal.size(); start += engineBlock)
    {
        const int n = (int) std::min<size_t> (engineBlock, signal.size() - start);
        io.clear();
        io.copyFrom (0, 0, signal.data() + start, n);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), deckChannels, n);
        node->process (view);
        std::copy_n (io.getReadPointer (channel), n, out.begin() + (long) start);
    }
    return out;
}

DeckSignals encodedDeck (const a3::SpatialConfig& spatial, const std::vector<float>& signal, float azimuth)
{
    DeckSignals deck;
    for (int ch = 0; ch < deckChannels; ++ch)
        deck.bformat[(size_t) ch] = encodedAt (spatial, signal, azimuth, ch);
    return deck;
}

// Left tops (positive azimuth) over right tops, in dB.
double leftOverRightDb (const EngineRig& rig, size_t from, size_t to)
{
    double left = 0.0, right = 0.0;
    const auto& tops = rig.spatial.main.topDirections;
    const auto indices = rig.out.mainIndices;
    const size_t firstTop = rig.spatial.main.layout.hasSub() ? 1 : 0;
    for (size_t k = 0; k < tops.size(); ++k)
    {
        const double e = energy (rig.out.channels[indices[firstTop + k]], from, to);
        (tops[k].first > 0.0f ? left : right) += e;
    }
    return 10.0 * std::log10 (left / right);
}
}

TEST (Engine, MatchesPlan3SpatialBusAtRestValues)
{
    const size_t frames = 2 * oneSecond;
    const auto inputs = pinkOnAllStrips (frames);
    EngineRig rig (engineConfig (4, 2), frames);
    for (int pair = 1; pair <= 4; ++pair)
        rig.setAnalogPair (pair, inputs[(size_t) pair - 1]);
    rig.run();

    a3::SpatialBus bus (rig.spatial);
    bus.prepare (engineRate, engineBlock);
    a3::SpatialControls positions;
    place (positions, staticPlacement);
    OutputSignals expected (rig.spatial, frames);
    runBus (bus, renderStrips (inputs), expected, { engineBlock }, positions);

    EXPECT_LT (maxDifference (rig.mainOutputs(), channelsOf (expected, expected.mainIndices), oneSecond), 1e-4);
    EXPECT_LT (maxDifference (rig.boothOutputs(), channelsOf (expected, expected.boothIndices), oneSecond), 1e-4);
    EXPECT_LT (maxDifference (rig.recOutputs(), channelsOf (expected, expected.recIndices), oneSecond), 1e-4);
    EXPECT_GT (peak (rig.mainOutputs(), oneSecond), 1e-2);
    EXPECT_EQ (peak (stereo (rig.phones)), 0.0);
}

TEST (Engine, AuxSendFeedsTheDelayEverywhereButNotDry)
{
    const size_t frames = oneSecond, hit = 1000;
    const auto run = [&] (float aux, bool expectSilentSend)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        rig->setAnalogPair (1, both (impulseAt (frames, hit)));
        rig->controls.channel (0).auxSend = aux;
        rig->controls.global().phonesMix = 1.0f;
        rig->run (0, 0, { engineBlock }, {},
                  [&] { if (expectSilentSend) EXPECT_EQ (rig->engine->lastFxSendPeak(), 0.0f); });
        return rig;
    };
    const auto wet = run (1.0f, false);
    const auto dry = run (0.0f, true);

    const size_t echoFrom = hit + echoAfter - 1000, echoTo = hit + echoAfter + 3000;
    const auto zones = [] (const EngineRig& r)
    {
        return std::vector<std::vector<const std::vector<float>*>> { r.mainOutputs(), r.boothOutputs(), r.recOutputs(),
                                                                      stereo (r.phones) };
    };
    const auto wetZones = zones (*wet), dryZones = zones (*dry);
    for (size_t z = 0; z < wetZones.size(); ++z)
    {
        const double wetEcho = energy (wetZones[z], echoFrom, echoTo), dryEcho = energy (dryZones[z], echoFrom, echoTo);
        EXPECT_GT (wetEcho, 1e-6) << "zone " << z;
        EXPECT_GT (wetEcho, 1000.0 * dryEcho) << "zone " << z;
        // Not dry: the delay's own dry path is -60 dB, so the direct sound is the dry run's.
        double difference = 0.0;
        for (size_t c = 0; c < wetZones[z].size(); ++c)
            for (size_t i = hit; i < hit + 2000; ++i)
                difference += std::pow ((double) (*wetZones[z][c])[i] - (*dryZones[z][c])[i], 2.0);
        EXPECT_LT (difference, 1e-4 * energy (dryZones[z], hit, hit + 2000)) << "zone " << z;
    }
}

TEST (Engine, CueIsPreVolumeAndMixIsPost)
{
    const size_t frames = oneSecond / 2;
    const auto run = [&] (float volume, float cue, float mix)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        rig->setAnalogPair (1, pinkStereo (frames, 7));
        rig->controls.channel (0).volume = volume;
        rig->controls.channel (0).cue = cue;
        rig->controls.global().phonesMix = mix;
        rig->run();
        return rig;
    };
    const auto cued = run (0.0f, 1.0f, 0.0f);
    EXPECT_GT (energy (stereo (cued->phones), settled / 4, frames), 1.0);
    EXPECT_EQ (peak (cued->mainOutputs()), 0.0);

    EXPECT_EQ (peak (stereo (run (0.0f, 0.0f, 0.0f)->phones)), 0.0);
    EXPECT_GT (energy (stereo (run (0.75f, 0.0f, 1.0f)->phones), settled / 4, frames), 1.0);
}

TEST (Engine, ReturnGoesToMainAndPhonesNotToFx)
{
    const size_t frames = oneSecond, hit = 1000;
    const auto run = [&] (float auxReturn)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        rig->setAnalogPair (rig->config.returnAnalogPair, both (impulseAt (frames, hit)));
        for (int c = 0; c < 4; ++c)
            rig->controls.channel (c).auxSend = 1.0f;
        rig->controls.global().phonesMix = 1.0f;
        rig->controls.global().auxReturn = auxReturn;
        rig->run (0, 0, { engineBlock }, {}, [&] { EXPECT_EQ (rig->engine->lastFxSendPeak(), 0.0f); });
        return rig;
    };
    const auto open = run (0.75f);
    const double direct = energy (open->mainOutputs(), hit, hit + 2000);
    EXPECT_GT (direct, 1e-4);
    EXPECT_LT (energy (open->mainOutputs(), hit + echoAfter - 1000, hit + echoAfter + 3000), 1e-9 * direct);
    EXPECT_GT (energy (stereo (open->phones), hit, hit + 2000), 1e-4);

    // RETURN 0 is silence (R18), not REAPER's -40 dB.
    const auto closed = run (0.0f);
    EXPECT_EQ (peak (closed->mainOutputs()), 0.0);
    EXPECT_EQ (peak (closed->recOutputs()), 0.0);
    EXPECT_EQ (peak (stereo (closed->phones)), 0.0);
}

TEST (Engine, MasterBoothAndPhonesAtZeroAreSilence)
{
    const size_t frames = oneSecond, change = frames / 2, quiet = change + 1024;
    struct Case { const char* name; std::function<void (a3::GlobalControls&)> close; };
    const std::vector<Case> cases {
        { "master", [] (a3::GlobalControls& g) { g.masterVolume = 0.0f; } },
        { "booth", [] (a3::GlobalControls& g) { g.booth = 0.0f; } },
        { "phones", [] (a3::GlobalControls& g) { g.phonesVolume = 0.0f; } },
    };
    for (const auto& c : cases)
    {
        EngineRig rig (engineConfig (4, 2), frames);
        rig.setAnalogPair (1, pinkStereo (frames, 3));
        rig.controls.global().phonesMix = 1.0f;
        rig.run (0, 0, { engineBlock }, [&] (size_t start) { if (start >= change) c.close (rig.controls.global()); });

        const auto mainPeak = peak (rig.mainOutputs(), quiet), boothPeak = peak (rig.boothOutputs(), quiet),
                   phonesPeak = peak (stereo (rig.phones), quiet);
        const std::string name = c.name;
        EXPECT_EQ (mainPeak == 0.0, name == "master") << name << " main " << mainPeak;
        EXPECT_EQ (boothPeak == 0.0, name == "booth") << name << " booth " << boothPeak;
        EXPECT_EQ (phonesPeak == 0.0, name == "phones") << name << " phones " << phonesPeak;
        EXPECT_GT (peak (rig.recOutputs(), quiet), 0.0) << name;
    }
}

TEST (Engine, SourceSelectIsExclusive)
{
    const size_t frames = oneSecond;
    const auto run = [&] (int source, bool analogSignal, bool deckSignal)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        if (analogSignal)   // starts after the 10 ms select crossfade, so any trace of it is a leak
        {
            auto signal = pinkStereo (frames, 11);
            std::fill_n (signal.l.begin(), afterTheCrossfade, 0.0f);
            std::fill_n (signal.r.begin(), afterTheCrossfade, 0.0f);
            rig->setAnalogPair (1, signal);
        }
        if (deckSignal)
            fillDeck (rig->decks[0], frames, 0.1f, 50);
        rig->controls.channel (0).source = source;
        rig->controls.channel (0).cue = 1.0f;
        rig->controls.channel (0).auxSend = 0.5f;
        rig->controls.global().phonesMix = 0.5f;
        rig->run();
        return rig;
    };
    // Analog chosen: the deck path never runs, bit for bit.
    EXPECT_EQ (maxDifference (run (0, true, true)->everyOutput(), run (0, true, false)->everyOutput()), 0.0);

    // Deck chosen: once the 10 ms crossfade is done the analog input reaches nothing, bit for bit.
    const auto deck = run (1, true, true);
    EXPECT_EQ (maxDifference (deck->everyOutput(), run (1, false, true)->everyOutput()), 0.0);
    EXPECT_GT (peak (deck->pre[0].l, settled), 1e-3);
    EXPECT_EQ (maxDifference (stereo (deck->pre[0]), { &deck->pre[0].r, &deck->pre[0].l }, 1024), 0.0);   // W on both sides
}

TEST (Engine, DeckPassesThroughToTheBuses)
{
    const size_t frames = oneSecond;
    EngineRig rig (engineConfig (4, 2), frames);
    fillDeck (rig.decks[0], frames, 0.1f, 70);
    rig.controls.channel (0).source = 1;
    rig.neutralStrip (0);
    rig.run (0, 0, { engineBlock }, {}, [&] { EXPECT_EQ (rig.engine->lastAnalogEncoderPeak(), 0.0f); });

    // The reference: the deck straight into main's SimpleDecoder, times MASTER.
    const auto& zone = rig.spatial.main;
    auto decoder = a3::IemNode::create (a3::IemKind::simpleDecoder, deckChannels, zone.layout.numOutputs());
    ASSERT_TRUE (decoder->setParameters (zone.parameters).wasOk());
    ASSERT_TRUE (decoder->loadDecoderConfig (zone.layout.decoderJson).wasOk());
    decoder->prepare (engineRate, engineBlock);
    const int width = std::max (deckChannels, zone.layout.numOutputs());
    std::vector<std::vector<float>> decoded ((size_t) zone.layout.numOutputs(), std::vector<float> (frames));
    juce::AudioBuffer<float> io (width, engineBlock);
    for (size_t start = 0; start < frames; start += engineBlock)
    {
        const int n = (int) std::min<size_t> (engineBlock, frames - start);
        io.clear();
        for (int ch = 0; ch < deckChannels; ++ch)
            io.copyFrom (ch, 0, rig.decks[0].bformat[(size_t) ch].data() + start, n);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), width, n);
        decoder->process (view);
        for (int ch = 0; ch < zone.layout.numOutputs(); ++ch)
            std::copy_n (io.getReadPointer (ch), n, decoded[(size_t) ch].begin() + (long) start);
    }
    const double master = a3::laws::stageGain (masterAtRest);
    std::vector<int> routing;
    if (zone.layout.hasSub())
        routing.push_back (zone.layout.subChannel);
    routing.insert (routing.end(), zone.layout.topChannels.begin(), zone.layout.topChannels.end());
    const auto mains = rig.mainOutputs();
    ASSERT_EQ (mains.size(), routing.size());
    double worst = 0.0;
    for (size_t k = 0; k < routing.size(); ++k)
        for (size_t i = settled; i < frames; ++i)
            worst = std::max (worst, std::abs ((double) (*mains[k])[i] - master * decoded[(size_t) routing[k] - 1][i]));
    EXPECT_LT (worst, 1e-5);
    EXPECT_GT (peak (mains, settled), 1e-3);
}

TEST (Engine, DeckAuxEchoesKeepTheDirection)
{
    const size_t frames = oneSecond, hit = 2000;
    EngineRig rig (engineConfig (4, 2), frames);
    rig.decks[0] = encodedDeck (rig.spatial, impulseAt (frames, hit), 90.0f);
    rig.controls.channel (0).source = 1;
    rig.neutralStrip (0);
    rig.controls.channel (0).auxSend = 1.0f;
    rig.run();

    const double dry = leftOverRightDb (rig, hit, hit + 1500);
    const double echo = leftOverRightDb (rig, hit + echoAfter - 500, hit + echoAfter + 3000);
    EXPECT_GT (dry, 3.0);
    EXPECT_NEAR (echo, dry, 1.0);

    // The deck's stereo aux is a return source only (R11).
    EngineRig auxOnly (engineConfig (4, 2), frames / 4);
    auxOnly.decks[0].aux = { noise (frames / 4, 0.3f, 5).l, noise (frames / 4, 0.3f, 6).l };
    auxOnly.controls.channel (0).source = 1;
    auxOnly.controls.channel (0).auxSend = 1.0f;
    auxOnly.run (0, 0, { engineBlock }, {}, [&] { EXPECT_EQ (auxOnly.engine->lastFxSendPeak(), 0.0f); });
    EXPECT_EQ (peak (auxOnly.everyOutput()), 0.0);   // nor anywhere behind the delay
}

TEST (Engine, DeckCueAndMixLikeAnalog)
{
    const size_t frames = oneSecond / 2;
    const auto run = [&] (float cue)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        fillDeck (rig->decks[0], frames, 0.1f, 90);
        rig->controls.channel (0).source = 1;
        rig->controls.channel (0).volume = 0.0f;
        rig->controls.channel (0).cue = cue;
        rig->run();
        return rig;
    };
    const auto cued = run (1.0f);
    EXPECT_GT (energy (stereo (cued->phones), settled / 4, frames), 1e-2);
    EXPECT_EQ (peak (cued->mainOutputs()), 0.0);
    EXPECT_EQ (peak (stereo (run (0.0f)->phones)), 0.0);
}

TEST (Engine, StemCueLandsAfterTheDecoder)
{
    const size_t frames = oneSecond / 2;
    const auto signal = sine (frames, 440.0, engineRate, 0.25f).l;
    const auto run = [&] (float mix)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        rig->decks[1].cue[0] = signal;
        rig->controls.global().phonesMix = mix;
        rig->run();
        return rig;
    };
    const auto heard = run (0.0f);
    const double gain = a3::laws::stageGain (0.75f) * a3::laws::cueWeight (0.0);
    double worst = 0.0;
    for (size_t i = 0; i < frames; ++i)
        worst = std::max (worst, std::abs ((double) heard->phones.l[i] - gain * signal[i]));
    EXPECT_LT (worst, 1e-5);
    EXPECT_EQ (peak (heard->phones.r), 0.0);
    EXPECT_EQ (peak (heard->mainOutputs()), 0.0);
    EXPECT_EQ (peak (heard->boothOutputs()), 0.0);
    EXPECT_EQ (peak (heard->recOutputs()), 0.0);

    EXPECT_EQ (peak (stereo (run (1.0f)->phones)), 0.0);
}

TEST (Engine, ReturnFromDeckAux)
{
    const size_t frames = oneSecond;
    const auto run = [&] (bool analogReturn)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        rig->decks[1].aux = { noise (frames, 0.2f, 21).l, noise (frames, 0.2f, 22).l };
        if (analogReturn)
            rig->setAnalogPair (rig->config.returnAnalogPair, pinkStereo (frames, 23));
        rig->controls.global().returnSource = 2;
        rig->run();
        return rig;
    };
    const auto withAnalog = run (true);
    EXPECT_GT (peak (withAnalog->mainOutputs(), settled), 1e-3);
    EXPECT_LT (maxDifference (withAnalog->everyOutput(), run (false)->everyOutput(), settled), 1e-6);
}

TEST (Engine, PositionsAndThreeDDoNothingOnADeckChannel)
{
    const size_t frames = oneSecond / 2;
    const auto run = [&] (bool sweep)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        rig->setAnalogPair (1, pinkStereo (frames, 31));
        fillDeck (rig->decks[0], frames, 0.1f, 32);
        rig->controls.channel (0).source = 1;
        rig->controls.channel (0).auxSend = 0.7f;
        rig->controls.channel (0).cue = 1.0f;
        rig->controls.global().phonesMix = 0.5f;
        rig->run (0, 0, { engineBlock }, [&] (size_t start)
        {
            if (! sweep || start < 2048)
                return;
            auto& ch = rig->controls.channel (0);
            const float phase = (float) (start % 4096) / 4096.0f;
            ch.azimuth = -180.0f + 360.0f * phase;
            ch.elevation = -90.0f + 180.0f * phase;
            ch.threeD = phase;
            ch.frequency = 1.0f - phase;
            ch.q = phase;
        });
        return rig;
    };
    EXPECT_EQ (maxDifference (run (true)->everyOutput(), run (false)->everyOutput()), 0.0);
}

TEST (Engine, SourceSwitchIsClickFree)
{
    const size_t frames = oneSecond;
    const float level = 0.5f;
    EngineRig rig (engineConfig (4, 2), frames);
    rig.setAnalogPair (1, sine (frames, 997.0, engineRate, level));
    wOnly (rig.decks[0], sine (frames, 1499.0, engineRate, level).l);
    wOnly (rig.decks[1], sine (frames, 2003.0, engineRate, level).l);
    rig.neutralStrip (0);
    rig.run (0, 0, { engineBlock }, [&] (size_t start)
    {
        const int quarter = (int) (start / (frames / 4));
        rig.controls.channel (0).source = quarter == 1 ? 1 : quarter == 2 ? 2 : 0;
    });

    const double inputStep = 2.0 * juce::MathConstants<double>::pi * 2003.0 / engineRate * level;
    EXPECT_LE (maxStep (rig.post[0].l), 2.0 * inputStep);
    EXPECT_LE (maxStep (rig.pre[0].l), 2.0 * inputStep);
    for (const auto* channel : rig.mainOutputs())
        EXPECT_LE (maxStep (*channel), 2.0 * inputStep);
    for (const auto* channel : rig.recOutputs())
        EXPECT_LE (maxStep (*channel), 2.0 * inputStep);
}

TEST (Engine, MissingDeckIsSilence)
{
    const size_t frames = oneSecond / 2;
    const auto run = [&] (int how)   // 0 = null pointers, 1 = zero signals, 2 = no deck inputs at all
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        rig->setAnalogPair (2, pinkStereo (frames, 41));
        if (how == 1)
        {
            for (auto& plane : rig->decks[0].bformat)
                plane = mono (frames);
            rig->decks[0].cue = rig->decks[0].aux = { mono (frames), mono (frames) };
        }
        rig->omitDecks = how == 2;
        rig->controls.channel (0).source = 1;
        rig->controls.channel (0).auxSend = 1.0f;
        rig->controls.global().phonesMix = 0.5f;
        rig->run();
        return rig;
    };
    const auto missing = run (0);
    EXPECT_EQ (peak (stereo (missing->pre[0])), 0.0);
    EXPECT_EQ (peak (stereo (missing->post[0])), 0.0);
    EXPECT_GT (peak (missing->post[1].l), 1e-3);
    EXPECT_EQ (maxDifference (missing->everyOutput(), run (1)->everyOutput()), 0.0);
    EXPECT_EQ (maxDifference (missing->everyOutput(), run (2)->everyOutput()), 0.0);
}

TEST (Engine, DeckOnlyChannelsHaveNoEncoderSource)
{
    auto config = engineConfig (4, 2);
    config.analogPairs = { 1, 0, 3, 0 };
    juce::String error;
    const auto engine = a3::Engine::create (config, loadedSpatialConfig (shippedSpatialDirectory), error);
    ASSERT_TRUE (engine != nullptr) << error;
    EXPECT_EQ (engine->analogSourceCount(), 2);
}

TEST (Engine, FilterIsGlobalAndPerChannelOn)
{
    const size_t frames = oneSecond / 2;
    const auto run = [&] (bool onChannelTwo)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        const auto low = sine (frames, 100.0, engineRate, 0.3f);
        rig->setAnalogPair (1, low);
        rig->setAnalogPair (2, low);
        rig->controls.global().filter.mode = 1;          // high-pass
        rig->controls.global().filter.frequency = 1.0f;  // about 5 kHz
        rig->controls.channel (1).filterOn = onChannelTwo ? 1.0f : 0.0f;
        rig->run();
        return rig;
    };
    const auto filtered = run (true), open = run (false);
    EXPECT_EQ (maxDifference (stereo (filtered->post[0]), stereo (open->post[0])), 0.0);
    EXPECT_LT (energy (filtered->post[1].l, settled / 2, frames), 0.01 * energy (open->post[1].l, settled / 2, frames));
}

TEST (Engine, BuildsForOneFourEight)
{
    const size_t frames = 8192;
    for (int channels : { 1, 4, 8 })
        for (int decks : { 0, 2 })
        {
            const auto config = engineConfig (channels, decks);
            EngineRig silent (config, frames);
            silent.run();
            EXPECT_EQ (peak (silent.everyOutput()), 0.0) << channels << " channels, " << decks << " decks";

            EngineRig loud (config, frames);
            pinkOnAnalogPairs (loud);
            loud.run();
            for (int c = 0; c < channels; ++c)
            {
                const bool hasInput = config.analogPairs[(size_t) c] != 0;
                EXPECT_EQ (peak (stereo (loud.post[(size_t) c])) > 0.0, hasInput)
                    << "channel " << c + 1 << " of " << channels << ", " << decks << " decks";
            }
        }
}

// IEM's DualDelay depends on the call size itself (its echoes differ between 64- and
// 512-sample calls even with the same prepare), so the delay is compared only where the
// engine hands it the same chunks: a call longer than the prepared block is cut into
// prepared-size chunks. Everything else matches for any prepare and call size once the
// decoders' start-up has settled (1 s, as Plan 3's comparison).
TEST (Engine, CallSizesDoNotChangeTheResult)
{
    const auto run = [] (size_t frames, float aux, int prepared, const std::vector<int>& calls)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames, prepared);
        pinkOnAnalogPairs (*rig);
        fillDeck (rig->decks[0], frames, 0.1f, 60);
        rig->decks[1].cue[0] = noise (frames, 0.1f, 61).l;
        rig->controls.channel (1).source = 1;
        for (int c = 0; c < 4; ++c)
            rig->controls.channel (c).auxSend = aux;
        rig->controls.global().phonesMix = 0.5f;
        rig->run (0, 0, calls);
        return rig;
    };

    const size_t shortRun = oneSecond / 2;
    const auto inQuanta = run (shortRun, 0.5f, engineBlock, { engineBlock });
    EXPECT_GT (peak (inQuanta->mainOutputs()), 1e-3);
    EXPECT_EQ (maxDifference (run (shortRun, 0.5f, engineBlock, { 2048, 640, 128 })->everyOutput(), inQuanta->everyOutput()), 0.0);

    const size_t longRun = oneSecond + oneSecond / 2;
    const auto reference = run (longRun, 0.0f, 512, { 512 });
    for (const auto& [prepared, calls] : { std::pair<int, std::vector<int>> { 64, { 64, 2048, 1000, 63 } },
                                           { 2048, { 2048, 64, 333 } } })
        EXPECT_LT (maxDifference (run (longRun, 0.0f, prepared, calls)->everyOutput(), reference->everyOutput(), oneSecond), 1e-6)
            << "prepared " << prepared;
}

TEST (Engine, DelayModulationIsNotOnTheAudioPath)
{
    EngineRig rig (engineConfig (4, 2), 100 * engineBlock);
    auto* pitch = rig.engine->delay().rawParameter ("pitchL");
    ASSERT_TRUE (pitch != nullptr);
    const float before = pitch->load();
    rig.run (0, 0, { engineBlock }, {}, [&] { EXPECT_EQ (pitch->load(), before); });

    rig.engine->serviceControls (3.7);
    EXPECT_NE (pitch->load(), before);
}

// The rig's quantum is 128; any call size gives the same output once the decoders have
// settled. Aux stays 0: IEM's DualDelay depends on the call size itself (see above).
TEST (Engine, AnyCallSizeMatchesTheQuantum)
{
    constexpr int quantum = 128;
    const size_t frames = oneSecond + oneSecond / 2;
    const auto run = [&] (const std::vector<int>& calls)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames, quantum);
        pinkOnAnalogPairs (*rig);
        fillDeck (rig->decks[0], frames, 0.1f, 100);
        rig->decks[1].cue[0] = noise (frames, 0.1f, 101).l;
        rig->decks[1].aux = { noise (frames, 0.1f, 102).l, noise (frames, 0.1f, 103).l };
        rig->controls.channel (1).source = 1;
        rig->controls.channel (0).cue = 1.0f;
        rig->controls.global().phonesMix = 0.5f;
        rig->controls.global().returnSource = 2;   // a control change would land on a different sample per call size
        rig->run (0, 0, calls);
        return rig;
    };
    const auto reference = run ({ quantum });
    EXPECT_GT (peak (reference->mainOutputs(), oneSecond), 1e-3);
    EXPECT_GT (peak (stereo (reference->phones), oneSecond), 1e-3);
    for (const auto& calls : std::vector<std::vector<int>> { { 64 }, { 1024 }, { 333 }, { 64, 128, 1024, 333 } })
        EXPECT_LT (maxDifference (run (calls)->everyOutput(), reference->everyOutput(), oneSecond), 1e-6)
            << "first call " << calls.front() << ", " << calls.size() << " sizes";
}

TEST (Engine, At48kIsFiniteAndNotSilent)
{
    constexpr double rate = 48000.0;
    const size_t frames = 48000;
    EngineRig rig (engineConfig (4, 2), frames, engineBlock, rate);
    pinkOnAnalogPairs (rig);
    fillDeck (rig.decks[0], frames, 0.1f, 110);
    rig.controls.channel (1).source = 1;
    for (int c = 0; c < 4; ++c)
        rig.controls.channel (c).auxSend = 0.5f;
    rig.controls.global().phonesMix = 0.5f;
    rig.run();

    for (const auto* channel : rig.everyOutput())
        for (float x : *channel)
            ASSERT_TRUE (std::isfinite (x));
    for (const auto& zone : { rig.mainOutputs(), rig.boothOutputs(), rig.recOutputs(), stereo (rig.phones) })
        EXPECT_GT (peak (zone, frames / 2), 1e-3);
}

// R14 (A8): while a channel plays a deck its analog strip does not run; it is reset
// before it fades back in, so what came in on the analog pair meanwhile leaves no trace.
TEST (Engine, SilentAnalogPathIsResetNotRun)
{
    const size_t frames = oneSecond / 2, back = 11008;   // a call boundary
    const auto run = [&] (bool analogMeanwhile)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        auto signal = pinkStereo (frames, 51);
        if (! analogMeanwhile)
        {
            std::fill_n (signal.l.begin(), back, 0.0f);
            std::fill_n (signal.r.begin(), back, 0.0f);
        }
        rig->setAnalogPair (1, signal);
        fillDeck (rig->decks[0], frames, 0.1f, 52);
        rig->controls.channel (0).threeD = 0.6f;
        rig->run (0, 0, { engineBlock }, [&] (size_t start)
        {
            // Analog -> deck once the initial 10 ms are past, back to analog at `back`.
            rig->controls.channel (0).source = start >= 1024 && start < back ? 1 : 0;
        });
        return rig;
    };
    const auto loud = run (true), quiet = run (false);
    const auto taps = [] (const EngineRig& r)
    {
        return std::vector<const std::vector<float>*> { &r.pre[0].l, &r.pre[0].r, &r.post[0].l, &r.post[0].r };
    };
    EXPECT_GT (peak (taps (*loud), back + 1024), 1e-3);
    EXPECT_EQ (maxDifference (taps (*loud), taps (*quiet), back), 0.0);
}
