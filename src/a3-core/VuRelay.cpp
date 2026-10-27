// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "VuRelay.h"

namespace a3core
{
void forward (const std::function<std::optional<Datagram>()>& receive,
              const std::function<void (const Datagram&, const Endpoint&)>& send,
              const std::function<std::vector<Endpoint>()>& destinations,
              const std::function<bool()>& running)
{
    while (running())
    {
        const auto data = receive();
        if (! data)
            continue;
        for (const auto& to : destinations())
        {
            try
            {
                send (*data, to);
            }
            catch (const std::system_error&)
            {
            }
        }
    }
}

class VuRelayThread::Loop : public juce::Thread
{
public:
    Loop (const Endpoint& listenOn, std::function<std::vector<Endpoint>()> destinationsToUse)
        : juce::Thread ("a3-vu-relay"), destinations (std::move (destinationsToUse)), inbox (false), out (false)
    {
        const auto address = listenOn.host == "local" ? juce::String (loopbackAddress())
                           : listenOn.host == "any"   ? juce::String()
                                                      : juce::String (listenOn.host);
        isBound = inbox.bindToPort (listenOn.port, address);
    }

    ~Loop() override { stopThread (2000); }

    bool bound() const { return isBound; }
    int boundPort() { return inbox.getBoundPort(); }
    bool running() { return isThreadRunning(); }

    void run() override
    {
        // Waits in slices, so that stop() is heard within one.
        const auto receive = [this]() -> std::optional<Datagram> {
            const auto ready = inbox.waitUntilReady (true, 100);
            if (ready < 0)
            {
                wait (100);   // an error returns at once: do not spin until stop()
                return std::nullopt;
            }
            if (ready == 0)
                return std::nullopt;
            const auto count = inbox.read (buffer.data(), maxDatagram, false);
            if (count <= 0)
                return std::nullopt;
            return Datagram (buffer.begin(), buffer.begin() + count);
        };
        const auto send = [this] (const Datagram& data, const Endpoint& to) {
            if (out.write (juce::String (to.host), to.port, data.data(), static_cast<int> (data.size())) < 0)
                throw std::system_error (std::make_error_code (std::errc::io_error));
        };
        forward (receive, send, destinations, [this] { return ! threadShouldExit(); });
    }

private:
    std::function<std::vector<Endpoint>()> destinations;
    juce::DatagramSocket inbox, out;
    bool isBound = false;
    Datagram buffer = Datagram (maxDatagram);
};

VuRelayThread::VuRelayThread (const Endpoint& listenOn, std::function<std::vector<Endpoint>()> destinations)
    : loop (std::make_unique<Loop> (listenOn, std::move (destinations)))
{
}

VuRelayThread::~VuRelayThread() { stop(); }

bool VuRelayThread::bound() const { return loop->bound(); }
int VuRelayThread::boundPort() const { return loop->boundPort(); }

bool VuRelayThread::start()
{
    if (! loop->bound())
        return false;
    loop->startThread();
    return true;
}

bool VuRelayThread::isRunning() const { return loop->running(); }

void VuRelayThread::stop() { loop->stopThread (2000); }
}
