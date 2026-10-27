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
        meterPreL, meterPreR, meterPostL, meterPostR,       // what the pre/post ports carry
        numPlanes
    };

    int analogPair = 0;   // 1-based; 0 = deck only
    std::unique_ptr<ChannelStrip> strip;
    DeckStrip deck, deckAux;   // deckAux: the deck's aux bus through the same controls, into the delay
    ChannelSelect select;
    WeightRamp aux, cue, mix;
    juce::AudioBuffer<float> planes, deckPre, deckPost, deckAuxPost;
    bool analogRuns = false, deckRuns = false;   // in the current chunk, with a gain above 0 somewhere
    int analogStart = 0;                         // where in the chunk the analog path starts to run
    bool resetAnalog = false;                    // it fades in from 0 there (R14)
    int preMeter = 0, postMeter = 0;             // PortMeters groups

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
    void addStemCues (const EngineInputs&, int offset, int n);
    void decode (const EngineInputs&, const EngineOutputs&, int offset, int n);
    void pushMeters (int n);
    void buildMeters();

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
    std::array<OutputGate, numGatedZones> gates;   // GatedZone order
    bool gateOpen = false;
    std::atomic<bool> heldClosed { false };   // the host's fade before a swap; Core's gateOpen is left as it is
    float recGain = 1.0f;
    PortMeters meters;
    std::vector<int> mainMeters, boothMeters;
    int recMeter = 0, phonesMeter = 0;

    ReturnSelect returnSelect;
    GainStage returnStage, phonesStage;
    WeightRamp returnPhones, stemCue;

    std::unique_ptr<std::atomic<float>[]> azimuth, elevation;   // contiguous, one per analog source
    std::vector<StereoIn> mainMoving, mainSteady, fxMoving, fxSteady, phMoving, phSteady;
    juce::AudioBuffer<float> busMain, busFx, busPhones;
    enum ReturnPlane { retL, retR, retPhonesL, retPhonesR, retPhonesCurve, stemCueCurve, delayCurve, returnGainCurve, numReturnPlanes };
    juce::AudioBuffer<float> returnPlanes;
    std::vector<float> zeros;
    double sampleRate = 44100.0;
    int blockSize = 0;
    float fxSendPeak = 0.0f, analogEncoderPeak = 0.0f;   // this call's, published below at its end
    std::atomic<float> lastFxSendPeak { 0.0f }, lastAnalogEncoderPeak { 0.0f };
    int returnSource = 0;
    bool delayAtUnity = true;   // this chunk's delay gain is 1 throughout (the return is on analog)
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
        channel->deckAux.setSharedFilter (&filter);
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

    gates[(size_t) GatedZone::main].setTrim (config.trims.main);
    gates[(size_t) GatedZone::booth].setTrim (config.trims.booth);
    gates[(size_t) GatedZone::phones].setTrim (config.trims.phones);
    recGain = juce::Decibels::decibelsToGain (config.trims.rec, -1000.0f);
    buildMeters();
}

// Port order: main, booth (sub?, tops), rec L/R, phones L/R, per channel pre L/R, post L/R.
void Engine::Impl::buildMeters()
{
    for (auto [zone, groups] : { std::pair { main.get(), &mainMeters }, { booth.get(), &boothMeters } })
        for (int port = 0; port < zone->ports(); port += 2)
            groups->push_back (meters.addGroup (std::min (2, zone->ports() - port)));
    recMeter = meters.addGroup (2);
    phonesMeter = meters.addGroup (2);
    for (auto& channel : channels)
    {
        channel->preMeter = meters.addGroup (2);
        channel->postMeter = meters.addGroup (2);
    }
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
        channel->deckAux.prepare (rate, blockSize);
        channel->select.prepare (rate, config.decks, channel->analogPair > 0);
        for (auto* ramp : { &channel->aux, &channel->cue, &channel->mix })
            ramp->prepare (rate);
        channel->planes.setSize (EngineChannel::numPlanes, blockSize);
        channel->deckPre.setSize (DeckStrip::channels, blockSize);
        channel->deckPost.setSize (DeckStrip::channels, blockSize);
        channel->deckAuxPost.setSize (DeckStrip::channels, blockSize);
        channel->planes.clear();
        channel->deckPre.clear();
        channel->deckPost.clear();
        channel->deckAuxPost.clear();
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
    for (auto& gate : gates)
        gate.prepare (rate);   // closed
    meters.prepare (rate);
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
    if (! delayAtUnity)
        for (int k = 0; k < busChannels; ++k)
            multiply (busFx.getWritePointer (k), returnPlanes.getReadPointer (delayCurve), n);
    decode (in, out, offset, n);
    pushMeters (n);
}

void Engine::Impl::latchGlobals (EngineControls& controls)
{
    auto& g = controls.global();
    gateOpen = g.gateOpen.load (std::memory_order_relaxed) && ! heldClosed.load (std::memory_order_relaxed);
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
        if (ch.strip != nullptr && ! ch.analogRuns && (s.analogFrom > 0.0f || s.analogTo > 0.0f))
        {
            ch.analogRuns = true;
            ch.analogStart = done;
            ch.resetAnalog = s.resetAnalog;
        }
        if (s.deckFrom > 0.0f || s.deckTo > 0.0f)
        {
            if (s.resetDeck)
            {
                ch.deck.reset();
                ch.deckAux.reset();
            }
            runDeck (ch, controls, in, s.deckSlot, offset, done, length);
            ch.deckRuns = true;
        }
        else
        {
            for (auto* taps : { &ch.deckPre, &ch.deckPost, &ch.deckAuxPost })
                for (int k = 0; k < DeckStrip::channels; ++k)
                    clear (taps->getWritePointer (k) + done, length);
        }
        done += length;
    }
    if (! ch.deckRuns)
        return;
    for (auto* taps : { &ch.deckPre, &ch.deckPost, &ch.deckAuxPost })
        for (int k = 0; k < DeckStrip::channels; ++k)
            multiply (taps->getWritePointer (k), ch.p (EngineChannel::deckGain), n);
}

void Engine::Impl::runDeck (EngineChannel& ch, const ChannelControls& controls, const EngineInputs& in, int slot,
                            int offset, int at, int length)
{
    const auto* deck = deckInputs (in, slot);
    const float* in16[DeckStrip::channels];
    const float* aux16[DeckStrip::channels];
    DeckTaps taps, auxTaps {};
    const auto planeAt = [&] (const float* plane) { return plane != nullptr ? plane + offset + at : zeros.data(); };
    for (int k = 0; k < DeckStrip::channels; ++k)
    {
        in16[k] = planeAt (deck != nullptr ? deck->bformat[(size_t) k] : nullptr);
        aux16[k] = planeAt (deck != nullptr ? deck->aux[(size_t) k] : nullptr);
        taps.preVolume[k] = ch.deckPre.getWritePointer (k) + at;
        taps.postVolume[k] = ch.deckPost.getWritePointer (k) + at;
        auxTaps.postVolume[k] = ch.deckAuxPost.getWritePointer (k) + at;
    }
    ch.deck.process (in16, taps, length, controls);
    ch.deckAux.process (aux16, auxTaps, length, controls);
}

// R14: the analog strip runs only from where its select gain leaves 0 (reset first when
// it fades in from 0); before that, and while it stays at 0, its taps are exact zeros and
// the encoder sources silent.
void Engine::Impl::runAnalog (EngineChannel& ch, const ChannelControls& controls, const EngineInputs& in, int offset, int n)
{
    using P = EngineChannel;
    if (ch.strip == nullptr)
        return;
    const int at = ch.analogRuns ? ch.analogStart : n;
    for (int plane = P::movingL; plane <= P::preR; ++plane)
        clear (ch.p ((P::Plane) plane), at);
    if (! ch.analogRuns)
        return;
    if (ch.resetAnalog)
        ch.strip->reset();

    auto& state = ch.strip->controls();
    for (auto [to, from] : { std::pair { &state.gain, &controls.gain }, { &state.volume, &controls.volume },
                             { &state.eqHigh, &controls.eqHigh }, { &state.eqMid, &controls.eqMid },
                             { &state.eqLow, &controls.eqLow }, { &state.frequency, &controls.frequency },
                             { &state.q, &controls.q }, { &state.threeD, &controls.threeD },
                             { &state.filterOn, &controls.filterOn } })
        to->store (load (*from), std::memory_order_relaxed);

    const int first = 2 * ch.analogPair - 2;
    const float* input[2] { analogInput (in, first, offset + at), analogInput (in, first + 1, offset + at) };
    const auto plane = [&] (P::Plane p) { return ch.p (p) + at; };
    StripBuffers out { { plane (P::movingL), plane (P::movingR) }, { plane (P::steadyL), plane (P::steadyR) },
                       { plane (P::movingPreL), plane (P::movingPreR) }, { plane (P::steadyPreL), plane (P::steadyPreR) },
                       { plane (P::preL), plane (P::preR) } };
    ch.strip->process (input, out, n - at);

    for (int p = P::movingL; p <= P::preR; ++p)
        multiply (plane ((P::Plane) p), plane (P::analogGain), n - at);
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
    const auto write = [&] (P::Plane meterLeft, P::Plane analogLeft, P::Plane analogSecondLeft, const juce::AudioBuffer<float>& deck,
                            const std::vector<std::array<float*, 2>>& taps)
    {
        for (int side = 0; side < sides; ++side)
        {
            float* dst = ch.p ((P::Plane) (meterLeft + side));
            clear (dst, n);
            if (ch.analogRuns)
            {
                juce::FloatVectorOperations::add (dst, ch.p ((P::Plane) (analogLeft + side)), n);
                if (analogSecondLeft != analogLeft)
                    juce::FloatVectorOperations::add (dst, ch.p ((P::Plane) (analogSecondLeft + side)), n);
            }
            if (ch.deckRuns)
                juce::FloatVectorOperations::add (dst, deck.getReadPointer (0), n);
            if (channel < taps.size() && taps[channel][(size_t) side] != nullptr)
                juce::FloatVectorOperations::copy (taps[channel][(size_t) side] + offset, dst, n);
        }
    };
    write (P::meterPreL, P::preL, P::preL, ch.deckPre, out.pre);
    write (P::meterPostL, P::movingL, P::steadyL, ch.deckPost, out.post);
}

void Engine::Impl::runReturn (const EngineInputs& in, int offset, int n)
{
    const int first = 2 * config.returnAnalogPair - 2;
    const float* analog[2] { analogInput (in, first, offset), analogInput (in, first + 1, offset) };
    float* ret[2] { returnPlanes.getWritePointer (retL), returnPlanes.getWritePointer (retR) };
    float* delay = returnPlanes.getWritePointer (delayCurve);
    float* returnGain = returnPlanes.getWritePointer (returnGainCurve);
    returnSelect.process (analog, ret, delay, n, returnSource);
    // RETURN's gain curve rides through the stage as a third channel of ones, so the delay
    // follows the very same ramp and silence as the analog pair.
    std::fill (returnGain, returnGain + n, 1.0f);
    float* staged[3] { ret[0], ret[1], returnGain };
    returnStage.process (staged, n, 3);
    delayAtUnity = juce::FloatVectorOperations::findMaximum (delay, n) == 0.0f;
    if (! delayAtUnity)
        for (int i = 0; i < n; ++i)
            delay[i] = (1.0f - delay[i]) + delay[i] * returnGain[i];   // R1: 1 on analog, RETURN on delay

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

// R11: a deck channel's 16 post-VOLUME channels go straight into the buses, no encoder;
// the delay gets the deck's aux bus through its own strip instead (design 4.2).
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
                addWeighted (busFx.getWritePointer (k), ch.deckAuxPost.getReadPointer (k), ch.p (P::auxCurve), n);
            if (cueOpen)
                addWeighted (busPhones.getWritePointer (k), ch.deckPre.getReadPointer (k), ch.p (P::cueCurve), n);
            if (mixOpen)
                addWeighted (busPhones.getWritePointer (k), post, ch.p (P::mixCurve), n);
        }
    }
}

// Every present deck's stem cue (2nd-order B-format, ACN 0..8) joins the phones bus ahead of its decoder,
// so it shares the HRTF and PHONES with the desk's cue. ACN 9..15 stay as they are.
void Engine::Impl::addStemCues (const EngineInputs& in, int offset, int n)
{
    stemCue.render (returnPlanes.getWritePointer (stemCueCurve), n);
    if (stemCue.renderedSilence())
        return;
    for (int slot = 1; slot <= config.decks; ++slot)
    {
        const auto* deck = deckInputs (in, slot);
        if (deck == nullptr)
            continue;
        for (size_t acn = 0; acn < deck->cue.size(); ++acn)
            if (deck->cue[acn] != nullptr)
                addWeighted (busPhones.getWritePointer ((int) acn), deck->cue[acn] + offset,
                             returnPlanes.getReadPointer (stemCueCurve), n);
    }
}

void Engine::Impl::decode (const EngineInputs& in, const EngineOutputs& out, int offset, int n)
{
    for (int k = 0; k < busChannels; ++k)
    {
        juce::FloatVectorOperations::add (busMain.getWritePointer (k), busFx.getReadPointer (k), n);
        juce::FloatVectorOperations::add (busPhones.getWritePointer (k), busFx.getReadPointer (k), n);
    }
    addStemCues (in, offset, n);
    main->process (busMain, out.main, offset, n, gates[(size_t) GatedZone::main], gateOpen);
    booth->process (busMain, out.booth, offset, n, gates[(size_t) GatedZone::booth], gateOpen);

    rec->process (busMain, n);   // trims.rec, never gated (Scope 7)
    for (int side = 0; side < sides; ++side)
    {
        if (recGain != 1.0f)
            juce::FloatVectorOperations::multiply (rec->side (side), recGain, n);
        if (out.rec[side] != nullptr)
            juce::FloatVectorOperations::copy (out.rec[side] + offset, rec->side (side), n);
    }

    phones->process (busPhones, n);
    float* heads[2] { phones->side (0), phones->side (1) };
    phonesStage.process (heads, n, sides);
    gates[(size_t) GatedZone::phones].process (heads, sides, n, gateOpen);
    for (int side = 0; side < sides; ++side)
        if (out.phones[side] != nullptr)
            juce::FloatVectorOperations::copy (out.phones[side] + offset, heads[side], n);
}

void Engine::Impl::pushMeters (int n)
{
    for (auto [zone, groups] : { std::pair { main.get(), &mainMeters }, { booth.get(), &boothMeters } })
        for (size_t g = 0; g < groups->size(); ++g)
        {
            const int port = 2 * (int) g;
            meters.push ((*groups)[g], zone->port (port), port + 1 < zone->ports() ? zone->port (port + 1) : nullptr, n);
        }
    meters.push (recMeter, rec->side (0), rec->side (1), n);
    meters.push (phonesMeter, phones->side (0), phones->side (1), n);
    using P = EngineChannel;
    for (auto& channel : channels)
    {
        meters.push (channel->preMeter, channel->p (P::meterPreL), channel->p (P::meterPreR), n);
        meters.push (channel->postMeter, channel->p (P::meterPostL), channel->p (P::meterPostR), n);
    }
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
    impl->lastFxSendPeak.store (impl->fxSendPeak, std::memory_order_relaxed);
    impl->lastAnalogEncoderPeak.store (impl->analogEncoderPeak, std::memory_order_relaxed);
}

void Engine::serviceControls (double seconds) { impl->modulation.tick (seconds, *impl->dualDelay); }
GateLevels Engine::gateLevels() const
{
    GateLevels levels;
    for (int z = 0; z < numGatedZones; ++z)
        levels.gain[z] = impl->gates[(size_t) z].currentGain();
    return levels;
}

void Engine::holdGateClosed (bool held) { impl->heldClosed.store (held, std::memory_order_relaxed); }

void Engine::startGateAt (const GateLevels& levels)
{
    for (int z = 0; z < numGatedZones; ++z)
        impl->gates[(size_t) z].startAt (levels.gain[z]);
}

std::vector<Engine::OutputMeter> Engine::outputMeters()
{
    std::vector<OutputMeter> list ((size_t) impl->meters.ports());
    for (int port = 0; port < impl->meters.ports(); ++port)
        list[(size_t) port] = { impl->meters.takePeak (port), impl->meters.rms (port) };
    return list;
}

int Engine::analogSourceCount() const { return (int) impl->analogChannelOf.size(); }
float Engine::lastFxSendPeak() const { return impl->lastFxSendPeak.load (std::memory_order_relaxed); }
float Engine::lastAnalogEncoderPeak() const { return impl->lastAnalogEncoderPeak.load (std::memory_order_relaxed); }
IemNode& Engine::delay() { return *impl->dualDelay; }
}
