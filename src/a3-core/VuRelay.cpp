// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "VuRelay.h"

#include <algorithm>

#include "OscWire.h"

namespace a3core
{
void appendOnce (std::vector<Endpoint>& endpoints, const Endpoint& endpoint)
{
    if (std::find (endpoints.begin(), endpoints.end(), endpoint) == endpoints.end())
        endpoints.push_back (endpoint);
}

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

namespace
{
// The truth's words for the meters' key, its vu role and the prefix that marks StemDeck's meters.
constexpr const char* vuKey = "vu";
constexpr const char* vuRole = "vu";
constexpr const char* vuNumber = "n";
constexpr const char* stemPrefix = "stem_";
constexpr const char* coreProgram = "core";
constexpr const char* deskProgram = "desk";
constexpr const char* engineProgram = "engine";

bool startsWith (const std::string& text, const std::string& prefix) { return text.compare (0, prefix.size(), prefix) == 0; }

std::string textOf (const juce::var& value) { return value.toString().toStdString(); }

// Whether the truth has the engine send meters to Core. Before O13 the analyzer does, and reaches the
// displays itself: Core adds nothing then.
bool engineSendsMeters (const std::vector<juce::var>& routes)
{
    return std::any_of (routes.begin(), routes.end(), [] (const juce::var& route) {
        const auto to = textOf (route["to"]);
        return textOf (route["from"]) == engineProgram && textOf (route["carries"]) == vuKey && startsWith (to, std::string (coreProgram) + ".");
    });
}
}

VuSource vuSourceOf (const Truth& truth, const uint8_t* data, size_t size)
{
    const auto messages = decodePacket (data, size);
    if (! messages || messages->empty())
        return VuSource::unknown;
    const auto found = truth.match (messages->front().address);
    if (! found || found->first != vuKey)
        return VuSource::unknown;
    const auto names = truth.vuMeters();
    const auto number = static_cast<size_t> (found->second.at (vuNumber));
    if (number < 1 || number > names.size())
        return VuSource::unknown;
    return startsWith (names[number - 1], stemPrefix) ? VuSource::stemdeck : VuSource::engine;
}

std::vector<Endpoint> engineVuTargets (const Truth& truth)
{
    std::vector<Endpoint> targets;
    const auto routes = truth.routes();
    if (! engineSendsMeters (routes))
        return targets;
    for (const auto& route : routes)
    {
        if (textOf (route["from"]) != coreProgram)
            continue;
        const auto name = textOf (route["to"]);
        const auto dot = name.find ('.');
        if (dot == std::string::npos)
            continue;
        const auto program = name.substr (0, dot), role = name.substr (dot + 1);
        // The desks are followed by address, not named by the truth: their port is no display of its own.
        if (program == deskProgram || (textOf (route["carries"]) != vuKey && role != vuRole))
            continue;
        try
        {
            appendOnce (targets, truth.endpoint (program, role));
        }
        catch (const TruthError&)
        {
        }
    }
    return targets;
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
