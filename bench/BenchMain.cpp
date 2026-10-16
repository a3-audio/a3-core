// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// a3-engine-bench: a headless JACK client that runs four ChannelStrips.
// Ports are created and never connected (the patching is done by hand).
#include <jack/jack.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_osc/juce_osc.h>
#include "BlockTimer.h"
#include "ChannelStrip.h"
#include "Generator.h"
#include "OscControl.h"

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
};

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

    if (b.measure)
        b.timing.push ((float) std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count());
    return 0;
}

int bufferSizeChanged (jack_nframes_t, void*) { return 0; }   // the strips take any size up to maxBlock and beyond

jack_port_t* makePort (jack_client_t* c, const std::string& name, unsigned long flags)
{
    return jack_port_register (c, name.c_str(), JACK_DEFAULT_AUDIO_TYPE, flags, 0);
}

bool registerPorts (Bench& b)
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
    return b.gen[0] && b.gen[1];
}

class ControlListener : public juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    ControlListener (const a3bench::ControlMap& m, std::array<a3::StripControls*, 4> s) : map (m), controls (s) {}
    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        if (message.size() < 1)
            return;
        const auto& arg = message[0];
        if (arg.isFloat32())
            a3bench::applyChannelMessage (map, message.getAddressPattern().toString(), arg.getFloat32(), controls);
        else if (arg.isInt32())
            a3bench::applyChannelMessage (map, message.getAddressPattern().toString(), (float) arg.getInt32(), controls);
    }
private:
    const a3bench::ControlMap& map;
    std::array<a3::StripControls*, 4> controls;
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

void usage()
{
    std::fprintf (stderr, "usage: a3-engine-bench [--truth FILE] [--gen sine:HZ:DB|pink:DB|sweep:DB]\n"
                          "                       [--meters-osc[=HOST:PORT]] [--timing]\n");
}
}

int main (int argc, char** argv)
{
    std::string truthPath = "/usr/share/a3/a3-osc.json", metersTarget;
    a3bench::GenSpec gen;
    bool metersOsc = false, timing = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--truth" && i + 1 < argc)
            truthPath = argv[++i];
        else if (a == "--gen" && i + 1 < argc && a3bench::parseGenSpec (argv[++i], gen))
            continue;
        else if (a == "--meters-osc" || a.rfind ("--meters-osc=", 0) == 0)
            metersOsc = true, metersTarget = a.size() > 13 ? a.substr (13) : "";
        else if (a == "--timing")
            timing = true;
        else
            return usage(), 2;
    }

    const auto text = juce::File (truthPath).loadFileAsString();
    const auto truth = a3bench::parseTruth (text);
    if (! truth)
        return std::fprintf (stderr, "cannot use the truth at %s (missing, or no engine.osc entry)\n", truthPath.c_str()), 1;

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
    if (! registerPorts (*bench))
        return std::fprintf (stderr, "cannot register the JACK ports\n"), 1;
    jack_set_process_callback (bench->client, process, bench.get());
    jack_set_buffer_size_callback (bench->client, bufferSizeChanged, nullptr);

    std::array<a3::StripControls*, 4> controls { &bench->strip[0].controls(), &bench->strip[1].controls(),
                                                 &bench->strip[2].controls(), &bench->strip[3].controls() };
    ControlListener listener (truth->controls, controls);
    juce::DatagramSocket socket (false);
    juce::OSCReceiver receiver;
    if (! socket.bindToPort (truth->enginePort, truth->engineBindAddress) || ! receiver.connectToSocket (socket))
        return std::fprintf (stderr, "cannot listen on %s:%d\n", truth->engineBindAddress.toRawUTF8(), truth->enginePort), 1;
    receiver.addListener (&listener);

    juce::OSCSender sender;
    if (metersOsc)
    {
        juce::String host = "127.0.0.1";
        int port = truth->motionVuPort;
        if (metersTarget.find (':') != std::string::npos)
            host = metersTarget.substr (0, metersTarget.find (':')), port = std::atoi (metersTarget.c_str() + metersTarget.find (':') + 1);
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
    return 0;
}
