// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"
#include <algorithm>
#include <stdexcept>
#include "AmbiEncoderBus.h"
#include "ChannelSelect.h"
#include "ChannelStrip.h"
#include "DeckLetters.h"
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

// The EQ is part of the decoder's parameters at build time: IEM loads its impulse response on the
// thread that sets the parameter, so it never happens on the audio thread.
juce::var withHeadphoneEq (const juce::var& phonesParameters, const juce::String& model)
{
    const auto choice = headphoneEqChoice (model);
    if (! choice)
        throw std::runtime_error ("unknown headphone model '" + model.toStdString() + "'");
    auto parameters = phonesParameters.clone();
    parameters.getDynamicObject()->setProperty ("applyHeadphoneEq", *choice);
    return parameters;
}

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

// The number between `prefix` and `rest` in a meter name ("in3_pre_L" with "in", "_pre_L" is 3);
// 0 when the name is not of that form.
int numberIn (const std::string& name, const std::string& prefix, const std::string& rest)
{
    if (name.size() <= prefix.size() + rest.size() || name.compare (0, prefix.size(), prefix) != 0
        || name.compare (name.size() - rest.size(), rest.size(), rest) != 0)
        return 0;
    const auto digits = name.substr (prefix.size(), name.size() - prefix.size() - rest.size());
    if (digits.size() > 3 || ! std::all_of (digits.begin(), digits.end(), [] (char c) { return c >= '0' && c <= '9'; }))
        return 0;
    return std::stoi (digits);
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
    DeckStrip deck, deckAux;   // deckAux: the deck's aux bus through the same controls, into the delay at unity
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
    void runReturn (int n);
    void playReturn (int n);
    void encode (int n);
    void addDecks (int n);
    void addStemCues (const EngineInputs&, int offset, int n);
    void decode (const EngineInputs&, const EngineOutputs&, int offset, int n);
    void pushMeters (int n);
    void buildMeters();
    std::vector<std::string> attachVuTaps (VuTaps&);
    bool placeVuTap (const std::string& name, VuTap&);
    bool placeZoneTap (const std::string& name, const std::string& zoneName, const ZoneDecoder&,
                       std::vector<std::pair<int, VuTap*>>&, VuTap&);
    void pushVu (const EngineInputs&, int offset, int n);

    const float* analogInput (const EngineInputs&, int index, int offset) const;
    const DeckInputs* deckInputs (const EngineInputs&, int slot) const;

    EngineConfig config;
    FilterControls filter;   // the wire's values, copied per chunk; the strips hold a pointer to it
    std::vector<std::unique_ptr<EngineChannel>> channels;
    std::vector<int> analogChannelOf;   // source index -> channel index
    std::unique_ptr<AmbiEncoderBus> encMain, encFx, encPhones;
    std::unique_ptr<IemNode> dualDelay;      // declared before the loop, which holds its raw parameters
    std::unique_ptr<DelayLoop> loop;         // BEAT and FEEDBK, audio thread
    std::unique_ptr<DelayShape> shape;       // COLOR and SPREAD, control thread
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

    // The /vu taps (O13), placed by attachVuTaps; each list is read only by the audio thread.
    std::vector<std::pair<int, VuTap*>> vuInputs, vuMain, vuBooth;   // analog input / zone port, tap
    std::vector<std::pair<int, VuTap*>> vuDecks;                      // deck slot (1-based), tap of its W
    std::array<VuTap*, 2> vuRec {}, vuPhones {}, vuReturn {};
    struct VuStripTap { size_t channel; EngineChannel::Plane plane; VuTap* tap; };
    std::vector<VuStripTap> vuStrips;

    GainStage returnStage, phonesStage;
    WeightRamp returnMix, returnCue, stemCue;

    std::unique_ptr<std::atomic<float>[]> azimuth, elevation;   // contiguous, one per analog source
    std::vector<StereoIn> mainMoving, mainSteady, fxMoving, fxSteady, phMoving, phSteady;
    juce::AudioBuffer<float> busMain, busFx, busPhones;
    enum ReturnPlane { returnGainCurve, returnMixCurve, returnPhonesCurve, stemCueCurve, numReturnPlanes };
    juce::AudioBuffer<float> returnPlanes;
    std::vector<float> zeros;
    double sampleRate = 44100.0;
    int blockSize = 0;
    float fxSendPeak = 0.0f, analogEncoderPeak = 0.0f;   // this call's, published below at its end
    std::atomic<float> lastFxSendPeak { 0.0f }, lastAnalogEncoderPeak { 0.0f };
};

Engine::Impl::Impl (const EngineConfig& c, const SpatialConfig& spatial)
    : config (c), modulation (withoutOwnedParameters (c.delayModulation))
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
    loop = std::make_unique<DelayLoop> (*dualDelay);
    shape = std::make_unique<DelayShape> (*dualDelay);
    rec = std::make_unique<HeadphoneDecoder> (spatial.recParameters, "rec");
    phones = std::make_unique<HeadphoneDecoder> (withHeadphoneEq (spatial.phonesParameters, config.phonesHeadphoneEq), "phones");
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
    loop->prepare (rate);
    rec->prepare (rate, blockSize);
    phones->prepare (rate, blockSize);
    main->prepare (rate, blockSize);
    booth->prepare (rate, blockSize);
    returnStage.prepare (rate);
    phonesStage.prepare (rate);
    returnMix.prepare (rate);
    returnCue.prepare (rate);
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
    runReturn (n);
    encode (n);
    addDecks (n);
    fxSendPeak = std::max (fxSendPeak, peakOf (busFx, n));
    const auto& g = controls.global();
    loop->set (load (g.delayBpm), load (g.delayBeat), load (g.delayFeedback), n);
    engine::runNode (*dualDelay, busFx, n);
    playReturn (n);
    decode (in, out, offset, n);
    pushMeters (n);
    pushVu (in, offset, n);
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
    returnMix.setTarget ((float) laws::mixWeight (mix));
    returnCue.setTarget (isOn (g.returnCue) ? (float) laws::cueWeight (mix) : 0.0f);
    stemCue.setTarget ((float) laws::cueWeight (mix));
    returnStage.set (load (g.auxReturn));
    phonesStage.set (load (g.phonesVolume));
    main->setLevel (load (g.masterVolume));
    booth->setLevel (load (g.booth));

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

// P45: the FX Return is the delay channel's master. Its curves: RETURN's gain (through the stage, so it
// shares the stage's ramp and its exact silence at 0), and the phones' weight, which treats the delay as
// a channel: post-RETURN x mix plus pre-RETURN x cue (PFL).
void Engine::Impl::runReturn (int n)
{
    float* gain = returnPlanes.getWritePointer (returnGainCurve);
    std::fill (gain, gain + n, 1.0f);
    float* staged[1] { gain };
    returnStage.process (staged, n, 1);

    float* mixCurve = returnPlanes.getWritePointer (returnMixCurve);
    float* phonesCurve = returnPlanes.getWritePointer (returnPhonesCurve);
    returnMix.render (mixCurve, n);
    returnCue.render (phonesCurve, n);
    for (int i = 0; i < n; ++i)
        phonesCurve[i] += gain[i] * mixCurve[i];
}

// The delay's output into the phones bus by the phones' weight, then scaled by RETURN for main, booth and rec.
void Engine::Impl::playReturn (int n)
{
    const float* gain = returnPlanes.getReadPointer (returnGainCurve);
    const float* phonesCurve = returnPlanes.getReadPointer (returnPhonesCurve);
    const bool phonesSilent = returnMix.renderedSilence() && returnCue.renderedSilence();
    for (int k = 0; k < busChannels; ++k)
    {
        float* delay = busFx.getWritePointer (k);
        if (! phonesSilent)
            addWeighted (busPhones.getWritePointer (k), delay, phonesCurve, n);
        multiply (delay, gain, n);
    }
}

void Engine::Impl::encode (int n)
{
    encMain->process (mainMoving, mainSteady, {}, busMain, n, azimuth.get(), elevation.get());
    analogEncoderPeak = std::max (analogEncoderPeak, peakOf (busMain, n));
    encFx->process (fxMoving, fxSteady, {}, busFx, n, azimuth.get(), elevation.get());
    encPhones->process (phMoving, phSteady, {}, busPhones, n, azimuth.get(), elevation.get());
}

// R11: a deck channel's 16 post-VOLUME channels go straight into the buses, no encoder.
// The delay gets them like an analog channel's, times AUX; the deck's aux bus is StemDeck's own
// send, so it goes in at unity, though through the channel's strip and select fade (design 4.2,
// Ruling A1): a pulled fader or a deck on no channel still sends nothing.
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
            juce::FloatVectorOperations::add (busFx.getWritePointer (k), ch.deckAuxPost.getReadPointer (k), n);
            if (auxOpen)
                addWeighted (busFx.getWritePointer (k), post, ch.p (P::auxCurve), n);
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
        juce::FloatVectorOperations::add (busMain.getWritePointer (k), busFx.getReadPointer (k), n);
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

std::vector<std::string> Engine::Impl::attachVuTaps (VuTaps& taps)
{
    for (auto* list : { &vuInputs, &vuMain, &vuBooth, &vuDecks })
        list->clear();
    vuRec = vuPhones = vuReturn = {};
    vuStrips.clear();
    std::vector<std::string> unplaced;
    for (const auto& name : taps.names())
        if (auto* tap = taps.tapFor (name); tap != nullptr && ! placeVuTap (name, *tap))
            unplaced.push_back (name);
    return unplaced;
}

// The points the patchbay fed beat-analyzer (baseline Step 7, tests/engine/data/vu-points.json).
bool Engine::Impl::placeVuTap (const std::string& name, VuTap& tap)
{
    using P = EngineChannel;
    if (const int slot = deckSlotOfMeter (name))
    {
        if (slot > config.decks)
            return false;
        vuDecks.emplace_back (slot, &tap);
        return true;
    }
    for (int side = 0; side < sides; ++side)
    {
        const std::string s = side == 0 ? "_L" : "_R";
        const auto input = [&] (int pair)
        {
            const int index = 2 * pair - 2 + side;
            if (pair < 1 || index >= config.analogInputs)
                return false;
            vuInputs.emplace_back (index, &tap);
            return true;
        };
        if (const int pair = numberIn (name, "analog", s))
            return input (pair);
        if (name == "aux" + s)
        {
            vuReturn[(size_t) side] = &tap;   // P47: the FX Return's meter
            return true;
        }
        if (name == "rec" + s || name == "phones" + s)
        {
            (name == "rec" + s ? vuRec : vuPhones)[(size_t) side] = &tap;
            return true;
        }
        for (const auto& [part, left] : { std::pair { std::string ("_pre"), P::meterPreL }, { std::string ("_post"), P::meterPostL } })
            if (const int channel = numberIn (name, "in", part + s))
            {
                if (channel > (int) channels.size())
                    return false;
                vuStrips.push_back ({ (size_t) channel - 1, (P::Plane) (left + side), &tap });
                return true;
            }
    }
    return placeZoneTap (name, "main", *main, vuMain, tap) || placeZoneTap (name, "booth", *booth, vuBooth, tap);
}

bool Engine::Impl::placeZoneTap (const std::string& name, const std::string& zoneName, const ZoneDecoder& zone,
                                 std::vector<std::pair<int, VuTap*>>& list, VuTap& tap)
{
    int port = -1;
    if (name == zoneName + "_sub")
        port = zone.hasSub() ? 0 : -1;
    else if (const int top = numberIn (name, zoneName + "_top", ""))
        port = (zone.hasSub() ? 1 : 0) + top - 1;
    if (port < 0 || port >= zone.ports())
        return false;
    list.emplace_back (port, &tap);
    return true;
}

// Audio thread: each tap reads its point as this chunk leaves it.
void Engine::Impl::pushVu (const EngineInputs& in, int offset, int n)
{
    for (const auto& [index, tap] : vuInputs)
        tap->processMono (analogInput (in, index, offset), n);
    for (auto [zone, list] : { std::pair { main.get(), &vuMain }, { booth.get(), &vuBooth } })
        for (const auto& [port, tap] : *list)
            tap->processMono (zone->port (port), n);
    for (int side = 0; side < sides; ++side)
    {
        if (vuRec[(size_t) side] != nullptr)
            vuRec[(size_t) side]->processMono (rec->side (side), n);
        if (vuPhones[(size_t) side] != nullptr)
            vuPhones[(size_t) side]->processMono (phones->side (side), n);
        if (vuReturn[(size_t) side] != nullptr)
            vuReturn[(size_t) side]->processMono (busFx.getReadPointer (0), n);   // W, after RETURN
    }
    for (const auto& strip : vuStrips)
        strip.tap->processMono (channels[strip.channel]->p (strip.plane), n);
    for (const auto& [slot, tap] : vuDecks)
    {
        const auto* deck = deckInputs (in, slot);
        const float* w = deck != nullptr ? deck->bformat[0] : nullptr;
        tap->processMono (w != nullptr ? w + offset : zeros.data(), n);
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

void Engine::serviceControls (double seconds, const EngineControls& controls)
{
    const auto& g = controls.global();
    impl->modulation.tick (seconds, *impl->dualDelay);
    impl->shape->tick (load (g.delayColor), load (g.delaySpread), *impl->dualDelay);
}

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

std::vector<std::string> Engine::attachVuTaps (VuTaps& taps) { return impl->attachVuTaps (taps); }
int Engine::analogSourceCount() const { return (int) impl->analogChannelOf.size(); }
float Engine::lastFxSendPeak() const { return impl->lastFxSendPeak.load (std::memory_order_relaxed); }
float Engine::lastAnalogEncoderPeak() const { return impl->lastAnalogEncoderPeak.load (std::memory_order_relaxed); }
IemNode& Engine::delay() { return *impl->dualDelay; }
}
