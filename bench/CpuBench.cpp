// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CpuBench.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace a3bench
{
namespace
{
constexpr int loopBlocks = 344;      // about 1 s of signal at 128 frames, cycled
constexpr int inputsPerDeck = 16 + 16 + 9;
constexpr int distinctShifts = 64;   // inputs start at different places of the one signal
constexpr float signalDb = -20.0f;

bool parsePositive (const std::string& text, double& out)
{
    char* end = nullptr;
    const double value = std::strtod (text.c_str(), &end);
    if (text.empty() || *end != '\0' || ! (value >= 1.0))
        return false;
    out = value;
    return true;
}

bool takeCount (const std::vector<std::string>& args, size_t& i, int& out)
{
    double value = 0.0;
    if (i + 1 >= args.size() || ! parsePositive (args[++i], value))
        return false;
    out = (int) value;
    return true;
}
}

std::optional<CpuBenchOptions> parseCpuBenchArgs (const std::vector<std::string>& args)
{
    CpuBenchOptions o;
    for (size_t i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        bool ok = true;
        if (a == "--decks")
            ok = takeCount (args, i, o.decks);
        else if (a == "--channels")
            ok = takeCount (args, i, o.channels);
        else if (a == "--frames")
            ok = takeCount (args, i, o.frames);
        else if (a == "--blocks")
            ok = takeCount (args, i, o.blocks);
        else if (a == "--rate")
        {
            int rate = 0;
            ok = takeCount (args, i, rate);
            o.rate = rate;
        }
        else if (a == "--paced")
            o.paced = true;
        else if (a == "--truth" && i + 1 < args.size())
            o.truth = args[++i];
        else if (a == "--spatial" && i + 1 < args.size())
            o.spatial = args[++i];
        else
            ok = false;
        if (! ok)
            return std::nullopt;
    }
    return o;
}

int deckCountOfTruth (const juce::String& truthJson)
{
    const auto range = juce::JSON::parse (truthJson)["addresses"]["stemdeck.deck-hello"]["deck"];
    return range.isArray() && range.size() == 2 ? (int) range[1] : 0;
}

double blockSharePercent (double blockMs, int frames, double rate)
{
    return 100.0 * blockMs / (1000.0 * frames / rate);
}

int benchSourceOfChannel (int channel, int decks)
{
    return channel < decks ? channel + 1 : 0;
}

struct CpuBenchRig::Impl
{
    CpuBenchOptions options;
    a3::EngineConfig config;
    a3::SpatialConfig spatial;
    std::unique_ptr<a3::Engine> engine;
    std::unique_ptr<a3::EngineControls> controls;
    std::unique_ptr<a3::VuTaps> taps;
    std::vector<float> signal;                          // one long pink noise, every input a shifted window
    std::vector<std::vector<float>> written;            // every output's storage
    a3::EngineOutputs outputs;
    std::vector<float*> mainBuffers;

    std::vector<a3::EngineInputs> inputsPerBlock;      // built once, so a timed block does no allocation

    const float* input (int block, int index) const
    {
        const size_t start = (size_t) (block % loopBlocks) * (size_t) options.frames
                             + (size_t) (index % distinctShifts) * (size_t) options.frames;
        return signal.data() + start;
    }

    float* output()
    {
        written.emplace_back ((size_t) options.frames, 0.0f);
        return written.back().data();
    }

    void makeSignal()
    {
        Generator generator;
        generator.prepare (options.rate);
        generator.set (Shape::pink, 0.0, signalDb);
        signal.assign ((size_t) (loopBlocks + distinctShifts) * (size_t) options.frames, 0.0f);
        std::vector<float> unused (signal.size());
        generator.render (signal.data(), unused.data(), (int) signal.size());
    }

    void makeOutputs()
    {
        written.reserve (4096);   // pointers into it must stay put
        const auto zone = [this] (a3::ZoneOutputs& out, const a3::Layout& layout)
        {
            if (layout.hasSub())
                out.sub = output();
            for (size_t k = 0; k < layout.topChannels.size(); ++k)
                out.tops.push_back (output());
        };
        zone (outputs.main, spatial.main.layout);
        zone (outputs.booth, spatial.booth.layout);
        for (auto*& p : outputs.rec)
            p = output();
        for (auto*& p : outputs.phones)
            p = output();
        for (int c = 0; c < config.channels; ++c)
        {
            outputs.pre.push_back ({ output(), output() });
            outputs.post.push_back ({ output(), output() });
        }
        mainBuffers = outputs.main.tops;
        if (outputs.main.sub != nullptr)
            mainBuffers.push_back (outputs.main.sub);
    }

    a3::EngineInputs inputsFor (int block) const
    {
        a3::EngineInputs in;
        int index = 0;
        for (int a = 0; a < config.analogInputs; ++a)
            in.analog.push_back (input (block, index++));
        for (int d = 0; d < config.decks; ++d)
        {
            a3::DeckInputs deck;
            index = d * inputsPerDeck;
            for (auto& p : deck.bformat)
                p = input (block, index++);
            for (auto& p : deck.aux)
                p = input (block, index++);
            for (auto& p : deck.cue)
                p = input (block, index++);
            in.decks.push_back (deck);
        }
        return in;
    }

    void makeInputs()
    {
        for (int block = 0; block < loopBlocks; ++block)
            inputsPerBlock.push_back (inputsFor (block));
    }

    void openEverything()
    {
        for (int c = 0; c < config.channels; ++c)
        {
            auto& ch = controls->channel (c);
            ch.gain = 0.75f; ch.volume = 0.75f;
            ch.eqHigh = ch.eqMid = ch.eqLow = 0.5f;
            ch.frequency = 0.5f; ch.q = 0.5f; ch.threeD = 1.0f;
            ch.source = benchSourceOfChannel (c, config.decks);
            ch.cue = 1.0f; ch.auxSend = 0.5f; ch.filterOn = 1.0f;
            ch.azimuth = 30.0f * (float) c; ch.elevation = 0.0f;
        }
        auto& g = controls->global();
        g.masterVolume = 0.68f; g.booth = 0.99f;
        g.phonesMix = 0.5f; g.phonesVolume = 0.75f; g.auxReturn = 0.75f;
        g.returnCue = 0.0f; g.delayBpm = 120.0f; g.gateOpen = true;
    }
};

CpuBenchRig::CpuBenchRig (std::unique_ptr<Impl> i) : impl (std::move (i)) {}
CpuBenchRig::~CpuBenchRig() = default;

std::unique_ptr<CpuBenchRig> CpuBenchRig::create (const CpuBenchOptions& options, const a3::SpatialConfig& spatial,
                                                  const std::vector<std::string>& vuMeterNames, juce::String& error)
{
    auto impl = std::make_unique<Impl>();
    impl->options = options;
    impl->spatial = spatial;
    auto& config = impl->config;
    config.channels = options.channels;
    config.decks = options.decks;
    config.decksNamed = true;
    config.analogPairs.assign ((size_t) options.channels, 0);
    for (int c = 0; c < options.channels && 2 * c + 2 <= config.analogInputs; ++c)
        config.analogPairs[(size_t) c] = c + 1;
    config.trims.main = config.trims.booth = config.trims.phones = 0.0f;
    config.phonesHeadphoneEq = "Sennheiser-HD600";

    impl->engine = a3::Engine::create (config, impl->spatial, error);
    if (impl->engine == nullptr)
        return nullptr;
    impl->engine->prepare (options.rate, options.frames);
    if (! vuMeterNames.empty())
    {
        impl->taps = std::make_unique<a3::VuTaps> (vuMeterNames, a3::VuSettings {});
        impl->engine->attachVuTaps (*impl->taps);
    }
    a3::GateLevels open;
    for (auto& g : open.gain)
        g = 1.0f;
    impl->engine->startGateAt (open);
    impl->controls = std::make_unique<a3::EngineControls> (config.channels);
    impl->openEverything();
    impl->makeSignal();
    impl->makeOutputs();
    impl->makeInputs();
    return std::unique_ptr<CpuBenchRig> (new CpuBenchRig (std::move (impl)));
}

const a3::EngineInputs& CpuBenchRig::inputsAt (int block) const
{
    return impl->inputsPerBlock[(size_t) (block % loopBlocks)];
}

void CpuBenchRig::process (int block) { processWith (inputsAt (block)); }

void CpuBenchRig::processWith (const a3::EngineInputs& inputs)
{
    impl->engine->process (inputs, impl->outputs, impl->options.frames, *impl->controls);
}

float CpuBenchRig::mainEnergy() const
{
    float sum = 0.0f;
    for (const float* buffer : impl->mainBuffers)
        for (int i = 0; i < impl->options.frames; ++i)
            sum += buffer[i] * buffer[i];
    return sum;
}

a3::EngineControls& CpuBenchRig::controls() { return *impl->controls; }

float CpuBenchRig::mainPeak() const
{
    float peak = 0.0f;
    for (const float* buffer : impl->mainBuffers)
        for (int i = 0; i < impl->options.frames; ++i)
            peak = std::max (peak, std::abs (buffer[i]));
    return peak;
}
}
