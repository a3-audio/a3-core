// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include <algorithm>
#include <stdexcept>
#include "AmbiEncoderBus.h"
#include "ChannelSelect.h"
#include "ChannelStrip.h"
#include "DeckStrip.h"
#include "DelayModulation.h"
#include "EngineStages.h"
#include "Laws.h"

namespace a3
{
namespace
{
using engine::busChannels;
constexpr int sides = 2;

bool isOn (const std::atomic<float>& key) { return key.load (std::memory_order_relaxed) >= 0.5f; }
float load (const std::atomic<float>& value) { return value.load (std::memory_order_relaxed); }

void clear (float* x, int n) { juce::FloatVectorOperations::clear (x, n); }
void multiply (float* x, const float* curve, int n) { juce::FloatVectorOperations::multiply (x, curve, n); }

// dst = a * wa (+ b * wb), per sample
void weighted (float* dst, const float* a, const float* wa, int n)
{
    juce::FloatVectorOperations::multiply (dst, a, wa, n);
}
void addWeighted (float* dst, const float* a, const float* wa, int n)
{
    for (int i = 0; i < n; ++i)
        dst[i] += a[i] * wa[i];
}

float peakOf (const juce::AudioBuffer<float>& bus, int n)
{
    float worst = 0.0f;
    for (int ch = 0; ch < busChannels; ++ch)
        worst = std::max (worst, bus.getMagnitude (ch, 0, n));
    return worst;
}

// Linear ramp from `from` to `to` over `length` samples: the j-th value is from + (to - from) (j + 1) / length.
void fillRamp (float* curve, float from, float to, int length)
{
    if (from == to)
    {
        std::fill (curve, curve + length, to);
        return;
    }
    for (int j = 0; j < length; ++j)
        curve[j] = from + (to - from) * (float) (j + 1) / (float) length;
}
}

// One channel's two paths and its send weights; every plane is one prepared block long.
struct EngineChannel
{
    enum Plane
    {
        movingL, movingR, steadyL, steadyR,                 // post-VOLUME
        movingPreL, movingPreR, steadyPreL, steadyPreR,     // pre-VOLUME
        preL, preR,                                         // after TRIM/EQ
        fxMovingL, fxMovingR, fxSteadyL, fxSteadyR,         // x aux
        phMovingL, phMovingR, phSteadyL, phSteadyR,         // x cue (pre) + x mix (post)
        analogGain, deckGain, auxCurve, cueCurve, mixCurve,
        numPlanes
    };

    int analogPair = 0;   // 1-based; 0 = deck only
    std::unique_ptr<ChannelStrip> strip;
    DeckStrip deck;
    ChannelSelect select;
    WeightRamp aux, cue, mix;
    juce::AudioBuffer<float> planes, deckPre, deckPost;
    bool analogRuns = false, deckRuns = false;   // in the current chunk, with a gain above 0 somewhere

    float* p (Plane plane) { return planes.getWritePointer (plane); }
};

struct Engine::Impl
{
    Impl (const EngineConfig&, const SpatialConfig&);   // throws std::runtime_error

    void prepare (double sampleRate, int blockSize);
    void processChunk (const EngineInputs&, const EngineOutputs&, int offset, int n, EngineControls&);

    void latchGlobals (EngineControls&);
    void runChannel (EngineChannel&, ChannelControls&, const EngineInputs&, int offset, int n);
    void selectPaths (EngineChannel&, const ChannelControls&, const EngineInputs&, int offset, int n);
    void runAnalog (EngineChannel&, const ChannelControls&, const EngineInputs&, int offset, int n);
    void runDeck (EngineChannel&, const ChannelControls&, const EngineInputs&, int slot, int offset, int at, int length);
    void buildSends (EngineChannel&, int n);
    void writeMeters (EngineChannel&, const EngineOutputs&, size_t channel, int offset, int n);
    void runReturn (const EngineInputs&, int offset, int n);
    void encode (int n);
    void addDecks (int n);
    void decode (const EngineInputs&, const EngineOutputs&, int offset, int n);

    const float* analogInput (const EngineInputs&, int index, int offset) const;
    const DeckInputs* deckInputs (const EngineInputs&, int slot) const;

    EngineConfig config;
    FilterControls filter;   // the wire's values, copied per chunk; the strips hold a pointer to it
    std::vector<std::unique_ptr<EngineChannel>> channels;
    std::vector<int> analogChannelOf;   // source index -> channel index
    std::unique_ptr<AmbiEncoderBus> encMain, encFx, encPhones;
    std::unique_ptr<IemNode> dualDelay;      // declared before the tempo, which holds its raw parameters
    std::unique_ptr<DelayTempo> tempo;
    DelayModulation modulation;
    std::unique_ptr<HeadphoneDecoder> rec, phones;
    std::unique_ptr<ZoneDecoder> main, booth;

    ReturnSelect returnSelect;
    GainStage returnStage, phonesStage;
    WeightRamp returnPhones, stemCue;

    std::unique_ptr<std::atomic<float>[]> azimuth, elevation;   // contiguous, one per analog source
    std::vector<StereoIn> mainMoving, mainSteady, fxMoving, fxSteady, phMoving, phSteady;
    std::vector<std::array<const float*, 2>> deckAux;
    juce::AudioBuffer<float> busMain, busFx, busPhones;
    enum ReturnPlane { retL, retR, retPhonesL, retPhonesR, retPhonesCurve, stemCueCurve, numReturnPlanes };
    juce::AudioBuffer<float> returnPlanes;
    std::vector<float> zeros;
    double sampleRate = 44100.0;
    int blockSize = 0;
    float fxSendPeak = 0.0f, analogEncoderPeak = 0.0f;
    int returnSource = 0;
};

Engine::Impl::Impl (const EngineConfig& c, const SpatialConfig& spatial)
    : config (c), modulation (c.delayModulation)
{
    if (config.channels < 1)
        throw std::runtime_error ("channels must be at least 1");
    if ((int) config.analogPairs.size() != config.channels)
        throw std::runtime_error ("analogPairs must list every channel");
    if (config.decks < 0)
        throw std::runtime_error ("decks must not be negative");

    for (int i = 0; i < config.channels; ++i)
    {
        auto channel = std::make_unique<EngineChannel>();
        channel->analogPair = config.analogPairs[(size_t) i];
        if (channel->analogPair > 0)
        {
            channel->strip = std::make_unique<ChannelStrip>();
            channel->strip->setSharedFilter (&filter);
            analogChannelOf.push_back (i);
        }
        channel->deck.setSharedFilter (&filter);
        channels.push_back (std::move (channel));
    }

    const int analogSources = (int) analogChannelOf.size();
    for (auto* bus : { &encMain, &encFx, &encPhones })
    {
        juce::String error;
        *bus = AmbiEncoderBus::create (analogSources, spatial, error);
        if (*bus == nullptr)
            throw std::runtime_error (error.toStdString());
    }
    dualDelay = engine::createNode (IemKind::dualDelay, busChannels, busChannels, "fx");
    engine::require (dualDelay->setParameters (spatial.fxParameters), "fx.parameters.json");
    tempo = std::make_unique<DelayTempo> (*dualDelay);
    rec = std::make_unique<HeadphoneDecoder> (spatial.recParameters, "rec");
    phones = std::make_unique<HeadphoneDecoder> (spatial.phonesParameters, "phones");
    main = std::make_unique<ZoneDecoder> (spatial.main, "main");
    booth = std::make_unique<ZoneDecoder> (spatial.booth, "booth");

    azimuth = std::make_unique<std::atomic<float>[]> ((size_t) analogSources);
    elevation = std::make_unique<std::atomic<float>[]> ((size_t) analogSources);
}

void Engine::Impl::prepare (double rate, int newBlockSize)
{
    sampleRate = rate;
    blockSize = std::max (1, newBlockSize);
    for (auto& channel : channels)
    {
        if (channel->strip != nullptr)
            channel->strip->prepare (rate, blockSize);
        channel->deck.prepare (rate, blockSize);
        channel->select.prepare (rate, config.decks, channel->analogPair > 0);
        for (auto* ramp : { &channel->aux, &channel->cue, &channel->mix })
            ramp->prepare (rate);
        channel->planes.setSize (EngineChannel::numPlanes, blockSize);
        channel->deckPre.setSize (DeckStrip::channels, blockSize);
        channel->deckPost.setSize (DeckStrip::channels, blockSize);
        channel->planes.clear();
        channel->deckPre.clear();
        channel->deckPost.clear();
    }

    const auto stereo = [] (EngineChannel& ch, EngineChannel::Plane left)
    {
        return StereoIn { ch.p (left), ch.p ((EngineChannel::Plane) (left + 1)) };
    };
    for (auto* list : { &mainMoving, &mainSteady, &fxMoving, &fxSteady, &phMoving, &phSteady })
        list->clear();
    for (int index : analogChannelOf)
    {
        auto& ch = *channels[(size_t) index];
        mainMoving.push_back (stereo (ch, EngineChannel::movingL));
        mainSteady.push_back (stereo (ch, EngineChannel::steadyL));
        fxMoving.push_back (stereo (ch, EngineChannel::fxMovingL));
        fxSteady.push_back (stereo (ch, EngineChannel::fxSteadyL));
        phMoving.push_back (stereo (ch, EngineChannel::phMovingL));
        phSteady.push_back (stereo (ch, EngineChannel::phSteadyL));
    }
    deckAux.assign ((size_t) config.decks, { nullptr, nullptr });

    for (auto* bus : { &busMain, &busFx, &busPhones })
        bus->setSize (busChannels, blockSize);
    returnPlanes.setSize (numReturnPlanes, blockSize);
    zeros.assign ((size_t) blockSize, 0.0f);

    for (auto* bus : { encMain.get(), encFx.get(), encPhones.get() })
        bus->prepare (rate, blockSize);
    dualDelay->prepare (rate, blockSize);
    rec->prepare (rate, blockSize);
    phones->prepare (rate, blockSize);
    main->prepare (rate, blockSize);
    booth->prepare (rate, blockSize);
    returnSelect.prepare (rate);
    returnStage.prepare (rate);
    phonesStage.prepare (rate);
    returnPhones.prepare (rate);
    stemCue.prepare (rate);
}

void Engine::Impl::processChunk (const EngineInputs& in, const EngineOutputs& out, int offset, int n, EngineControls& controls)
{
    latchGlobals (controls);
    for (size_t c = 0; c < channels.size(); ++c)
    {
        auto& channel = *channels[c];
        runChannel (channel, controls.channel ((int) c), in, offset, n);
        writeMeters (channel, out, c, offset, n);
    }
    runReturn (in, offset, n);
    encode (n);
    addDecks (n);
    fxSendPeak = std::max (fxSendPeak, peakOf (busFx, n));
    engine::runNode (*dualDelay, busFx, n);
    decode (in, out, offset, n);
}

void Engine::Impl::latchGlobals (EngineControls& controls)
{
    auto& g = controls.global();
    filter.kind.store (g.filter.kind.load (std::memory_order_relaxed), std::memory_order_relaxed);
    filter.mode.store (g.filter.mode.load (std::memory_order_relaxed), std::memory_order_relaxed);
    filter.frequency.store (load (g.filter.frequency), std::memory_order_relaxed);
    filter.resonance.store (load (g.filter.resonance), std::memory_order_relaxed);

    const float mix = load (g.phonesMix);
    for (size_t c = 0; c < channels.size(); ++c)
    {
        auto& channel = *channels[c];
        const auto& wire = controls.channel ((int) c);
        channel.aux.setTarget ((float) laws::auxSendGain (load (wire.auxSend)));
        channel.cue.setTarget (isOn (wire.cue) ? (float) laws::cueWeight (mix) : 0.0f);
        channel.mix.setTarget ((float) laws::mixWeight (mix));
    }
    returnSource = g.returnSource.load (std::memory_order_relaxed);
    returnPhones.setTarget ((float) (laws::mixWeight (mix) + (isOn (g.returnCue) ? laws::cueWeight (mix) : 0.0)));
    stemCue.setTarget ((float) laws::cueWeight (mix));
    returnStage.set (load (g.auxReturn));
    phonesStage.set (load (g.phonesVolume));
    main->setLevel (load (g.masterVolume));
    booth->setLevel (load (g.booth));
    tempo->set (load (g.delayBpm));

    for (size_t s = 0; s < analogChannelOf.size(); ++s)
    {
        auto& ch = controls.channel (analogChannelOf[s]);
        azimuth[s].store (load (ch.azimuth), std::memory_order_relaxed);
        elevation[s].store (load (ch.elevation), std::memory_order_relaxed);
    }
}

void Engine::Impl::runChannel (EngineChannel& ch, ChannelControls& controls, const EngineInputs& in, int offset, int n)
{
    selectPaths (ch, controls, in, offset, n);
    runAnalog (ch, controls, in, offset, n);
    buildSends (ch, n);
}

// Walks the select's linear segments: the gain curves for both paths, and the deck
// path run segment by segment (it may change slot or be reset between them).
void Engine::Impl::selectPaths (EngineChannel& ch, const ChannelControls& controls, const EngineInputs& in, int offset, int n)
{
    const int source = controls.source.load (std::memory_order_relaxed);
    ch.analogRuns = ch.deckRuns = false;
    for (int done = 0; done < n;)
    {
        const auto s = ch.select.advance (source, n - done);
        const int length = s.length > 0 ? s.length : n - done;
        fillRamp (ch.p (EngineChannel::analogGain) + done, s.analogFrom, s.analogTo, length);
        fillRamp (ch.p (EngineChannel::deckGain) + done, s.deckFrom, s.deckTo, length);
        ch.analogRuns = ch.analogRuns || (ch.strip != nullptr && (s.analogFrom > 0.0f || s.analogTo > 0.0f));
        if (s.deckFrom > 0.0f || s.deckTo > 0.0f)
        {
            if (s.resetDeck)
                ch.deck.reset();
            runDeck (ch, controls, in, s.deckSlot, offset, done, length);
            ch.deckRuns = true;
        }
        else
        {
            for (auto* taps : { &ch.deckPre, &ch.deckPost })
                for (int k = 0; k < DeckStrip::channels; ++k)
                    clear (taps->getWritePointer (k) + done, length);
        }
        done += length;
    }
    if (! ch.deckRuns)
        return;
    for (auto* taps : { &ch.deckPre, &ch.deckPost })
        for (int k = 0; k < DeckStrip::channels; ++k)
            multiply (taps->getWritePointer (k), ch.p (EngineChannel::deckGain), n);
}

void Engine::Impl::runDeck (EngineChannel& ch, const ChannelControls& controls, const EngineInputs& in, int slot,
                            int offset, int at, int length)
{
    const auto* deck = deckInputs (in, slot);
    const float* in16[DeckStrip::channels];
    DeckTaps taps;
    for (int k = 0; k < DeckStrip::channels; ++k)
    {
        const float* plane = deck != nullptr ? deck->bformat[(size_t) k] : nullptr;
        in16[k] = plane != nullptr ? plane + offset + at : zeros.data();
        taps.preVolume[k] = ch.deckPre.getWritePointer (k) + at;
        taps.postVolume[k] = ch.deckPost.getWritePointer (k) + at;
    }
    ch.deck.process (in16, taps, length, controls);
}

// The analog strip runs whenever the channel has a pair, so it never needs a reset;
// while its select gain is 0 its taps are cleared and the encoder sources are silent.
void Engine::Impl::runAnalog (EngineChannel& ch, const ChannelControls& controls, const EngineInputs& in, int offset, int n)
{
    if (ch.strip == nullptr)
        return;
    auto& state = ch.strip->controls();
    for (auto [to, from] : { std::pair { &state.gain, &controls.gain }, { &state.volume, &controls.volume },
                             { &state.eqHigh, &controls.eqHigh }, { &state.eqMid, &controls.eqMid },
                             { &state.eqLow, &controls.eqLow }, { &state.frequency, &controls.frequency },
                             { &state.q, &controls.q }, { &state.threeD, &controls.threeD },
                             { &state.filterOn, &controls.filterOn } })
        to->store (load (*from), std::memory_order_relaxed);

    const int first = 2 * ch.analogPair - 2;
    const float* input[2] { analogInput (in, first, offset), analogInput (in, first + 1, offset) };
    StripBuffers out { { ch.p (EngineChannel::movingL), ch.p (EngineChannel::movingR) },
                       { ch.p (EngineChannel::steadyL), ch.p (EngineChannel::steadyR) },
                       { ch.p (EngineChannel::movingPreL), ch.p (EngineChannel::movingPreR) },
                       { ch.p (EngineChannel::steadyPreL), ch.p (EngineChannel::steadyPreR) },
                       { ch.p (EngineChannel::preL), ch.p (EngineChannel::preR) } };
    ch.strip->process (input, out, n);

    for (int plane = EngineChannel::movingL; plane <= EngineChannel::preR; ++plane)
    {
        if (ch.analogRuns)
            multiply (ch.p ((EngineChannel::Plane) plane), ch.p (EngineChannel::analogGain), n);
        else
            clear (ch.p ((EngineChannel::Plane) plane), n);
    }
}

// fx = post x aux; phones = pre x cue + post x mix. A silent weight writes exact zeros.
void Engine::Impl::buildSends (EngineChannel& ch, int n)
{
    using P = EngineChannel;
    ch.aux.render (ch.p (P::auxCurve), n);
    ch.cue.render (ch.p (P::cueCurve), n);
    ch.mix.render (ch.p (P::mixCurve), n);
    if (ch.strip == nullptr)
        return;

    const bool auxSilent = ch.aux.renderedSilence();
    for (int k = 0; k < 4; ++k)
    {
        auto* fx = ch.p ((P::Plane) (P::fxMovingL + k));
        if (auxSilent || ! ch.analogRuns)
            clear (fx, n);
        else
            weighted (fx, ch.p ((P::Plane) (P::movingL + k)), ch.p (P::auxCurve), n);

        auto* ph = ch.p ((P::Plane) (P::phMovingL + k));
        clear (ph, n);
        if (! ch.analogRuns)
            continue;
        if (! (ch.cue.renderedSilence()))
            addWeighted (ph, ch.p ((P::Plane) (P::movingPreL + k)), ch.p (P::cueCurve), n);
        if (! (ch.mix.renderedSilence()))
            addWeighted (ph, ch.p ((P::Plane) (P::movingL + k)), ch.p (P::mixCurve), n);
    }
}

// pre/post of the active path: analog as Plan 2, a deck's W on both sides, during a crossfade the sum.
void Engine::Impl::writeMeters (EngineChannel& ch, const EngineOutputs& out, size_t channel, int offset, int n)
{
    using P = EngineChannel;
    const auto write = [&] (const std::vector<std::array<float*, 2>>& taps, P::Plane analogLeft, const juce::AudioBuffer<float>& deck,
                            P::Plane analogSecondLeft)
    {
        if (channel >= taps.size())
            return;
        for (int side = 0; side < sides; ++side)
        {
            float* dst = taps[channel][(size_t) side];
            if (dst == nullptr)
                continue;
            dst += offset;
            clear (dst, n);
            if (ch.analogRuns)
            {
                juce::FloatVectorOperations::add (dst, ch.p ((P::Plane) (analogLeft + side)), n);
                if (analogSecondLeft != analogLeft)
                    juce::FloatVectorOperations::add (dst, ch.p ((P::Plane) (analogSecondLeft + side)), n);
            }
            if (ch.deckRuns)
                juce::FloatVectorOperations::add (dst, deck.getReadPointer (0), n);
        }
    };
    write (out.pre, P::preL, ch.deckPre, P::preL);
    write (out.post, P::movingL, ch.deckPost, P::steadyL);
}

void Engine::Impl::runReturn (const EngineInputs& in, int offset, int n)
{
    for (size_t k = 0; k < deckAux.size(); ++k)
    {
        const auto* deck = deckInputs (in, (int) k + 1);
        deckAux[k] = { deck != nullptr && deck->aux[0] != nullptr ? deck->aux[0] + offset : nullptr,
                       deck != nullptr && deck->aux[1] != nullptr ? deck->aux[1] + offset : nullptr };
    }
    const int first = 2 * config.returnAnalogPair - 2;
    const float* analog[2] { analogInput (in, first, offset), analogInput (in, first + 1, offset) };
    float* ret[2] { returnPlanes.getWritePointer (retL), returnPlanes.getWritePointer (retR) };
    returnSelect.process (analog, deckAux, ret, n, returnSource);
    returnStage.process (ret, n, sides);

    returnPhones.render (returnPlanes.getWritePointer (retPhonesCurve), n);
    for (int side = 0; side < sides; ++side)
        weighted (returnPlanes.getWritePointer (retPhonesL + side), ret[side], returnPlanes.getReadPointer (retPhonesCurve), n);
}

void Engine::Impl::encode (int n)
{
    const StereoIn ret { returnPlanes.getReadPointer (retL), returnPlanes.getReadPointer (retR) };
    const StereoIn retPhones { returnPlanes.getReadPointer (retPhonesL), returnPlanes.getReadPointer (retPhonesR) };
    encMain->process (mainMoving, mainSteady, ret, busMain, n, azimuth.get(), elevation.get());
    analogEncoderPeak = std::max (analogEncoderPeak, peakOf (busMain, n));
    encFx->process (fxMoving, fxSteady, {}, busFx, n, azimuth.get(), elevation.get());
    encPhones->process (phMoving, phSteady, retPhones, busPhones, n, azimuth.get(), elevation.get());
}

// R11: a deck channel's 16 post-VOLUME channels go straight into the buses, no encoder.
void Engine::Impl::addDecks (int n)
{
    using P = EngineChannel;
    for (auto& channel : channels)
    {
        auto& ch = *channel;
        if (! ch.deckRuns)
            continue;
        const bool auxOpen = ! (ch.aux.renderedSilence());
        const bool cueOpen = ! (ch.cue.renderedSilence());
        const bool mixOpen = ! (ch.mix.renderedSilence());
        for (int k = 0; k < busChannels; ++k)
        {
            const float* post = ch.deckPost.getReadPointer (k);
            juce::FloatVectorOperations::add (busMain.getWritePointer (k), post, n);
            if (auxOpen)
                addWeighted (busFx.getWritePointer (k), post, ch.p (P::auxCurve), n);
            if (cueOpen)
                addWeighted (busPhones.getWritePointer (k), ch.deckPre.getReadPointer (k), ch.p (P::cueCurve), n);
            if (mixOpen)
                addWeighted (busPhones.getWritePointer (k), post, ch.p (P::mixCurve), n);
        }
    }
}

void Engine::Impl::decode (const EngineInputs& in, const EngineOutputs& out, int offset, int n)
{
    for (int k = 0; k < busChannels; ++k)
    {
        juce::FloatVectorOperations::add (busMain.getWritePointer (k), busFx.getReadPointer (k), n);
        juce::FloatVectorOperations::add (busPhones.getWritePointer (k), busFx.getReadPointer (k), n);
    }
    main->process (busMain, out.main, offset, n);
    booth->process (busMain, out.booth, offset, n);

    rec->process (busMain, n);
    for (int side = 0; side < sides; ++side)
        if (out.rec[side] != nullptr)
            juce::FloatVectorOperations::copy (out.rec[side] + offset, rec->side (side), n);

    // R13: every deck's stem cue, after the decoder and before PHONES.
    phones->process (busPhones, n);
    stemCue.render (returnPlanes.getWritePointer (stemCueCurve), n);
    if (! (stemCue.renderedSilence()))
        for (int slot = 1; slot <= config.decks; ++slot)
            if (const auto* deck = deckInputs (in, slot))
                for (int side = 0; side < sides; ++side)
                    if (deck->cue[(size_t) side] != nullptr)
                        addWeighted (phones->side (side), deck->cue[(size_t) side] + offset,
                                     returnPlanes.getReadPointer (stemCueCurve), n);
    float* heads[2] { phones->side (0), phones->side (1) };
    phonesStage.process (heads, n, sides);
    for (int side = 0; side < sides; ++side)
        if (out.phones[side] != nullptr)
            juce::FloatVectorOperations::copy (out.phones[side] + offset, heads[side], n);
}

const float* Engine::Impl::analogInput (const EngineInputs& in, int index, int offset) const
{
    if (index < 0 || index >= (int) in.analog.size() || in.analog[(size_t) index] == nullptr)
        return zeros.data();
    return in.analog[(size_t) index] + offset;
}

const DeckInputs* Engine::Impl::deckInputs (const EngineInputs& in, int slot) const
{
    if (slot < 1 || slot > config.decks || slot > (int) in.decks.size())
        return nullptr;
    return &in.decks[(size_t) slot - 1];
}

Engine::Engine (std::unique_ptr<Impl> i) : impl (std::move (i)) {}
Engine::~Engine() = default;

std::unique_ptr<Engine> Engine::create (const EngineConfig& config, const SpatialConfig& spatial, juce::String& error)
{
    try
    {
        return std::unique_ptr<Engine> (new Engine (std::make_unique<Impl> (config, spatial)));
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return nullptr;
    }
}

void Engine::prepare (double sampleRate, int blockSize) { impl->prepare (sampleRate, blockSize); }
int Engine::preparedBlockSize() const { return impl->blockSize; }

void Engine::process (const EngineInputs& in, const EngineOutputs& out, int numSamples, EngineControls& controls)
{
    jassert (impl->blockSize > 0 && controls.channels() >= (int) impl->channels.size());
    if (impl->blockSize <= 0 || controls.channels() < (int) impl->channels.size())
        return;
    juce::ScopedNoDenormals noDenormals;
    impl->fxSendPeak = impl->analogEncoderPeak = 0.0f;
    for (int offset = 0; offset < numSamples; offset += impl->blockSize)
        impl->processChunk (in, out, offset, std::min (impl->blockSize, numSamples - offset), controls);
}

void Engine::serviceControls (double seconds) { impl->modulation.tick (seconds, *impl->dualDelay); }
int Engine::analogSourceCount() const { return (int) impl->analogChannelOf.size(); }
float Engine::lastFxSendPeak() const { return impl->fxSendPeak; }
float Engine::lastAnalogEncoderPeak() const { return impl->analogEncoderPeak; }
IemNode& Engine::delay() { return *impl->dualDelay; }
}
