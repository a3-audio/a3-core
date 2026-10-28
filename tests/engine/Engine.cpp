// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include "ChannelSelect.h"
#include <map>
#include "EngineFixture.h"
#include "Laws.h"
#include "VuTestData.h"

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

// Every analog pair the tests give channels carries its own pink noise.
void pinkOnAnalogPairs (EngineRig& rig)
{
    for (int pair = 1; pair <= testAnalogPairs && 2 * pair <= rig.config.analogInputs; ++pair)
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

// P45: analog 11/12 is no return any more; the FX Return is the delay's master, so a signal there
// reaches no output at any RETURN, cue or MIX.
TEST (FxReturn, TheOldAnalogReturnPairReachesNothing)
{
    const size_t frames = oneSecond / 2;
    EngineRig rig (engineConfig (4, 2), frames);
    rig.setAnalogPair (6, sine (frames, 440.0, engineRate, 0.5f));
    rig.controls.global().auxReturn = 1.0f;
    rig.controls.global().returnCue = 1.0f;
    rig.controls.global().phonesMix = 0.5f;
    rig.run();
    EXPECT_EQ (peak (rig.everyOutput()), 0.0);
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
    rig.decks[0].aux = rig.decks[0].bformat;   // the echoes come from the aux bus
    rig.controls.channel (0).source = 1;
    rig.neutralStrip (0);
    rig.controls.channel (0).auxSend = 0.0f;   // the deck main stays out of the delay
    rig.run();

    const double dry = leftOverRightDb (rig, hit, hit + 1500);
    const double echo = leftOverRightDb (rig, hit + echoAfter - 500, hit + echoAfter + 3000);
    EXPECT_GT (dry, 3.0);
    EXPECT_NEAR (echo, dry, 1.0);
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

namespace
{
constexpr int cueAcns = 9;
constexpr size_t afterStartUp = settled / 2;   // the decoder's start-up is over; the runs here last half a second

// The same B-format once as a deck's stem cue, once as a channel's pre-VOLUME cue tap.
std::unique_ptr<EngineRig> runDeckCue (const std::array<std::vector<float>, cueAcns>& signal, size_t frames, float mix,
                                       float phonesVolume, bool asCue, float channelCueKey = 0.0f)
{
    auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
    for (int acn = 0; acn < cueAcns; ++acn)
        if (asCue)
            rig->decks[1].cue[(size_t) acn] = signal[(size_t) acn];
        else
            rig->decks[0].bformat[(size_t) acn] = signal[(size_t) acn];
    if (! asCue)
    {
        rig->controls.channel (0).source = 1;
        rig->controls.channel (0).cue = 1.0f;
        rig->neutralStrip (0);
        rig->controls.channel (0).volume = 0.0f;
    }
    else
        rig->controls.channel (0).cue = channelCueKey;
    rig->controls.global().phonesMix = mix;
    rig->controls.global().phonesVolume = phonesVolume;
    rig->run();
    return rig;
}

// The channel path's TRIM/VOLUME laws sit 1.3e-4 (relative) off unity at their nominal setting, so the two
// routes are compared relative to their level; a decoder, HRTF or ACN mistake is orders of magnitude larger.
void expectSamePhones (const Stereo& a, const Stereo& b, const char* what)
{
    const double level = std::max (peak (stereo (a), afterStartUp), peak (stereo (b), afterStartUp));
    EXPECT_GT (level, 1e-4) << what;
    EXPECT_LT (maxDifference (stereo (a), stereo (b), afterStartUp), 5e-4 * level) << what;
}

std::array<std::vector<float>, cueAcns> noiseCue (size_t frames, uint32_t seed, int firstAcn = 0)
{
    std::array<std::vector<float>, cueAcns> signal;
    for (int acn = 0; acn < cueAcns; ++acn)
        signal[(size_t) acn] = acn < firstAcn ? mono (frames) : noise (frames, 0.1f, seed + (uint32_t) acn).l;
    return signal;
}
}

TEST (Engine, DeckCueIsDecodedWithThePhones)
{
    const size_t frames = oneSecond / 2;
    std::array<std::vector<float>, cueAcns> dc;
    for (auto& plane : dc)
        plane = mono (frames);
    dc[0] = std::vector<float> (frames, 0.5f);

    const auto cue = runDeckCue (dc, frames, 0.0f, 1.0f, true);
    EXPECT_GT (peak (cue->phones.l, afterStartUp), 1e-3);
    EXPECT_NEAR (cue->phones.l.back(), cue->phones.r.back(), 1e-5);   // W alone has no side

    // One decoder, one HRTF: a channel's cue tap with the same B-format sounds the same.
    const auto tap = runDeckCue (dc, frames, 0.0f, 1.0f, false);
    expectSamePhones (cue->phones, tap->phones, "W only");

    const auto all = noiseCue (frames, 700);
    expectSamePhones (runDeckCue (all, frames, 0.0f, 1.0f, true)->phones, runDeckCue (all, frames, 0.0f, 1.0f, false)->phones, "ACN 0..8");
}

TEST (Engine, DeckCueMovesOnlyWithCueMixAndPhones)
{
    const size_t frames = oneSecond / 2;
    const auto signal = noiseCue (frames, 710);
    EXPECT_GT (peak (stereo (runDeckCue (signal, frames, 0.0f, 1.0f, true)->phones), afterStartUp), 1e-3);
    EXPECT_EQ (peak (stereo (runDeckCue (signal, frames, 1.0f, 1.0f, true)->phones)), 0.0);   // CUE end of the mix
    EXPECT_EQ (peak (stereo (runDeckCue (signal, frames, 0.0f, 0.0f, true)->phones)), 0.0);   // PHONES 0

    EXPECT_EQ (maxDifference (stereo (runDeckCue (signal, frames, 0.5f, 1.0f, true, 0.0f)->phones),
                              stereo (runDeckCue (signal, frames, 0.5f, 1.0f, true, 1.0f)->phones)), 0.0);
}

TEST (Engine, DeckCueNeverReachesMainBoothRec)
{
    const size_t frames = oneSecond / 2;
    const auto run = [&] (bool withCue)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        rig->setAnalogPair (1, pinkStereo (frames, 720));
        if (withCue)
            for (int acn = 0; acn < cueAcns; ++acn)
                rig->decks[1].cue[(size_t) acn] = noise (frames, 0.2f, 730 + (uint32_t) acn).l;
        rig->controls.global().phonesMix = 0.0f;
        rig->run();
        return rig;
    };
    const auto cued = run (true), quiet = run (false);
    EXPECT_GT (peak (cued->mainOutputs()), 1e-3);
    EXPECT_EQ (maxDifference (cued->mainOutputs(), quiet->mainOutputs()), 0.0);
    EXPECT_EQ (maxDifference (cued->boothOutputs(), quiet->boothOutputs()), 0.0);
    EXPECT_EQ (maxDifference (cued->recOutputs(), quiet->recOutputs()), 0.0);
    EXPECT_GT (maxDifference (stereo (cued->phones), stereo (quiet->phones)), 1e-4);
}

// The cue carries ACN 0..8 only: each lands on its own bus channel, none on 9..15. A cue shifted by one
// (design text counts 1..9) or spilling over would differ from the same signal as a 16-channel B-format.
TEST (Engine, CueAcnTenToSixteenStayEmpty)
{
    const size_t frames = oneSecond / 2;
    for (int first = 0; first < cueAcns; first += 4)
    {
        const auto signal = noiseCue (frames, 740, first);
        expectSamePhones (runDeckCue (signal, frames, 0.0f, 1.0f, true)->phones,
                          runDeckCue (signal, frames, 0.0f, 1.0f, false)->phones, ("first ACN " + std::to_string (first)).c_str());
    }
}

namespace
{
struct ReturnSettings
{
    float auxReturn = 1.0f;
    float phonesMix = 1.0f;
    bool cue = false;
    float azimuth = 30.0f;
};

// Channel 1 sends `dry` to the delay at `auxSend`; the FX Return is set as `settings` says.
std::unique_ptr<EngineRig> returnRig (size_t frames, const ReturnSettings& settings, const std::vector<float>& dry, float auxSend)
{
    auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
    rig->setAnalogPair (1, both (dry));
    rig->controls.channel (0).auxSend = auxSend;
    rig->controls.channel (0).azimuth = settings.azimuth;
    rig->controls.global().phonesMix = settings.phonesMix;
    rig->controls.global().returnCue = settings.cue ? 1.0f : 0.0f;
    rig->controls.global().auxReturn = settings.auxReturn;
    rig->run();
    return rig;
}

// What the delay adds to every output: the run with AUX 1 minus the same run at AUX 0.
std::vector<std::vector<float>> delayPart (size_t frames, const ReturnSettings& settings, const std::vector<float>& dry)
{
    const auto wet = returnRig (frames, settings, dry, 1.0f);
    const auto plain = returnRig (frames, settings, dry, 0.0f);
    const auto w = wet->everyOutput(), p = plain->everyOutput();
    std::vector<std::vector<float>> part (w.size(), std::vector<float> (frames));
    for (size_t c = 0; c < w.size(); ++c)
        for (size_t i = 0; i < frames; ++i)
            part[c][i] = (*w[c])[i] - (*p[c])[i];
    return part;
}

std::vector<const std::vector<float>*> pointersTo (const std::vector<std::vector<float>>& channels)
{
    std::vector<const std::vector<float>*> list;
    for (const auto& c : channels)
        list.push_back (&c);
    return list;
}

// everyOutput's phones pair: after rec, main and booth.
std::vector<const std::vector<float>*> phonesOf (const std::vector<std::vector<float>>& part, const EngineRig& layout)
{
    const size_t first = layout.out.channels.size();
    return { &part[first], &part[first + 1] };
}

std::vector<const std::vector<float>*> speakersOf (const std::vector<std::vector<float>>& part, const EngineRig& layout)
{
    std::vector<const std::vector<float>*> list;
    for (size_t c = 0; c < layout.out.channels.size(); ++c)
        list.push_back (&part[c]);
    return list;
}

constexpr size_t hit = settled / 2;
constexpr size_t echoFrom = hit + echoAfter - 1000, echoTo = hit + echoAfter + 3000;

double echoEnergy (const std::vector<const std::vector<float>*>& channels) { return energy (channels, echoFrom, echoTo); }
}

// P45: the FX Return is the delay's master. Every output's delay part is the delay bus at unity
// RETURN, times RETURN's gain: at 0 exact silence, at 1.0 the bus itself, in between its gain.
TEST (FxReturn, IsTheDelayBusScaledByReturn)
{
    const size_t frames = oneSecond;
    const auto dry = impulseAt (frames, hit);
    const auto unity = delayPart (frames, { 1.0f }, dry);
    ASSERT_GT (echoEnergy (pointersTo (unity)), 1e-6);
    for (const float level : { 0.5f, 0.8f })
    {
        const auto scaled = delayPart (frames, { level }, dry);
        const double ratio = std::sqrt (echoEnergy (pointersTo (scaled)) / echoEnergy (pointersTo (unity)));
        EXPECT_NEAR (ratio, a3::laws::stageGain (level), 1e-4) << level;
    }
    const auto closed = delayPart (frames, { 0.0f }, dry);
    for (size_t c = 0; c < closed.size(); ++c)
        for (size_t i = 0; i < frames; ++i)
            ASSERT_EQ (closed[c][i], 0.0f) << "output " << c << " at " << i;
}

// An analog channel's send is encoded at the channel's position before the delay (B-format):
// the same echo from another azimuth lands on other speakers.
TEST (FxReturn, AnAnalogSendReachesTheDelayInBFormat)
{
    const size_t frames = oneSecond;
    const auto dry = impulseAt (frames, hit);
    const EngineRig layout (engineConfig (4, 2), 64);
    const auto front = delayPart (frames, { 1.0f, 1.0f, false, 0.0f }, dry);
    const auto back = delayPart (frames, { 1.0f, 1.0f, false, 180.0f }, dry);
    ASSERT_GT (echoEnergy (speakersOf (front, layout)), 1e-6);
    double moved = 0.0;
    for (size_t c = 0; c < layout.out.channels.size(); ++c)
        moved = std::max (moved, std::abs (energy (front[c], echoFrom, echoTo) - energy (back[c], echoFrom, echoTo)));
    EXPECT_GT (moved, 0.1 * echoEnergy (speakersOf (front, layout)) / (double) layout.out.channels.size());
}

// The cue is a PFL, as a channel's: before RETURN, at cueWeight; MIX adds the return after RETURN.
TEST (FxReturn, CueIsBeforeReturnAndMixIsAfter)
{
    const size_t frames = oneSecond;
    const auto dry = impulseAt (frames, hit);
    const EngineRig layout (engineConfig (4, 2), 64);

    const auto cuedClosed = delayPart (frames, { 0.0f, 0.0f, true }, dry);
    EXPECT_GT (echoEnergy (phonesOf (cuedClosed, layout)), 1e-6);
    EXPECT_EQ (peak (speakersOf (cuedClosed, layout)), 0.0);

    const auto cuedOpen = delayPart (frames, { 0.5f, 0.0f, true }, dry);
    EXPECT_NEAR (std::sqrt (echoEnergy (phonesOf (cuedOpen, layout)) / echoEnergy (phonesOf (cuedClosed, layout))), 1.0, 1e-4);

    const auto uncued = delayPart (frames, { 1.0f, 0.0f, false }, dry);
    EXPECT_EQ (peak (phonesOf (uncued, layout)), 0.0);
}

TEST (FxReturn, PhonesFollowMixLikeAChannel)
{
    const size_t frames = oneSecond;
    const auto dry = impulseAt (frames, hit);
    const EngineRig layout (engineConfig (4, 2), 64);
    const auto full = delayPart (frames, { 1.0f, 1.0f, false }, dry);
    const auto half = delayPart (frames, { 0.5f, 0.5f, false }, dry);
    ASSERT_GT (echoEnergy (phonesOf (full, layout)), 1e-6);
    const double ratio = std::sqrt (echoEnergy (phonesOf (half, layout)) / echoEnergy (phonesOf (full, layout)));
    EXPECT_NEAR (ratio, a3::laws::stageGain (0.5) * a3::laws::mixWeight (0.5), 1e-4);
}

TEST (FxReturn, ReturnMovesAreClickFree)
{
    const size_t frames = oneSecond;
    const float level = 0.5f;
    EngineRig rig (engineConfig (4, 2), frames);
    rig.setAnalogPair (1, sine (frames, 110.0, engineRate, level));
    rig.neutralStrip (0);
    rig.controls.channel (0).auxSend = 1.0f;
    rig.controls.channel (0).volume = 1.0f;
    rig.controls.global().phonesMix = 1.0f;
    rig.run (0, 0, { engineBlock }, [&] (size_t start)
    {
        const int quarter = (int) (start / (frames / 4));
        rig.controls.global().auxReturn = quarter == 1 || quarter == 3 ? 1.0f : 0.5f;
        rig.controls.global().returnCue = quarter >= 2 ? 1.0f : 0.0f;
    });

    EXPECT_GT (peak (rig.mainOutputs(), settled), 0.1);
    for (const auto* channel : rig.mainOutputs())
        EXPECT_LT (maxStep (*channel, settled / 4), 0.05);
    for (const auto* channel : rig.recOutputs())
        EXPECT_LT (maxStep (*channel, settled / 4), 0.05);
    for (const auto* channel : stereo (rig.phones))
        EXPECT_LT (maxStep (*channel, settled / 4), 0.05);
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
            rig->decks[0].cue = { mono (frames), mono (frames) };
            for (auto& plane : rig->decks[0].aux)
                plane = mono (frames);
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
// The bound is float rounding in the zone decoders, relative to the level: 1.8e-6 at peaks ~0.11
// (1.6e-5 of the peak) once the analog return's noise left this run with P45.
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
        rig->controls.channel (1).source = 1;
        rig->controls.channel (0).cue = 1.0f;
        rig->controls.global().phonesMix = 0.5f;
        rig->run (0, 0, calls);
        return rig;
    };
    const auto reference = run ({ quantum });
    const double bound = 5e-5 * peak (reference->everyOutput(), oneSecond);
    EXPECT_GT (peak (reference->mainOutputs(), oneSecond), 1e-3);
    EXPECT_GT (peak (stereo (reference->phones), oneSecond), 1e-3);
    for (const auto& calls : std::vector<std::vector<int>> { { 64 }, { 1024 }, { 333 }, { 64, 128, 1024, 333 } })
        EXPECT_LT (maxDifference (run (calls)->everyOutput(), reference->everyOutput(), oneSecond), bound)
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

// R14 (A8): the analog path that comes back from a deck starts from reset() (filters and
// the isolator's coefficients cleared) and must still fade in without a click.
TEST (Engine, AnalogFadeInAfterResetIsClickFree)
{
    const size_t frames = oneSecond / 2, back = 11008;   // a call boundary
    const float level = 0.5f;
    EngineRig rig (engineConfig (4, 2), frames);
    rig.setAnalogPair (1, sine (frames, 997.0, engineRate, level));
    rig.neutralStrip (0);
    rig.controls.channel (0).threeD = 0.6f;     // the isolator splits moving and steady
    rig.controls.channel (0).filterOn = 1.0f;   // and the filter runs from its reset
    rig.controls.global().filter.frequency = 0.3f;
    rig.run (0, 0, { engineBlock }, [&] (size_t start) { rig.controls.channel (0).source = start >= 1024 && start < back ? 1 : 0; });

    const double inputStep = 2.0 * juce::MathConstants<double>::pi * 997.0 / engineRate * level;
    const size_t from = back - 64, to = back + 2048;   // the 10 ms fade-in and the first control blocks
    const auto stepIn = [&] (const std::vector<float>& x)
    {
        double worst = 0.0;
        for (size_t i = from; i < to; ++i)
            worst = std::max (worst, (double) std::abs (x[i] - x[i - 1]));
        return worst;
    };
    EXPECT_GT (peak (rig.post[0].l, back + 1024, to), 1e-2);
    for (const auto* x : { &rig.post[0].l, &rig.post[0].r, &rig.pre[0].l, &rig.pre[0].r })
        EXPECT_LE (stepIn (*x), 2.0 * inputStep);
    for (const auto* x : rig.mainOutputs())
        EXPECT_LE (stepIn (*x), 2.0 * inputStep);
}

namespace
{

std::vector<float> constant (size_t frames, float level) { return std::vector<float> (frames, level); }

// Deck 1 on channel 1 at unity, AUX 1; `setUp` feeds it and moves its controls.
std::unique_ptr<EngineRig> deckOnChannelOne (size_t frames, const std::function<void (EngineRig&)>& setUp)
{
    auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
    rig->controls.channel (0).source = 1;
    rig->neutralStrip (0);
    rig->controls.channel (0).auxSend = 1.0f;
    setUp (*rig);
    return rig;
}

// The largest |sample| sent into the delay from `from` on, call by call.
float fxSendFrom (EngineRig& rig, size_t from)
{
    float worst = 0.0f;
    size_t at = 0;
    rig.run (0, 0, { engineBlock }, [&] (size_t start) { at = start; },
             [&] { if (at >= from) worst = std::max (worst, rig.engine->lastFxSendPeak()); });
    return worst;
}
}

// Like an analog channel's: the deck's main 16 after VOLUME, times the desk AUX.
TEST (Engine, DeckMainReachesTheDelayOnlyThroughTheDeskAux)
{
    const size_t frames = oneSecond;
    const float level = 0.5f;
    const auto mainDc = [&] (float auxSend, float volume)
    {
        auto rig = deckOnChannelOne (frames, [&] (EngineRig& r)
        {
            r.decks[0].bformat[0] = constant (frames, level);
            r.controls.channel (0).auxSend = auxSend;
            r.controls.channel (0).volume = volume;
        });
        return (double) fxSendFrom (*rig, settled);
    };
    EXPECT_EQ (mainDc (0.0f, 1.0f), 0.0);
    EXPECT_NEAR (mainDc (1.0f, 1.0f), level * a3::laws::auxSendGain (1.0), 1e-4);
    EXPECT_NEAR (mainDc (0.5f, 1.0f), level * a3::laws::auxSendGain (0.5), 1e-4);
    EXPECT_EQ (mainDc (1.0f, 0.0f), 0.0);   // post-fader
}

// The deck's aux bus is StemDeck's own send: unity into the delay, after the channel's VOLUME.
TEST (Engine, DeckAuxReachesTheDelayThroughTheStrip)
{
    const size_t frames = oneSecond;
    const float level = 0.5f;
    const auto auxDc = [&] (float volume, float auxSend)
    {
        auto rig = deckOnChannelOne (frames, [&] (EngineRig& r)
        {
            r.decks[0].aux[0] = constant (frames, level);
            r.controls.channel (0).volume = volume;
            r.controls.channel (0).auxSend = auxSend;
        });
        return fxSendFrom (*rig, settled);
    };
    EXPECT_NEAR (auxDc (1.0f, 0.0f), level, 1e-4);
    EXPECT_NEAR (auxDc (1.0f, 0.5f), level, 1e-4);
    EXPECT_NEAR (auxDc (1.0f, 1.0f), level, 1e-4);
    EXPECT_EQ (auxDc (0.0f, 0.0f), 0.0f);

    // EQ low fully down: the aux path loses the low band in the same proportion as the main path.
    const auto low = sine (frames, 60.0, engineRate, level).l;
    const auto onAux = [&] (float eqLow)
    {
        auto rig = deckOnChannelOne (frames, [&] (EngineRig& r)
        {
            r.decks[0].aux[0] = low;
            r.controls.channel (0).eqLow = eqLow;
        });
        return (double) fxSendFrom (*rig, settled);
    };
    const auto onMain = [&] (float eqLow)
    {
        auto rig = deckOnChannelOne (frames, [&] (EngineRig& r)
        {
            r.decks[0].bformat[0] = low;
            r.controls.channel (0).eqLow = eqLow;
        });
        rig->run();
        return peak (rig->post[0].l, settled);   // the deck's W after VOLUME
    };
    const double auxRatio = onAux (0.0f) / onAux (0.5f);
    const double mainRatio = onMain (0.0f) / onMain (0.5f);
    EXPECT_LT (auxRatio, 0.5);
    EXPECT_NEAR (auxRatio, mainRatio, 1e-3);
}

TEST (Engine, DeskAuxMovesLeaveTheDeckAuxAlone)
{
    const size_t frames = oneSecond / 2, half = frames / 2;
    const auto sends = [&] (bool closeHalfway)
    {
        auto rig = deckOnChannelOne (frames, [&] (EngineRig& r)
        {
            for (int ch = 0; ch < deckChannels; ++ch)
                r.decks[0].aux[(size_t) ch] = noise (frames, 0.3f, 500 + (uint32_t) ch).l;
        });
        std::vector<float> peaks;
        rig->run (0, 0, { engineBlock }, [&] (size_t start)
        {
            if (closeHalfway && start >= half)
                rig->controls.channel (0).auxSend = 0.0f;
        },
        [&] { peaks.push_back (rig->engine->lastFxSendPeak()); });
        return peaks;
    };
    const auto open = sends (false);
    EXPECT_GT (*std::max_element (open.begin(), open.end()), 1e-2f);
    EXPECT_EQ (sends (true), open);

    // With the deck main playing too: what the send carries beyond main x AUX is the aux bus, at unity.
    const float auxLevel = 0.5f, mainLevel = 0.2f;
    const auto auxShare = [&] (float auxSend)
    {
        auto rig = deckOnChannelOne (oneSecond, [&] (EngineRig& r)
        {
            r.decks[0].aux[0] = constant (oneSecond, auxLevel);
            r.decks[0].bformat[0] = constant (oneSecond, mainLevel);
            r.controls.channel (0).auxSend = auxSend;
        });
        return (double) fxSendFrom (*rig, settled) - mainLevel * a3::laws::auxSendGain (auxSend);
    };
    for (float auxSend : { 0.0f, 0.5f, 1.0f })
        EXPECT_NEAR (auxShare (auxSend), auxLevel, 1e-4) << "AUX " << auxSend;
}

TEST (Engine, DeckOnNoChannelFeedsNoDelay)
{
    const size_t frames = oneSecond / 2;
    const auto run = [&] (int sourceOfChannelTwo)
    {
        auto rig = std::make_unique<EngineRig> (engineConfig (4, 2), frames);
        for (int ch = 0; ch < deckChannels; ++ch)
            rig->decks[1].aux[(size_t) ch] = noise (frames, 0.3f, 600 + (uint32_t) ch).l;
        for (int c = 0; c < 4; ++c)
        {
            rig->neutralStrip (c);
            rig->controls.channel (c).auxSend = 1.0f;
        }
        rig->controls.channel (0).source = 1;   // deck 1, silent
        rig->controls.channel (1).source = sourceOfChannelTwo;
        float worst = 0.0f;
        rig->run (0, 0, { engineBlock }, {}, [&] { worst = std::max (worst, rig->engine->lastFxSendPeak()); });
        return worst;
    };
    EXPECT_EQ (run (0), 0.0f);    // deck 2 on no channel
    EXPECT_GT (run (2), 1e-2f);   // the same deck on channel 2 does feed it
}

TEST (Engine, DeckSwitchDipsTheAuxToo)
{
    const size_t frames = 8192, switchAt = 4096;
    const float levelA = 0.5f, levelB = 0.25f;
    auto rig = deckOnChannelOne (frames, [&] (EngineRig& r)
    {
        r.decks[0].aux[0] = constant (frames, levelA);
        r.decks[1].aux[0] = constant (frames, levelB);
        r.controls.channel (0).auxSend = 0.0f;   // the aux bus alone, unity
    });
    std::vector<float> fx (frames, 0.0f);   // one-sample calls: each call's peak is that sample's |value|
    size_t at = 0;
    rig->run (0, 0, { 1 }, [&] (size_t start)
    {
        at = start;
        rig->controls.channel (0).source = start < switchAt ? 1 : 2;
    },
    [&] { fx[at] = rig->engine->lastFxSendPeak(); });

    EXPECT_NEAR (fx[switchAt - 1], levelA, 1e-4);
    EXPECT_NEAR (fx.back(), levelB, 1e-4);
    EXPECT_LT (*std::min_element (fx.begin() + (long) switchAt, fx.begin() + (long) (switchAt + 1000)), 1e-3f);   // the dip
    EXPECT_LE (maxStep (fx, switchAt - 100), 0.05);
}

TEST (Engine, RecDecoderNeverGetsTheEq)
{
    const size_t frames = oneSecond / 2;
    const auto run = [&] (const char* model)
    {
        auto config = engineConfig (4, 2);
        config.phonesHeadphoneEq = model;
        auto rig = std::make_unique<EngineRig> (config, frames);
        rig->setAnalogPair (1, pinkStereo (frames, 800));
        for (int acn = 0; acn < cueAcns; ++acn)   // the stem cue is what the phones decoder hears
            rig->decks[1].cue[(size_t) acn] = noise (frames, 0.2f, 810 + (uint32_t) acn).l;
        rig->controls.global().phonesMix = 0.0f;
        rig->controls.global().phonesVolume = 1.0f;
        rig->run();
        return rig;
    };
    const auto plain = run ("off"), eq = run ("AKG-K141MK2");
    EXPECT_GT (peak (plain->recOutputs(), afterStartUp), 1e-3);
    EXPECT_EQ (maxDifference (plain->recOutputs(), eq->recOutputs()), 0.0);   // rec parameters unchanged
    EXPECT_GT (peak (stereo (plain->phones), afterStartUp), 1e-3);
    EXPECT_GT (maxDifference (stereo (plain->phones), stereo (eq->phones), afterStartUp), 1e-4);   // phones are filtered
}

// P47: aux_L/aux_R are the FX Return's meter: the delay's output after RETURN, its W on both sides.
// The echo of one click reads the same on both, follows RETURN's gain, and RETURN 0 reads 0.
TEST (FxReturn, AuxMetersReadTheReturnAfterItsLevel)
{
    const auto meterAt = [] (float auxReturn)
    {
        a3::VuTaps taps (truthVuMeters(), a3::VuSettings {});
        EngineRig rig (engineConfig (4, 2), oneSecond);
        rig.engine->attachVuTaps (taps);
        rig.setAnalogPair (1, both (impulseAt (rig.frames, 1000)));
        rig.controls.channel (0).auxSend = 1.0f;
        rig.controls.global().auxReturn = auxReturn;
        std::map<std::string, float> highest;
        rig.run (0, 0, { engineBlock }, {}, [&]
        {
            std::vector<float> peaks, rms;
            taps.take (peaks, rms);
            for (size_t slot = 0; slot < taps.names().size(); ++slot)
                highest[taps.names()[slot]] = std::max (highest[taps.names()[slot]], peaks[slot]);
        });
        return std::pair { highest["aux_L"], highest["aux_R"] };
    };
    const auto unity = meterAt (1.0f), half = meterAt (0.5f), closed = meterAt (0.0f);
    EXPECT_GT (unity.first, 1e-3f);
    EXPECT_EQ (unity.first, unity.second);
    EXPECT_NEAR (half.first / unity.first, a3::laws::stageGain (0.5), 1e-4);
    EXPECT_EQ (closed.first, 0.0f);
    EXPECT_EQ (closed.second, 0.0f);
}

// deck_a ... deck_d: each deck's W as it reaches the engine, before any channel and whether or not a
// channel plays it -- the desk's deck bars (2026-10-28: a one-deck StemDeck sends no stem meters). Every
// channel on analog here: deck B's W still reads its own peak, its other planes and deck A read 0.
TEST (Engine, EachDeckMetersItsWBeforeAnyChannel)
{
    a3::VuTaps taps (truthVuMeters(), a3::VuSettings {});
    EngineRig rig (engineConfig (4, 2), 4096);
    rig.engine->attachVuTaps (taps);
    rig.decks[1].bformat[0] = impulseAt (rig.frames, 100, 0.5f);
    rig.decks[1].bformat[1] = impulseAt (rig.frames, 200, 0.9f);
    rig.run();
    std::vector<float> peaks, rms;
    taps.take (peaks, rms);
    std::map<std::string, float> byName;
    for (size_t slot = 0; slot < taps.names().size(); ++slot)
        byName[taps.names()[slot]] = peaks[slot];
    EXPECT_EQ (byName.at ("deck_a"), 0.0f);
    EXPECT_EQ (byName.at ("deck_b"), 0.5f);
    EXPECT_EQ (byName.at ("deck_c"), 0.0f);
    EXPECT_EQ (byName.at ("deck_d"), 0.0f);
}

// O13: the /vu meters at the points the patchbay fed beat-analyzer. The raw inputs: an impulse into
// analog_3 shows only on analog2_L; one into analog_12 (the old return pair, P45) on no meter. The outputs and
// strip taps: a sine through channel 1 moves in1_pre/post, the main and booth ports and rec, each tap's
// peak exactly what its port carried; tops beyond the layout and the other channels stay 0. The window's
// outputMeters keeps its own peaks: taking them leaves the taps' alone.
TEST (Engine, MetersAtTheAnalyzersPoints)
{
    const auto names = truthVuMeters();
    const auto taken = [] (a3::VuTaps& taps)
    {
        std::vector<float> peaks, rms;
        taps.take (peaks, rms);
        std::map<std::string, std::pair<float, float>> byName;
        for (size_t slot = 0; slot < taps.names().size(); ++slot)
            byName[taps.names()[slot]] = { peaks[slot], rms[slot] };
        return byName;
    };
    const auto startsWith = [] (const std::string& text, const char* prefix) { return text.rfind (prefix, 0) == 0; };

    {
        a3::VuTaps taps (names, a3::VuSettings {});
        EngineRig rig (engineConfig (4, 2), 4096);
        rig.engine->attachVuTaps (taps);
        rig.analog[2] = impulseAt (rig.frames, 100, 1.0f);
        rig.analog[11] = impulseAt (rig.frames, 300, 0.5f);
        rig.run();
        for (const auto& [name, meter] : taken (taps))
        {
            if (! startsWith (name, "analog") && ! startsWith (name, "aux_"))
                continue;
            const float expected = name == "analog2_L" ? 1.0f : 0.0f;
            EXPECT_EQ (meter.first, expected) << name;
        }
    }

    a3::VuTaps taps (names, a3::VuSettings {});
    EngineRig rig (engineConfig (4, 2), oneSecond / 4);
    rig.engine->attachVuTaps (taps);
    rig.setAnalogPair (1, sine (rig.frames, 997.0, engineRate, 0.3f));
    rig.run();
    EXPECT_FALSE (rig.engine->outputMeters().empty());   // the window's reader takes its peaks first
    auto meters = taken (taps);

    const auto expectPort = [&] (const std::string& name, const std::vector<float>& carried)
    {
        EXPECT_EQ ((double) meters.at (name).first, peak (carried)) << name;
    };
    const auto& mainLayout = rig.spatial.main.layout;
    const auto& boothLayout = rig.spatial.booth.layout;
    const auto zone = [&] (const std::string& prefix, const a3::Layout& layout, const std::vector<const std::vector<float>*>& ports)
    {
        size_t port = 0;
        if (layout.hasSub())
            expectPort (prefix + "_sub", *ports[port++]);
        else
            EXPECT_EQ (meters.at (prefix + "_sub").first, 0.0f) << prefix;
        for (size_t top = 1; top <= 9; ++top)
        {
            const auto name = prefix + "_top" + std::to_string (top);
            if (top <= layout.topChannels.size())
                expectPort (name, *ports[port++]);
            else
                EXPECT_EQ (meters.at (name).first, 0.0f) << name;
        }
    };
    zone ("main", mainLayout, rig.mainOutputs());
    zone ("booth", boothLayout, rig.boothOutputs());
    EXPECT_GT (meters.at ("main_top1").first, 1e-3f);
    EXPECT_GT (meters.at ("booth_top1").first, 1e-3f);

    const auto rec = rig.recOutputs();
    expectPort ("rec_L", *rec[0]);
    expectPort ("rec_R", *rec[1]);
    EXPECT_GT (meters.at ("rec_L").first, 1e-3f);
    EXPECT_GT (meters.at ("rec_R").first, 1e-3f);
    expectPort ("phones_L", rig.phones.l);
    expectPort ("phones_R", rig.phones.r);

    for (int c = 1; c <= 4; ++c)
        for (const char* side : { "L", "R" })
        {
            const auto& pre = rig.pre[(size_t) c - 1];
            const auto& post = rig.post[(size_t) c - 1];
            const bool left = side[0] == 'L';
            const auto preName = "in" + std::to_string (c) + "_pre_" + side;
            const auto postName = "in" + std::to_string (c) + "_post_" + side;
            expectPort (preName, left ? pre.l : pre.r);
            expectPort (postName, left ? post.l : post.r);
            if (c == 1)
            {
                EXPECT_GT (meters.at (preName).first, 1e-2f) << preName;
                EXPECT_GT (meters.at (postName).first, 1e-2f) << postName;
                EXPECT_GT (meters.at (preName).second, 1e-3f) << preName;
            }
            else
            {
                EXPECT_EQ (meters.at (preName).first, 0.0f) << preName;
                EXPECT_EQ (meters.at (postName).first, 0.0f) << postName;
            }
        }
    EXPECT_GT (meters.at ("analog1_L").first, 0.29f);
    EXPECT_EQ (meters.at ("analog2_L").first, 0.0f);
}
