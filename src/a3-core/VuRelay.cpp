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
        const auto address = listenOn.host == "local" ? juce::String ("127.0.0.1")
                           : listenOn.host == "any"   ? juce::String()
                                                      : juce::String (listenOn.host);
        isBound = inbox.bindToPort (listenOn.port, address);
    }

    ~Loop() override { stopThread (2000); }

    bool bound() const { return isBound; }
    int boundPort() { return inbox.getBoundPort(); }

    void run() override
    {
        // Waits in slices, so that stop() is heard within one.
        const auto receive = [this]() -> std::optional<Datagram> {
            if (inbox.waitUntilReady (true, 100) != 1)
                return std::nullopt;
            Datagram data (maxDatagram);
            const auto count = inbox.read (data.data(), maxDatagram, false);
            if (count <= 0)
                return std::nullopt;
            data.resize (static_cast<size_t> (count));
            return data;
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
};

VuRelayThread::VuRelayThread (const Endpoint& listenOn, std::function<std::vector<Endpoint>()> destinations)
    : loop (std::make_unique<Loop> (listenOn, std::move (destinations)))
{
}

VuRelayThread::~VuRelayThread() { stop(); }

bool VuRelayThread::bound() const { return loop->bound(); }
int VuRelayThread::boundPort() const { return loop->boundPort(); }

void VuRelayThread::start()
{
    if (loop->bound())
        loop->startThread();
}

void VuRelayThread::stop() { loop->stopThread (2000); }
}
