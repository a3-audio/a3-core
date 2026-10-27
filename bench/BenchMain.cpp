// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// a3-engine-bench: a headless JACK client that runs four ChannelStrips and the SpatialBus.
// Ports are created and never connected (the patching is done by hand).
#include <jack/jack.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_osc/juce_osc.h>
#include "BlockTimer.h"
#include "ChannelStrip.h"
#include "Generator.h"
#include "OscControl.h"
#include "SpatialBus.h"
#include "SpatialConfig.h"
#include "SpatialPorts.h"

namespace
{
constexpr int strips = 4, maxBlock = 4096, ringSize = 8192;
volatile std::sig_atomic_t stopRequested = 0;
void onSignal (int) { stopRequested = 1; }

// Block durations from the audio thread to the main thread, lock-free (single producer, single consumer).
class TimingRing
{
public:
    void push (float ms) noexcept
    {
        const auto w = write.load (std::memory_order_relaxed);
        if (w - read.load (std::memory_order_acquire) >= ringSize)
            return;   // reader is behind: drop, never wait
        slots[w % ringSize] = ms;
        write.store (w + 1, std::memory_order_release);
    }
    template <typename Fn> void drain (Fn&& fn)
    {
        auto r = read.load (std::memory_order_relaxed);
        const auto w = write.load (std::memory_order_acquire);
        for (; r != w; ++r)
            fn ((double) slots[r % ringSize]);
        read.store (r, std::memory_order_release);
    }
private:
    float slots[ringSize] {};
    std::atomic<size_t> write { 0 }, read { 0 };
};

struct ZonePorts { jack_port_t* sub = nullptr; std::vector<jack_port_t*> tops; };

struct Bench
{
    jack_client_t* client = nullptr;
    a3::ChannelStrip strip[strips];
    a3bench::Generator generator;
    jack_port_t* in[strips][2] {};
    jack_port_t* pre[strips][2] {};
    jack_port_t* moving[strips][2] {};
    jack_port_t* steady[strips][2] {};
    jack_port_t* gen[2] {};
    TimingRing timing;
    bool measure = false;

    // The spatial bus. The callback uses whichever bus `bus` holds and writes it
    // to `acknowledged` first; the main thread frees a replaced bus only after
    // the callback has acknowledged its successor.
    a3::SpatialControls positions;
    std::atomic<a3::SpatialBus*> bus { nullptr };
    std::atomic<a3::SpatialBus*> acknowledged { nullptr };
    jack_port_t* rec[2] {};
    ZonePorts mainPorts, boothPorts;
    a3::SpatialOutputs outputs;   // tops vectors sized once, so the callback never allocates
};

float* bufferOf (jack_port_t* port, jack_nframes_t frames)
{
    return port == nullptr ? nullptr : static_cast<float*> (jack_port_get_buffer (port, frames));
}

void fillZone (a3::ZoneOutputs& out, const ZonePorts& ports, jack_nframes_t frames)
{
    out.sub = bufferOf (ports.sub, frames);
    for (size_t k = 0; k < ports.tops.size(); ++k)
        out.tops[k] = bufferOf (ports.tops[k], frames);
}

void runSpatialBus (Bench& b, jack_nframes_t frames)
{
    auto* bus = b.bus.load (std::memory_order_acquire);
    b.acknowledged.store (bus, std::memory_order_release);
    std::array<a3::StripTaps, a3::numSpatialStrips> taps {};
    for (int n = 0; n < strips; ++n)
        taps[(size_t) n] = { { bufferOf (b.moving[n][0], frames), bufferOf (b.moving[n][1], frames) },
                             { bufferOf (b.steady[n][0], frames), bufferOf (b.steady[n][1], frames) } };
    b.outputs.rec[0] = bufferOf (b.rec[0], frames);
    b.outputs.rec[1] = bufferOf (b.rec[1], frames);
    fillZone (b.outputs.main, b.mainPorts, frames);
    fillZone (b.outputs.booth, b.boothPorts, frames);
    bus->process (taps, b.outputs, (int) frames, b.positions);
}

int process (jack_nframes_t frames, void* arg)
{
    auto& b = *static_cast<Bench*> (arg);
    juce::ScopedNoDenormals noDenormals;
    const auto started = b.measure ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    const auto buffer = [frames] (jack_port_t* p) { return static_cast<float*> (jack_port_get_buffer (p, frames)); };

    for (int n = 0; n < strips; ++n)
    {
        const float* i[2] { buffer (b.in[n][0]), buffer (b.in[n][1]) };
        float* m[2] { buffer (b.moving[n][0]), buffer (b.moving[n][1]) };
        float* s[2] { buffer (b.steady[n][0]), buffer (b.steady[n][1]) };
        float* p[2] { buffer (b.pre[n][0]), buffer (b.pre[n][1]) };
        b.strip[n].process (i, m, s, (int) frames, p);
    }
    b.generator.render (buffer (b.gen[0]), buffer (b.gen[1]), (int) frames);
    runSpatialBus (b, frames);

    if (b.measure)
        b.timing.push ((float) std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count());
    return 0;
}

int bufferSizeChanged (jack_nframes_t, void*) { return 0; }   // the strips take any size up to maxBlock and beyond

jack_port_t* makePort (jack_client_t* c, const std::string& name, unsigned long flags)
{
    return jack_port_register (c, name.c_str(), JACK_DEFAULT_AUDIO_TYPE, flags, 0);
}

bool registerSpatialPorts (Bench& b, const a3::SpatialConfig& config)
{
    // The same order as spatialPortNames, which the tests pin.
    const auto names = a3bench::spatialPortNames (config);
    size_t next = 0;
    const auto add = [&] { return makePort (b.client, names[next++], JackPortIsOutput); };
    b.rec[0] = add();
    b.rec[1] = add();
    bool ok = b.rec[0] && b.rec[1];
    for (auto* zone : { &b.mainPorts, &b.boothPorts })
    {
        const auto& layout = zone == &b.mainPorts ? config.main.layout : config.booth.layout;
        if (layout.hasSub())
            ok = ((zone->sub = add()) != nullptr) && ok;
        for (size_t k = 0; k < layout.topChannels.size(); ++k)
        {
            zone->tops.push_back (add());
            ok = zone->tops.back() != nullptr && ok;
        }
    }
    b.outputs.main.tops.assign (b.mainPorts.tops.size(), nullptr);
    b.outputs.booth.tops.assign (b.boothPorts.tops.size(), nullptr);
    return ok;
}

bool registerPorts (Bench& b, const a3::SpatialConfig& config)
{
    for (int n = 0; n < strips; ++n)
        for (int side = 0; side < 2; ++side)
        {
            const std::string c = std::string ("ch") + std::to_string (n + 1), lr = side == 0 ? "_L" : "_R";
            b.in[n][side] = makePort (b.client, c + lr, JackPortIsInput);
            b.pre[n][side] = makePort (b.client, c + "_pre" + lr, JackPortIsOutput);
            b.moving[n][side] = makePort (b.client, c + "_moving" + lr, JackPortIsOutput);
            b.steady[n][side] = makePort (b.client, c + "_steady" + lr, JackPortIsOutput);
            if (! b.in[n][side] || ! b.pre[n][side] || ! b.moving[n][side] || ! b.steady[n][side])
                return false;
        }
    b.gen[0] = makePort (b.client, "gen_L", JackPortIsOutput);
    b.gen[1] = makePort (b.client, "gen_R", JackPortIsOutput);
    return b.gen[0] && b.gen[1] && registerSpatialPorts (b, config);
}

class ControlListener : public juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    ControlListener (const a3bench::Truth& t, std::array<a3::StripControls*, 4> s, a3::SpatialControls& p)
        : truth (t), controls (s), positions (p) {}
    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        if (message.size() < 1)
            return;
        const auto& arg = message[0];
        if (arg.isFloat32())
            apply (message.getAddressPattern().toString(), arg.getFloat32());
        else if (arg.isInt32())
            apply (message.getAddressPattern().toString(), (float) arg.getInt32());
    }
private:
    void apply (const juce::String& address, float value)
    {
        if (! a3bench::applyChannelMessage (truth.controls, address, value, controls))
            a3bench::applyPositionMessage (truth.positions, address, value, positions);
    }

    const a3bench::Truth& truth;
    std::array<a3::StripControls*, 4> controls;
    a3::SpatialControls& positions;
};

// The only caller of ChannelStrip::meters(): it takes the peaks.
void sendMeters (Bench& b, const a3bench::Truth& truth, juce::OSCSender& sender)
{
    juce::OSCBundle bundle;
    for (int n = 0; n < strips; ++n)
    {
        const auto m = b.strip[n].meters();
        const std::string base = "in" + std::to_string (n + 1);
        const char* sides[2] { "_L", "_R" };
        for (int side = 0; side < 2; ++side)
        {
            bundle.addElement (juce::OSCMessage (juce::OSCAddressPattern (truth.vuAddressFor (base + "_pre" + sides[side])),
                                                 m.prePeak[side], m.preRms[side]));
            bundle.addElement (juce::OSCMessage (juce::OSCAddressPattern (truth.vuAddressFor (base + "_post" + sides[side])),
                                                 m.postPeak[side], m.postRms[side]));
        }
    }
    sender.send (bundle);
}

void printTiming (a3bench::BlockTimer& t)
{
    std::printf ("block ms: mean %.4f  p99 %.4f  worst %.4f\n", t.mean(), t.p99(), t.worst());
    std::fflush (stdout);
    t.clear();
}

// The JACK quantum changed: build and prepare a new bus off the audio thread and
// publish it. Until the callback has taken it, the old bus chunks the longer calls.
// A replaced bus is freed only once the callback has acknowledged its successor.
class BusSwapper
{
public:
    BusSwapper (Bench& bench, const a3::SpatialConfig& c, double rate) : b (bench), config (c), sampleRate (rate) {}

    void service (int quantum)
    {
        retireAcknowledged();
        auto* current = b.bus.load();
        if (retired != nullptr || quantum == current->preparedBlockSize())
            return;
        juce::String error;
        auto next = a3::SpatialBus::create (config, error);
        if (next == nullptr)
            return std::fprintf (stderr, "cannot rebuild the spatial bus for %d frames: %s\n", quantum, error.toRawUTF8()), void();
        next->prepare (sampleRate, quantum);
        retired = b.bus.exchange (next.release());
    }

    void freeAll()
    {
        delete retired;
        retired = nullptr;
        delete b.bus.exchange (nullptr);
    }

private:
    void retireAcknowledged()
    {
        if (retired == nullptr || b.acknowledged.load (std::memory_order_acquire) != b.bus.load())
            return;
        delete retired;
        retired = nullptr;
    }

    Bench& b;
    const a3::SpatialConfig& config;
    double sampleRate;
    a3::SpatialBus* retired = nullptr;
};

void usage()
{
    std::fprintf (stderr, "usage: a3-engine-bench [--truth FILE] [--gen sine:HZ:DB|pink:DB|sweep:DB]\n"
                          "                       [--meters-osc=HOST:PORT] [--timing] [--spatial DIR]\n"
                          "  --spatial DIR: the speaker layouts and encoder/decoder data. Default: the first\n"
                          "  that exists of ~/.config/a3/engine/spatial and /usr/share/a3-engine/spatial.\n");
}
}

int main (int argc, char** argv)
{
    std::string truthPath = "/usr/share/a3/a3-osc.json";
    std::string spatialArgument;
    a3bench::GenSpec gen;
    a3bench::MetersTarget metersTarget;
    bool metersOsc = false, timing = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--truth" && i + 1 < argc)
            truthPath = argv[++i];
        else if (a == "--gen" && i + 1 < argc && a3bench::parseGenSpec (argv[++i], gen))
            continue;
        else if (a3bench::parseMetersTarget (a, metersTarget))
            metersOsc = true;
        else if (a == "--spatial" && i + 1 < argc)
            spatialArgument = argv[++i];
        else if (a == "--timing")
            timing = true;
        else
            return usage(), 2;
    }

    const auto text = juce::File (truthPath).loadFileAsString();
    const auto truth = a3bench::parseTruth (text);
    if (! truth)
        return std::fprintf (stderr, "cannot use the truth at %s (missing, or no engine.osc entry)\n", truthPath.c_str()), 1;

    const char* home = std::getenv ("HOME");
    const auto spatialDirectory = a3bench::chooseSpatialDirectory (spatialArgument, home ? home : "",
        [] (const std::string& path) { return juce::File (path).isDirectory(); });
    juce::String spatialError;
    const auto spatialConfig = a3::loadSpatialConfig (juce::File (spatialDirectory), spatialError);
    if (! spatialConfig)
        return std::fprintf (stderr, "cannot use the spatial data in %s: %s\n", spatialDirectory.c_str(), spatialError.toRawUTF8()), 1;

    if (metersOsc)
        for (int n = 1; n <= strips; ++n)
            for (const char* stage : { "_pre", "_post" })
                for (const char* side : { "_L", "_R" })
                {
                    const auto name = "in" + std::to_string (n) + stage + side;
                    if (truth->vuAddressFor (name).isEmpty())
                        return std::fprintf (stderr, "the truth has no VU meter %s\n", name.c_str()), 1;
                }

    auto bench = std::make_unique<Bench>();
    bench->client = jack_client_open ("a3-engine-bench", JackNoStartServer, nullptr);
    if (bench->client == nullptr)
        return std::fprintf (stderr, "no JACK server running\n"), 1;
    const double rate = (double) jack_get_sample_rate (bench->client);
    for (auto& s : bench->strip)
        s.prepare (rate, maxBlock);
    bench->generator.prepare (rate);
    bench->generator.set (gen.shape, gen.hz, gen.levelDb);
    bench->measure = timing;
    const int quantum = (int) jack_get_buffer_size (bench->client);
    juce::String busError;
    auto firstBus = a3::SpatialBus::create (*spatialConfig, busError);
    if (firstBus == nullptr)
        return std::fprintf (stderr, "cannot use the spatial data in %s: %s\n", spatialDirectory.c_str(), busError.toRawUTF8()), 1;
    firstBus->prepare (rate, quantum);
    bench->bus = firstBus.release();
    BusSwapper swapper (*bench, *spatialConfig, rate);
    if (! registerPorts (*bench, *spatialConfig))
        return std::fprintf (stderr, "cannot register the JACK ports\n"), 1;
    jack_set_process_callback (bench->client, process, bench.get());
    jack_set_buffer_size_callback (bench->client, bufferSizeChanged, nullptr);

    std::array<a3::StripControls*, 4> controls { &bench->strip[0].controls(), &bench->strip[1].controls(),
                                                 &bench->strip[2].controls(), &bench->strip[3].controls() };
    ControlListener listener (*truth, controls, bench->positions);
    juce::DatagramSocket socket (false);
    juce::OSCReceiver receiver;
    if (! socket.bindToPort (truth->enginePort, truth->engineBindAddress) || ! receiver.connectToSocket (socket))
        return std::fprintf (stderr, "cannot listen on %s:%d\n", truth->engineBindAddress.toRawUTF8(), truth->enginePort), 1;
    receiver.addListener (&listener);

    juce::OSCSender sender;
    if (metersOsc)
    {
        const auto& host = metersTarget.host;
        const int port = metersTarget.port;
        const auto clash = truth->listenerOnPort (port);
        if (clash.isNotEmpty())
            std::fprintf (stderr, "warning: --meters-osc port %d is a listener of the truth (%s); /vu/51-66 may overwrite live meters\n",
                          port, clash.toRawUTF8());
        if (! sender.connect (host, port))
            return std::fprintf (stderr, "cannot send meters to %s:%d\n", host.toRawUTF8(), port), 1;
    }

    std::signal (SIGINT, onSignal);
    std::signal (SIGTERM, onSignal);
    if (jack_activate (bench->client) != 0)
        return std::fprintf (stderr, "cannot activate the JACK client\n"), 1;
    std::fprintf (stderr, "a3-engine-bench: %s:%d, %.0f Hz\n", truth->engineBindAddress.toRawUTF8(), truth->enginePort, rate);

    a3bench::BlockTimer timer;
    auto lastReport = std::chrono::steady_clock::now();
    while (! stopRequested)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
        swapper.service ((int) jack_get_buffer_size (bench->client));
        if (metersOsc)
            sendMeters (*bench, *truth, sender);
        bench->timing.drain ([&timer] (double ms) { timer.add (ms); });
        if (timing && std::chrono::steady_clock::now() - lastReport >= std::chrono::seconds (10))
        {
            printTiming (timer);
            lastReport = std::chrono::steady_clock::now();
        }
    }
    jack_deactivate (bench->client);
    bench->timing.drain ([&timer] (double ms) { timer.add (ms); });
    if (timing)
        printTiming (timer);
    receiver.removeListener (&listener);
    jack_client_close (bench->client);
    swapper.freeAll();
    return 0;
}
