// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The Core service on real loopback sockets: B1's a3-core.py main block (a3-core 7a7b2441f74e),
// engine mode. Ports of test_core_announce's CoreService rows, test_core_reads_the_truth,
// test_core_servers_do_not_spawn and test_meter_ballistics.test_core_says_a_refused_block -- the
// Python ones read a3-core.py's text; these run the service and read what it does -- plus the
// service's own: one serve thread, start and stop.
//
// Every test truth is the shipped one with every port Core uses moved to a free loopback port and
// the network block on 127.0.0.1/8, so nothing here reaches the LAN: the /core/here broadcast goes
// to 127.255.255.255.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

#include <gtest/gtest.h>

#include "Announce.h"
#include "B0Truth.h"
#include "CoreService.h"
#include "HttpClient.h"
#include "Json.h"
#include "OscWire.h"
#include "Render.h"
#include "SendFailures.h"
#include "Subscribers.h"
#include "UdpNet.h"

using namespace a3core;

namespace
{
using Steady = std::chrono::steady_clock;

const juce::File coreSources (A3_CORE_SOURCE_DIR);

int threadCount()
{
    int count = 0;
    for (const auto& entry : juce::File ("/proc/self/task").findChildFiles (juce::File::findDirectories, false))
    {
        juce::ignoreUnused (entry);
        ++count;
    }
    return count;
}

std::set<std::string> threadNames()
{
    std::set<std::string> names;
    for (const auto& task : juce::File ("/proc/self/task").findChildFiles (juce::File::findDirectories, false))
        names.insert (task.getChildFile ("comm").loadFileAsString().trim().toStdString());
    return names;
}

// A UDP socket of the test's: a fake device that listens, or a sender.
class Socket
{
public:
    explicit Socket (const std::string& host = loopbackAddress(), int port = 0)
    {
        fd = ::socket (AF_INET, SOCK_DGRAM, 0);
        int big = 8 * 1024 * 1024;
        ::setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
        sockaddr_in at {};
        at.sin_family = AF_INET;
        at.sin_port = htons (static_cast<uint16_t> (port));
        ::inet_pton (AF_INET, host.c_str(), &at.sin_addr);
        boundOk = ::bind (fd, (sockaddr*) &at, sizeof at) == 0;
        socklen_t length = sizeof at;
        ::getsockname (fd, (sockaddr*) &at, &length);
        boundPort = ntohs (at.sin_port);
    }
    ~Socket() { ::close (fd); }

    Endpoint endpoint() const { return { loopbackAddress(), boundPort }; }
    int port() const { return boundPort; }
    bool ok() const { return boundOk; }

    void sendTo (int port, const std::vector<uint8_t>& data) const
    {
        sockaddr_in to {};
        to.sin_family = AF_INET;
        to.sin_port = htons (static_cast<uint16_t> (port));
        ::inet_pton (AF_INET, loopbackAddress().c_str(), &to.sin_addr);
        ::sendto (fd, data.data(), data.size(), 0, (sockaddr*) &to, sizeof to);
    }

    void osc (int port, const std::string& address, std::vector<OscArg> args = {}) const
    {
        sendTo (port, encodeMessage ({ address, std::move (args) }));
    }

    struct Received
    {
        std::vector<uint8_t> data;
        int fromPort;
    };

    std::optional<Received> receive (int millis) const
    {
        pollfd ready { fd, POLLIN, 0 };
        if (::poll (&ready, 1, millis) <= 0)
            return std::nullopt;
        std::vector<uint8_t> buffer (65536);
        sockaddr_in from {};
        socklen_t length = sizeof from;
        const auto got = ::recvfrom (fd, buffer.data(), buffer.size(), 0, (sockaddr*) &from, &length);
        if (got < 0)
            return std::nullopt;
        buffer.resize (static_cast<size_t> (got));
        return Received { buffer, ntohs (from.sin_port) };
    }

    // Every message until `millis` pass without one.
    std::vector<OscMessage> messages (int millis = 300) const
    {
        std::vector<OscMessage> out;
        while (const auto got = receive (millis))
            if (const auto decoded = decodePacket (got->data.data(), got->data.size()))
                out.insert (out.end(), decoded->begin(), decoded->end());
        return out;
    }

    // The first message with `address` within `millis`; the others are read and dropped.
    std::optional<OscMessage> awaitAddress (const std::string& address, int millis = 3000) const
    {
        const auto end = Steady::now() + std::chrono::milliseconds (millis);
        while (Steady::now() < end)
        {
            const auto left = (int) std::chrono::duration_cast<std::chrono::milliseconds> (end - Steady::now()).count();
            const auto got = receive (std::max (left, 1));
            if (! got)
                continue;
            if (const auto decoded = decodePacket (got->data.data(), got->data.size()))
                for (const auto& message : *decoded)
                    if (message.address == address)
                        return message;
        }
        return std::nullopt;
    }

private:
    int fd = -1, boundPort = 0;
    bool boundOk = false;
};

int freePort()
{
    const Socket probe;
    return probe.port();
}

// Every line said, by whom, in order; safe from every thread.
class Said
{
public:
    struct Line
    {
        std::string text;
        bool toStderr;
        std::thread::id thread;
    };

    Journal journal()
    {
        return [this] (const std::string& line, bool toStderr) {
            if (b0::isTheMissingDeckAudioNote (line))
                return;
            {
                const std::scoped_lock hold (lock);
                lines.push_back ({ line, toStderr, std::this_thread::get_id() });
            }
            holdIfAsked (line);
        };
    }

    // The thread that says a line starting with `start` waits there until release(): a way to hold
    // the serve thread at a known point without a hook in the service.
    void holdAt (const std::string& start)
    {
        const std::scoped_lock hold (gateLock);
        holdPrefix = start;
        held = released = false;
    }

    bool waitUntilHeld (int millis = 3000)
    {
        std::unique_lock hold (gateLock);
        return gate.wait_for (hold, std::chrono::milliseconds (millis), [this] { return held; });
    }

    void release()
    {
        {
            const std::scoped_lock hold (gateLock);
            released = true;
        }
        gate.notify_all();
    }

    std::vector<Line> all() const
    {
        const std::scoped_lock hold (lock);
        return lines;
    }

    std::vector<std::string> texts() const
    {
        std::vector<std::string> out;
        for (const auto& line : all())
            out.push_back (line.text);
        return out;
    }

    std::optional<Line> find (const std::string& start) const
    {
        for (const auto& line : all())
            if (line.text.rfind (start, 0) == 0)
                return line;
        return std::nullopt;
    }

    bool waitFor (const std::string& start, int millis = 3000) const
    {
        const auto end = Steady::now() + std::chrono::milliseconds (millis);
        while (Steady::now() < end)
        {
            if (find (start))
                return true;
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
        return false;
    }

    bool waitForCount (const std::string& start, int wanted, int millis = 3000) const
    {
        const auto end = Steady::now() + std::chrono::milliseconds (millis);
        while (Steady::now() < end)
        {
            const auto lines = all();
            if (std::count_if (lines.begin(), lines.end(), [&] (const Line& line) { return line.text.rfind (start, 0) == 0; }) >= wanted)
                return true;
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
        return false;
    }

    // The index of the first line starting with `start`, or -1.
    int indexOf (const std::string& start) const
    {
        const auto lines = all();
        for (size_t i = 0; i < lines.size(); ++i)
            if (lines[i].text.rfind (start, 0) == 0)
                return (int) i;
        return -1;
    }

private:
    void holdIfAsked (const std::string& line)
    {
        std::unique_lock hold (gateLock);
        if (holdPrefix.empty() || line.rfind (holdPrefix, 0) != 0 || released)
            return;
        held = true;
        gate.notify_all();
        gate.wait (hold, [this] { return released; });
    }

    mutable std::mutex lock;
    std::vector<Line> lines;
    std::mutex gateLock;
    std::condition_variable gate;
    std::string holdPrefix;
    bool held = false, released = false;
};

class RecordingClock final : public Clock
{
public:
    double monotonic() override
    {
        note();
        return std::chrono::duration<double> (Steady::now().time_since_epoch()).count();
    }
    double wall() override
    {
        note();
        return std::chrono::duration<double> (std::chrono::system_clock::now().time_since_epoch()).count();
    }
    std::set<std::thread::id> threads() const
    {
        const std::scoped_lock hold (lock);
        return callers;
    }

private:
    void note()
    {
        const std::scoped_lock hold (lock);
        callers.insert (std::this_thread::get_id());
    }
    mutable std::mutex lock;
    std::set<std::thread::id> callers;
};

class FrozenClock final : public Clock
{
public:
    double monotonic() override { return 4242.5; }
    double wall() override { return 1800000000.0; }
};

void setListenerPort (juce::var& data, const std::string& name, int port)
{
    for (auto& listener : *data["listeners"].getArray())
        if (listener["name"].toString().toStdString() == name)
            listener.getDynamicObject()->setProperty ("port", port);
}

// One service, its fake devices, a home and a state directory of its own.
class Rig
{
public:
    Socket mixer, motion, engine, deskOsc, deskVu;
    Socket announced { "0.0.0.0", 0 };   // the broadcast reaches a socket on every interface
    const int vuPort = freePort();
    juce::File dir, home, stateDir, share, truthFile;
    Said said;
    std::unique_ptr<CoreService> service;
    std::string error;

    // `data` is the truth before its ports are moved: B0's recorded one unless a test names another.
    explicit Rig (juce::var data = b0::truth().data())
        : dir (juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a3-core-service", "", false))
    {
        home = dir.getChildFile ("home");
        stateDir = dir.getChildFile ("state");
        share = dir.getChildFile ("share");
        home.createDirectory();
        stateDir.createDirectory();
        share.getChildFile ("pipewire").createDirectory();
        juce::File (A3_CORE_TEST_DATA "/parity/render/aes67.conf.in").copyFileTo (share.getChildFile ("pipewire/pipewire-aes67.conf.in"));

        setListenerPort (data, "core.vu-relay", vuPort);
        setListenerPort (data, "devices.announce", announced.port());
        setListenerPort (data, "engine.osc", engine.port());
        setListenerPort (data, "desk.osc", deskOsc.port());
        setListenerPort (data, "desk.vu", deskVu.port());
        for (const auto* host : { "core", "mixer", "radla" })
            data["hosts"].getDynamicObject()->setProperty (host, juce::String (loopbackAddress()));
        data["network"].getDynamicObject()->setProperty ("address", juce::String (loopbackAddress() + "/8"));
        truthFile = dir.getChildFile ("a3-osc.json");
        truthFile.replaceWithText (dumpIndented (data, 2));
    }

    ~Rig()
    {
        service.reset();
        dir.deleteRecursively();
    }

    Truth truth (std::optional<juce::File> network = std::nullopt) const { return std::get<Truth> (Truth::load (truthFile, network)); }

    ServiceOptions options() const
    {
        ServiceOptions o;
        o.core = CoreArgs { loopbackAddress(), 0, mixer.endpoint(), motion.endpoint(), {} };
        o.webBind = loopbackAddress() + ":0";
        o.home = home;
        o.stateDir = stateDir;
        o.packageShare = share;
        o.hostname = "test-host";
        o.truthPath = truthFile;
        return o;
    }

    bool start (ServiceOptions o)
    {
        auto started = CoreService::start (std::move (o), said.journal());
        if (auto* failed = std::get_if<std::string> (&started))
        {
            error = *failed;
            return false;
        }
        service = std::move (std::get<std::unique_ptr<CoreService>> (started));
        return true;
    }

    bool start() { return start (options()); }

    int port() const { return service->mainPort().port; }

    void hello (const Socket& from, const std::string& device)
    {
        from.osc (port(), "/device/hello", { pyStr (device), pyStr (truth().fingerprint()) });
    }

    juce::var traffic() const
    {
        const auto reply = httpclient::get (service->webPort().port, "/api/traffic");
        juce::String problem;
        return parseJson (juce::String (reply.body), problem);
    }
};

std::string coreFile (const Rig& rig, const char* name)
{
    return rig.stateDir.getChildFile ("a3-core").getChildFile (name).loadFileAsString().toStdString();
}

// A source file with its comments taken out and its string literals kept (they are code).
std::string withoutComments (const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size();)
    {
        if (text.compare (i, 2, "//") == 0)
        {
            i = text.find ('\n', i);
            if (i == std::string::npos)
                break;
            continue;
        }
        if (text.compare (i, 2, "/*") == 0)
        {
            const auto end = text.find ("*/", i + 2);
            i = end == std::string::npos ? text.size() : end + 2;
            continue;
        }
        if (text[i] == '"' || text[i] == '\'')
        {
            const char quote = text[i];
            size_t j = i + 1;
            while (j < text.size() && text[j] != quote)
                j += text[j] == '\\' ? 2 : 1;
            out += text.substr (i, j + 1 - i);
            i = j + 1;
            continue;
        }
        out += text[i++];
    }
    return out;
}

std::vector<juce::File> sourcesOf (const juce::File& dir)
{
    auto found = dir.findChildFiles (juce::File::findFiles, false, "*.cpp;*.h");
    std::vector<juce::File> out (found.begin(), found.end());
    std::sort (out.begin(), out.end());
    return out;
}

std::vector<std::string> stringLiterals (const std::string& code)
{
    std::vector<std::string> out;
    static const std::regex literal (R"x("((?:[^"\\]|\\.)*)")x");
    for (std::sregex_iterator it (code.begin(), code.end(), literal), end; it != end; ++it)
        out.push_back ((*it)[1]);
    return out;
}

// Numbers of four or five digits standing alone: not part of a name, a decimal or a hex literal.
std::vector<int> standaloneNumbers (const std::string& code)
{
    std::vector<int> out;
    const auto word = [] (char c) { return std::isalnum ((unsigned char) c) || c == '_' || c == '.'; };
    for (size_t i = 0; i < code.size();)
    {
        if (! std::isdigit ((unsigned char) code[i]) || (i > 0 && word (code[i - 1])))
        {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < code.size() && std::isdigit ((unsigned char) code[j]))
            ++j;
        if (j - i >= 4 && j - i <= 5 && (j == code.size() || ! word (code[j])))
            out.push_back (std::stoi (code.substr (i, j - i)));
        i = j;
    }
    return out;
}

// test_core_reads_the_truth.truth_ports: where Core listens and where its routes lead.
std::set<int> truthPorts()
{
    const auto data = b0::truth().data();
    std::set<std::string> reached;
    for (const auto& route : *data["routes"].getArray())
        if (route["from"].toString() == "core")
            reached.insert (route["to"].toString().toStdString());
    std::set<int> ports;
    for (const auto& listener : *data["listeners"].getArray())
        if (listener["program"].toString() == "core" || reached.count (listener["name"].toString().toStdString()) > 0)
            ports.insert ((int) listener["port"]);
    return ports;
}

std::string readSource (const juce::File& file) { return withoutComments (file.loadFileAsString().toStdString()); }
}

//==============================================================================
// Start-up and the main port.

TEST (CoreService, StartsOnEphemeralPortsAndAnswersARecall)
{
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    EXPECT_NE (rig.port(), 0);
    EXPECT_EQ (rig.service->vuRelayPort().port, rig.vuPort);
    EXPECT_NE (rig.service->webPort().port, 0);
    rig.mixer.messages (200);   // what was said at start

    rig.mixer.osc (rig.port(), "/state/recall");
    ASSERT_TRUE (rig.said.waitFor ("/state/recall: replayed "));
    // The whole state, to every subscriber: the mixer hears as many messages as the line says.
    const auto line = rig.said.find ("/state/recall: replayed ")->text;
    const auto told = std::stoul (line.substr (std::string ("/state/recall: replayed ").size()));
    EXPECT_GT (told, 0u);
    EXPECT_GE (rig.mixer.messages (300).size(), told);
}

juce::var packageTruth() { return std::get<Truth> (Truth::load (juce::File (A3_PACKAGE_TRUTH), std::nullopt)).data(); }

TEST (CoreService, CoreStartsOnThePackageTruth)
{
    // The truth that ships has no REAPER since 2026-10-27: no reaper, iem or dualdelay is a peer of
    // Core's any more, so those names are free for a subscriber.
    Rig rig (packageTruth());
    const Socket reaper, iem, dualdelay;
    auto options = rig.options();
    options.core.subscribers = { "reaper=" + reaper.endpoint().host + ":" + std::to_string (reaper.port()),
                                 "iem=" + iem.endpoint().host + ":" + std::to_string (iem.port()),
                                 "dualdelay=" + dualdelay.endpoint().host + ":" + std::to_string (dualdelay.port()) };
    ASSERT_TRUE (rig.start (options)) << rig.error;
    EXPECT_NE (rig.port(), 0);
    EXPECT_EQ (rig.service->vuRelayPort().port, rig.vuPort);
    EXPECT_NE (rig.service->webPort().port, 0);
    for (const auto* name : { "reaper", "iem", "dualdelay" })
        EXPECT_TRUE (rig.said.find (std::string ("subscriber ") + name + " at ")) << name;
}

TEST (CoreService, TheEngineReplayComesBackThroughCoresOwnPort)
{
    // The replay and the token recall go to Core's own port from the one socket the hello gate
    // trusts: the recall that carries the token opens the engine.
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const Socket engineSays;
    engineSays.osc (rig.port(), "/engine/hello", { pyStr ("boot-1") });
    EXPECT_TRUE (rig.engine.awaitAddress (rig.truth().address ("engine.open")).has_value());
    EXPECT_TRUE (rig.said.find ("engine: hello from boot-1, replaying the evening").has_value());
}

TEST (CoreService, TheStartSequenceSaysWhatB1Says)
{
    Rig rig;
    const Socket light;
    auto options = rig.options();
    options.core.subscribers = { "light=" + loopbackAddress() + ":" + std::to_string (light.port()) };
    ASSERT_TRUE (rig.start (options)) << rig.error;
    const auto main = rig.port(), web = rig.service->webPort().port;
    const auto local = loopbackAddress();
    const std::vector<std::string> expected {
        "audio engine: engine",
        "subscriber light at " + local + ":" + std::to_string (light.port()),
        "forwarding the analyzer's meters from ('" + local + "', " + std::to_string (rig.vuPort) + ") to a remote Motion and every desk followed",
        "startup: spoke the remembered state, ",
        "0 addresses remembered from " + rig.stateDir.getChildFile ("a3-core/seen.json").getFullPathName().toStdString(),
        "window on http://" + local + ":" + std::to_string (web),
        "Serving on ('" + local + "', " + std::to_string (main) + ")",
        "aes67: ",
        "announcing the truth to 127.255.255.255:" + std::to_string (rig.announced.port()),
    };
    const auto lines = rig.said.texts();
    ASSERT_FALSE (lines.empty());
    EXPECT_EQ (lines.front(), expected.front());
    int last = -1;
    for (const auto& start : expected)
    {
        const auto at = rig.said.indexOf (start);
        EXPECT_GT (at, last) << start;
        last = std::max (last, at);
    }
    for (const auto& line : rig.said.all())
        EXPECT_FALSE (line.toStderr) << line.text;
}

TEST (CoreService, BusyMainPortExitsWithAReason)
{
    Rig rig;
    const Socket taken;
    auto options = rig.options();
    options.core.port = taken.port();
    const auto before = threadCount();
    EXPECT_FALSE (rig.start (options));
    EXPECT_EQ (rig.error, "a3-core: cannot listen on " + loopbackAddress() + ":" + std::to_string (taken.port())
                              + ": [Errno 98] Address already in use");
    // Where B1 fell over: after it spoke and opened the window, before it served.
    EXPECT_TRUE (rig.said.find ("startup: spoke the remembered state").has_value());
    EXPECT_TRUE (rig.said.find ("window on http://").has_value());
    EXPECT_FALSE (rig.said.find ("Serving on").has_value());
    EXPECT_EQ (threadCount(), before);
}

TEST (CoreService, BusyVuPortIsALineNotAFailure)
{
    Rig rig;
    const Socket taken (loopbackAddress(), rig.vuPort);
    ASSERT_TRUE (taken.ok());
    ASSERT_TRUE (rig.start()) << rig.error;
    const auto line = rig.said.find ("no meters for a remote Motion or the desks: ");
    ASSERT_TRUE (line.has_value());
    EXPECT_EQ (line->text, "no meters for a remote Motion or the desks: [Errno 98] Address already in use");
    EXPECT_TRUE (line->toStderr);
    EXPECT_FALSE (rig.said.find ("forwarding the analyzer's meters").has_value());
    EXPECT_EQ (rig.service->vuRelayPort().port, 0);
    EXPECT_LT (rig.said.indexOf ("no meters"), rig.said.indexOf ("startup: spoke"));
    EXPECT_TRUE (rig.said.find ("Serving on").has_value());
}

TEST (CoreService, AnUnreadableTruthRefusesTheStart)
{
    // a3_osc.load raises at B1's import: Core never comes up. Nothing is said before; the device
    // table is built from the joined truth's fingerprint, never from a hash of an unread file.
    Rig rig;
    auto options = rig.options();
    options.truthPath = rig.dir.getChildFile ("missing.json");
    EXPECT_FALSE (rig.start (options));
    const auto refused = std::get<TruthError> (Truth::load (rig.dir.getChildFile ("missing.json"), std::nullopt));
    EXPECT_EQ (rig.error, "a3-core: " + refused.what);
    EXPECT_TRUE (rig.said.all().empty());
}

TEST (CoreService, ARefusedSubscriberRefusesTheStart)
{
    Rig rig;
    auto options = rig.options();
    options.core.subscribers = { "nonsense" };
    const auto before = threadCount();
    EXPECT_FALSE (rig.start (options));
    std::string problem;
    try
    {
        parseSubscribers ({ "nonsense" }, shipped);
    }
    catch (const SubscriberError& refused)
    {
        problem = refused.what();
    }
    ASSERT_FALSE (problem.empty());
    EXPECT_EQ (rig.error, "a3-core: " + problem);
    EXPECT_EQ (rig.said.texts(), (std::vector<std::string> { "audio engine: engine" }));
    EXPECT_EQ (threadCount(), before);
}

//==============================================================================
// One serve thread (P8).

TEST (CoreService, OneServeThreadHandlesEverything)
{
    Rig rig;
    RecordingClock clock;
    auto options = rig.options();
    options.noWeb = true;
    options.clock = &clock;
    ASSERT_TRUE (rig.start (options)) << rig.error;
    rig.mixer.osc (rig.port(), "/state/recall");
    rig.mixer.osc (rig.port(), "/channel/1/gain", { pyFloat (0.3) });
    rig.hello (rig.mixer, "desk");
    rig.mixer.sendTo (rig.vuPort, encodeMessage ({ "/vu/1", { pyFloat (0.5), pyFloat (0.25) } }));
    EXPECT_TRUE (rig.said.waitFor ("desk: "));
    EXPECT_TRUE (rig.deskVu.receive (2000).has_value());
    std::this_thread::sleep_for (std::chrono::milliseconds (300));   // ticks
    rig.service->stop();

    const auto threads = clock.threads();
    ASSERT_EQ (threads.size(), 1u);
    const auto serveThread = *threads.begin();
    EXPECT_NE (serveThread, std::this_thread::get_id());
    for (const auto& line : rig.said.all())
        EXPECT_EQ (line.thread, serveThread) << line.text;
}

TEST (CoreService, TheMetersAreRelayedOnTheServeThread)
{
    // The relay's datagrams go to Core::handleVu on the serve thread: its destinations are the
    // followers' state, which only that thread may read.
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    rig.hello (rig.mixer, "desk");
    ASSERT_TRUE (rig.said.waitFor ("desk: " + loopbackAddress() + " joined"));
    const auto bundle = encodeMessage ({ "/vu/1", { pyFloat (0.5), pyFloat (0.25) } });
    rig.mixer.sendTo (rig.vuPort, bundle);
    const auto got = rig.deskVu.receive (2000);
    ASSERT_TRUE (got.has_value());
    EXPECT_EQ (got->data, bundle);
    for (const auto& line : rig.said.all())
        EXPECT_FALSE (line.toStderr) << line.text;
}

TEST (CoreService, APositionFloodDoesNotDelayADeskCommand)
{
    // A real backlog: the serve thread is held inside a recall's handler while 5000 positions and then
    // a desk command queue up. Released, it drains at most 1024 a pass and handles only the newest
    // position of each pass: the engine hears one azimuth per pass, the last one the final value, and
    // the lamp comes with the last pass instead of after 5000 handlings.
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const Socket engineSays;
    engineSays.osc (rig.port(), "/engine/hello", { pyStr ("boot-1") });
    ASSERT_TRUE (rig.engine.awaitAddress (rig.truth().address ("engine.open")).has_value());
    rig.engine.messages (200);
    rig.mixer.messages (200);
    rig.said.holdAt ("/state/recall: replayed ");
    rig.mixer.osc (rig.port(), "/state/recall");
    ASSERT_TRUE (rig.said.waitUntilHeld());
    const Socket flood;
    constexpr int positions = 5000;
    const auto valueOf = [] (int n) { return (float) ((n % 180) - 90); };
    for (int n = 0; n < positions; ++n)
        flood.osc (rig.port(), "/channel/1/azimuth", { pyFloat (valueOf (n)) });
    flood.osc (rig.port(), "/channel/1/cue", { pyStr ("1") });
    const auto released = Steady::now();
    rig.said.release();

    ASSERT_TRUE (rig.mixer.awaitAddress ("/channel/1/cue/led", 3000).has_value());
    EXPECT_LT (std::chrono::duration<double> (Steady::now() - released).count(), 0.5);
    std::vector<float> azimuths;
    for (const auto& message : rig.engine.messages (300))
        if (message.address == "/channel/1/azimuth")
            azimuths.push_back (std::get<float> (message.args.at (0).value));
    ASSERT_FALSE (azimuths.empty());
    EXPECT_LE (azimuths.size(), (size_t) ((positions + 1 + 1023) / 1024));   // one per pass at most
    EXPECT_EQ (azimuths.back(), valueOf (positions - 1));
}

TEST (CoreService, BothPortsAreReadByABlockingServer)
{
    // The main port and the meters' inbox are read by the one serve loop: a burst on both starts no
    // thread.
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    rig.hello (rig.mixer, "desk");
    ASSERT_TRUE (rig.said.waitFor ("desk: "));
    const auto before = threadCount();
    int peak = before;
    const auto meter = encodeMessage ({ "/vu/1", { pyFloat (0.5), pyFloat (0.25) } });
    for (int n = 0; n < 200; ++n)
    {
        rig.mixer.osc (rig.port(), "/channel/2/gain", { pyFloat ((n + 1) / 1000.0) });
        rig.mixer.sendTo (rig.vuPort, meter);
        peak = std::max (peak, threadCount());
    }
    int meters = 0;
    while (rig.deskVu.receive (300))
        ++meters;
    EXPECT_EQ (meters, 200);
    EXPECT_EQ (peak, before);
}

TEST (CoreService, ABurstArrivesInOrderWithoutNewThreads)
{
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    rig.motion.messages (200);
    const auto before = threadCount();
    int peak = before;
    for (int n = 0; n < 200; ++n)
    {
        rig.mixer.osc (rig.port(), "/channel/1/gain", { pyFloat ((n + 1) / 1000.0) });
        peak = std::max (peak, threadCount());
    }
    std::vector<float> received;
    for (const auto& message : rig.motion.messages (500))
        if (message.address == "/channel/1/gain")
            received.push_back (std::get<float> (message.args.at (0).value));
    ASSERT_EQ (received.size(), 200u);
    for (int n = 0; n < 200; ++n)
        EXPECT_FLOAT_EQ (received[(size_t) n], (float) ((n + 1) / 1000.0)) << n;
    EXPECT_EQ (peak, before);
}

//==============================================================================
// The doors out.

TEST (CoreService, TrafficOutIsCountedOnceAndSelfSendsNotAtAll)
{
    // Core::send writes the row; UdpNet only sends. Core's own replays (the token recall) are no
    // traffic out: they are counted once, when they come back in.
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const Socket engineSays;
    engineSays.osc (rig.port(), "/engine/hello", { pyStr ("boot-1") });
    ASSERT_TRUE (rig.said.waitFor ("/state/recall: replayed "));   // the token recall, through Core's own port
    rig.mixer.osc (rig.port(), "/state/recall");
    ASSERT_TRUE (rig.said.waitForCount ("/state/recall: replayed ", 2));

    std::map<std::string, long long> arrived;
    for (const auto* device : { &rig.mixer, &rig.motion, &rig.engine })
        for (const auto& message : device->messages (300))
            ++arrived[message.address];
    ASSERT_GT (arrived.count (rig.truth().address ("engine.open")), 0u);
    std::map<std::string, long long> counted;
    long long recallsIn = 0;
    const auto traffic = rig.traffic();
    for (const auto& row : *traffic["rows"].getArray())
    {
        const auto address = row["address"].toString().toStdString();
        if (row["direction"].toString() == "in" && address == "/state/recall")
            recallsIn += (juce::int64) row["count"];
        if (row["direction"].toString() == "out")
            counted[address] += (juce::int64) row["count"];
    }
    EXPECT_EQ (counted, arrived);
    EXPECT_EQ (counted.count ("/state/recall"), 0u);
    EXPECT_EQ (recallsIn, 2);   // the mixer's and Core's own token recall
}

TEST (CoreService, EveryTrafficTimeIsTheInjectedClock)
{
    Rig rig;
    FrozenClock clock;
    auto options = rig.options();
    options.clock = &clock;
    ASSERT_TRUE (rig.start (options)) << rig.error;
    rig.mixer.osc (rig.port(), "/state/recall");
    ASSERT_TRUE (rig.said.waitFor ("/state/recall: replayed "));
    const auto traffic = rig.traffic();
    ASSERT_GT (traffic["rows"].size(), 1);
    std::set<juce::String> directions;
    for (const auto& row : *traffic["rows"].getArray())
    {
        // Traffic keeps its stamps in order: a frozen clock's become 4242.5 plus a few ulps.
        EXPECT_NEAR ((double) row["last_seen"], 4242.5, 1e-6) << row["address"].toString();
        directions.insert (row["direction"].toString());
    }
    EXPECT_EQ (directions, (std::set<juce::String> { "in", "out" }));
}

TEST (CoreService, AMessageThatCannotBeEncodedIsSaidOnce)
{
    UdpSocket self;
    UdpNet net (self);
    const Socket mixer;
    Said said;
    SendFailures failures (said.journal());
    const Client client { "mixer", mixer.endpoint() };
    for (int i = 0; i < 2; ++i)
        failures.send ("mixer", [&] { net.send (client, OscMessage { "", { pyFloat (1.0) } }); });
    EXPECT_FALSE (mixer.receive (100).has_value());
    ASSERT_EQ (said.texts().size(), 1u);
    EXPECT_EQ (said.texts()[0], "mixer: send failed (cannot encode the message to '' as OSC); said once until it gets through again");
    failures.send ("mixer", [&] { net.send (client, OscMessage { "/channel/1/gain", { pyFloat (1.0) } }); });
    EXPECT_TRUE (mixer.receive (1000).has_value());
    EXPECT_EQ (said.texts().back(), "mixer: sends get through again");
}

TEST (CoreService, TheSelfClientLeavesByTheTrustedSocket)
{
    auto bound = UdpSocket::bound (loopbackAddress(), 0);
    auto& self = std::get<UdpSocket> (bound);
    UdpNet net (self);
    const Socket core;
    net.send ({ "self", core.endpoint() }, { "/state/recall", { pyInt (7) } });
    net.send ({ "mixer", core.endpoint() }, { "/state/recall", { pyInt (7) } });
    const auto first = core.receive (1000), second = core.receive (1000);
    ASSERT_TRUE (first && second);
    EXPECT_EQ (first->fromPort, self.local().port);
    EXPECT_NE (second->fromPort, self.local().port);
}

TEST (CoreService, TheReceiveBuffersAreWidened)
{
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const auto rmemMax = juce::File ("/proc/sys/net/core/rmem_max").loadFileAsString().trim().getIntValue();
    const auto wanted = rmemMax > 0 ? std::min (wantedReceiveBuffer, rmemMax) : wantedReceiveBuffer;
    const auto [main, self] = rig.service->receiveBuffers();
    EXPECT_GE (main, wanted);
    EXPECT_GE (self, wanted);
}

//==============================================================================
// Stop.

TEST (CoreService, StopFlushesStateAndEvening)
{
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const Socket engineSays;
    engineSays.osc (rig.port(), "/engine/hello", { pyStr ("boot-1") });
    ASSERT_TRUE (rig.engine.awaitAddress (rig.truth().address ("engine.open")).has_value());
    rig.mixer.osc (rig.port(), "/channel/3/gain", { pyFloat (0.375) });
    rig.mixer.osc (rig.port(), "/channel/3/cue", { pyStr ("1") });
    ASSERT_TRUE (rig.motion.awaitAddress ("/channel/3/cue").has_value());
    const auto started = Steady::now();
    rig.service->stop();
    EXPECT_LT (std::chrono::duration<double> (Steady::now() - started).count(), 1.0);

    juce::String problem;
    const auto state = parseJson (juce::String (coreFile (rig, "state.json")), problem);
    ASSERT_GT (state["channels"].size(), 2) << coreFile (rig, "state.json");
    EXPECT_TRUE ((bool) state["channels"][2]["toggle_cue"]) << coreFile (rig, "state.json");
    const auto evening = parseJson (juce::String (coreFile (rig, "evening.json")), problem);
    ASSERT_TRUE (evening["values"].isObject()) << coreFile (rig, "evening.json");
    EXPECT_EQ ((double) evening["values"]["/channel/3/gain"], 0.375) << coreFile (rig, "evening.json");
    EXPECT_NE (coreFile (rig, "seen.json").find ("/channel/3/gain"), std::string::npos);
}

TEST (CoreService, StopJoinsEveryThread)
{
    const auto before = threadCount();
    {
        Rig rig;
        ASSERT_TRUE (rig.start()) << rig.error;
        EXPECT_EQ (httpclient::get (rig.service->webPort().port, "/api/traffic").status, 200);
        EXPECT_GT (threadCount(), before);
        const auto started = Steady::now();
        rig.service->stop();
        rig.service->stop();
        EXPECT_LT (std::chrono::duration<double> (Steady::now() - started).count(), 1.0);
        EXPECT_EQ (threadCount(), before);
    }
    EXPECT_EQ (threadCount(), before);
}

//==============================================================================
// test_core_announce: CoreAnnounces, CoreRendersForItsNeighbours.

TEST (CoreService, AnnouncesEveryTwoSeconds)
{
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const auto first = rig.announced.receive (1000);
    ASSERT_TRUE (first.has_value());
    const auto firstAt = Steady::now();
    const auto second = rig.announced.receive (3000);
    ASSERT_TRUE (second.has_value());
    const auto gap = std::chrono::duration<double> (Steady::now() - firstAt).count();
    EXPECT_GT (gap, 1.8);
    EXPECT_LT (gap, 2.5);
    const auto message = decodeMessage (first->data.data(), first->data.size());
    ASSERT_TRUE (message.has_value());
    const auto truth = rig.truth();
    EXPECT_EQ (message->address, truth.address ("core.here"));
    ASSERT_EQ (message->args.size(), 3u);
    EXPECT_EQ (std::get<std::string> (message->args[0].value), truthUrl (truth));
    EXPECT_EQ (std::get<std::string> (message->args[1].value), truth.fingerprint());
    EXPECT_EQ (std::get<std::string> (message->args[2].value).size(), 16u);
    EXPECT_EQ (first->data, second->data);   // one boot id for the whole run
}

TEST (CoreService, TheAnnouncementRunsInItsOwnThread)
{
    Rig rig;
    auto options = rig.options();
    options.noWeb = true;
    const auto before = threadCount();
    ASSERT_TRUE (rig.start (options)) << rig.error;
    EXPECT_TRUE (rig.announced.receive (1000).has_value());
    EXPECT_EQ (threadCount(), before + 2);   // the serve thread and the announcer, nothing else
    const auto names = threadNames();
    EXPECT_EQ (names.count ("a3-announce"), 1u);
    EXPECT_EQ (names.count ("a3-core-serve"), 1u);
}

TEST (CoreService, TheWindowServesTheTruth)
{
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const auto reply = httpclient::get (rig.service->webPort().port, "/api/truth");
    EXPECT_EQ (reply.status, 200);
    EXPECT_EQ (reply.body, rig.truth().canonical());
    EXPECT_EQ (reply.headers.at ("x-a3-truth"), rig.truth().fingerprint());
}

TEST (CoreService, NoWebOpensNoWindow)
{
    Rig rig;
    auto options = rig.options();
    options.noWeb = true;
    ASSERT_TRUE (rig.start (options)) << rig.error;
    EXPECT_EQ (rig.service->webPort().port, 0);
    EXPECT_FALSE (rig.said.find ("window on").has_value());
}

TEST (CoreService, ARefusedNetworkFileIsSaid)
{
    Rig rig;
    const auto network = rig.dir.getChildFile ("network.json");
    network.replaceWithText ("{ not json");
    auto options = rig.options();
    options.networkPath = network;
    ASSERT_TRUE (rig.start (options)) << rig.error;
    const auto problem = rig.truth (network).networkProblem;
    ASSERT_TRUE (problem.has_value());
    const auto line = rig.said.find ("network.json refused, the package's defaults are used: ");
    ASSERT_TRUE (line.has_value());
    EXPECT_EQ (line->text, "network.json refused, the package's defaults are used: " + *problem);
    EXPECT_TRUE (line->toStderr);
    EXPECT_GT (rig.said.indexOf ("network.json refused"), rig.said.indexOf ("Serving on"));
    EXPECT_LT (rig.said.indexOf ("network.json refused"), rig.said.indexOf ("aes67: "));
}

TEST (CoreService, CoreSaysARefusedBlock)
{
    Rig rig;
    const auto network = rig.dir.getChildFile ("network.json");
    juce::String problemText;
    auto blocks = parseJson (juce::String (networkFile (rig.truth())), problemText);
    auto* meters = new juce::DynamicObject();
    meters->setProperty ("attack_ms", "fast");
    blocks.getDynamicObject()->setProperty ("meters", juce::var (meters));
    network.replaceWithText (dumpIndented (blocks, 2));
    auto options = rig.options();
    options.networkPath = network;
    ASSERT_TRUE (rig.start (options)) << rig.error;
    const auto problem = rig.truth (network).metersProblem;
    ASSERT_TRUE (problem.has_value());
    const auto line = rig.said.find ("network.json's meters refused, the package's are used: ");
    ASSERT_TRUE (line.has_value());
    EXPECT_EQ (line->text, "network.json's meters refused, the package's are used: " + *problem);
    EXPECT_TRUE (line->toStderr);
    EXPECT_FALSE (rig.said.find ("network.json refused").has_value());
}

TEST (CoreService, WritesTheUserFilesAtStart)
{
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const auto truth = rig.truth();
    EXPECT_EQ (rig.home.getChildFile (envFilePath).loadFileAsString().toStdString(), oscEnv (truth));
    EXPECT_EQ (rig.home.getChildFile (analyzerConfPath).loadFileAsString().toStdString(), analyzerBlock (truth));
}

TEST (CoreService, CoreWritesTheUserFilesAtStart)
{
    // The AES67 line is said where B1 says it: after the main port, before the announcer.
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const auto other = rig.dir.getChildFile ("other-home");
    other.createDirectory();
    const auto expected = writeAes67Conf (rig.truth(), other, "test-host", rig.share.getChildFile ("pipewire/pipewire-aes67.conf.in"));
    const auto line = rig.said.find ("aes67: ");
    ASSERT_TRUE (line.has_value());
    EXPECT_EQ (line->text, "aes67: " + expected);
    EXPECT_GT (rig.said.indexOf ("aes67: "), rig.said.indexOf ("Serving on"));
    EXPECT_LT (rig.said.indexOf ("aes67: "), rig.said.indexOf ("announcing the truth to "));
}

TEST (CoreService, AFailedRenderDoesNotStopCore)
{
    Rig rig;
    rig.home.getChildFile (".config").replaceWithText ("a file where a directory is due");
    ASSERT_TRUE (rig.start()) << rig.error;
    const auto line = rig.said.find ("could not render osc.env / the analyzer's conf.d file / the AES67 config: ");
    ASSERT_TRUE (line.has_value());
    EXPECT_TRUE (line->toStderr);
    EXPECT_FALSE (rig.said.find ("aes67: ").has_value());
    EXPECT_TRUE (rig.said.find ("announcing the truth to ").has_value());
    rig.mixer.osc (rig.port(), "/state/recall");
    EXPECT_TRUE (rig.said.waitFor ("/state/recall: replayed "));
}

//==============================================================================
// test_core_reads_the_truth, test_core_servers_do_not_spawn: what the sources may not hold.

TEST (CoreService, NoIpAddressInTheCode)
{
    static const std::regex ipv4 (R"(\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b)");
    const auto sources = sourcesOf (coreSources);
    ASSERT_GT (sources.size(), 10u);
    for (const auto& file : sources)
        for (const auto& literal : stringLiterals (readSource (file)))
            if (literal.find (":PORT") == std::string::npos)   // a help text's example of the format
                EXPECT_FALSE (std::regex_search (literal, ipv4)) << file.getFileName() << " \"" << literal << "\"";
}

TEST (CoreService, NoPortOfTheTruthInTheCode)
{
    const auto ports = truthPorts();
    ASSERT_GT (ports.count (9000), 0u);
    for (const auto& file : sourcesOf (coreSources))
        for (const auto number : standaloneNumbers (readSource (file)))
            EXPECT_EQ (ports.count (number), 0u) << file.getFileName() << " " << number;
}

TEST (CoreService, NoHandlerSleeps)
{
    // A serve loop stalls its whole port if a handler sleeps. Python allowed the project-save
    // thread's sleep; engine mode has none, and the announcer waits on a condition, not a sleep.
    static const std::regex sleeps (R"(sleep_for|sleep_until|usleep|nanosleep|Thread::sleep|\bsleep\s*\()");
    for (const auto& file : sourcesOf (coreSources))
    {
        const auto name = file.getFileName();
        if (! name.startsWith ("Core") && ! name.startsWith ("UdpNet") && ! name.startsWith ("LatestWins"))
            continue;
        EXPECT_FALSE (std::regex_search (readSource (file), sleeps)) << name;
    }
}

TEST (CoreService, NoThreadingServerIsLeft)
{
    // No thread per packet: the router and its sockets start none; the service starts two (the serve
    // loop and the announcer) and the window's own server its connections.
    static const std::regex spawns (R"(std::thread\s*\(|std::thread\s*\{|std::async|juce::Thread|\.detach\s*\()");
    for (const auto& file : sourcesOf (coreSources))
    {
        const auto name = file.getFileName();
        const auto code = readSource (file);
        const auto count = std::distance (std::sregex_iterator (code.begin(), code.end(), spawns), std::sregex_iterator());
        if (name == "CoreService.cpp")
            EXPECT_EQ (count, 2) << name;
        else if (name.startsWith ("Core") || name.startsWith ("UdpNet") || name.startsWith ("LatestWins"))
            EXPECT_EQ (count, 0) << name;
    }
}

//==============================================================================
// Fix round 1: where the main port opens, and a serve loop that ends on its own.

TEST (CoreService, TheMainPortOpensWhereB1OpensIt)
{
    // B1 binds the main port after the window, just before "Serving on": what arrives while it speaks
    // the remembered state is refused by the system, not queued.
    Rig rig;
    auto options = rig.options();
    options.core.port = freePort();
    rig.said.holdAt ("window on http://");
    std::thread starting ([&] { rig.start (options); });
    ASSERT_TRUE (rig.said.waitUntilHeld());
    {
        const Socket probe (loopbackAddress(), options.core.port);
        EXPECT_TRUE (probe.ok()) << "the main port is bound before B1 binds it";
    }
    rig.said.release();
    starting.join();
    ASSERT_TRUE (rig.service != nullptr) << rig.error;
    EXPECT_EQ (rig.port(), options.core.port);
}

namespace
{
// The service's own descriptor for a UDP socket bound to `port`.
int descriptorBoundTo (int port)
{
    for (const auto& entry : juce::File ("/proc/self/fd").findChildFiles (juce::File::findFilesAndDirectories, false))
    {
        const auto fd = entry.getFileName().getIntValue();
        int type = 0;
        socklen_t length = sizeof type;
        if (::getsockopt (fd, SOL_SOCKET, SO_TYPE, &type, &length) != 0 || type != SOCK_DGRAM)
            continue;
        sockaddr_in at {};
        socklen_t atLength = sizeof at;
        if (::getsockname (fd, (sockaddr*) &at, &atLength) == 0 && at.sin_family == AF_INET && ntohs (at.sin_port) == port)
            return fd;
    }
    return -1;
}
}

TEST (CoreService, AServeLoopThatEndsOnItsOwnIsSeen)
{
    Rig rig;
    auto options = rig.options();
    options.noWeb = true;
    ASSERT_TRUE (rig.start (options)) << rig.error;
    EXPECT_FALSE (rig.service->waitUntilFinished (0.2).has_value());   // serving

    const auto fd = descriptorBoundTo (rig.port());
    ASSERT_GE (fd, 0);
    ::close (fd);   // pulled from under the loop: its next poll reports POLLNVAL
    const auto reason = rig.service->waitUntilFinished (2.0);
    ASSERT_TRUE (reason.has_value());
    EXPECT_EQ (*reason, "the serve loop ended: poll: the socket is not open");
    const auto line = rig.said.find ("the serve loop ended: ");
    ASSERT_TRUE (line.has_value());
    EXPECT_TRUE (line->toStderr);
    // Core's stop ran on the way out: the address list is on disk.
    EXPECT_TRUE (rig.stateDir.getChildFile ("a3-core/seen.json").existsAsFile());
    rig.service.reset();   // before anything here opens a descriptor that could take the closed number
}

TEST (CoreService, AStoppedServiceIsFinishedWithoutAReason)
{
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    rig.service->stop();
    const auto reason = rig.service->waitUntilFinished (0.1);
    ASSERT_TRUE (reason.has_value());
    EXPECT_EQ (*reason, "");
}

//==============================================================================
// Task 21: the window's rig, status and bench, on the real service.

namespace
{
std::string benchBody (const std::string& to, const std::string& address, const std::string& value)
{
    auto* made = new juce::DynamicObject();
    made->setProperty ("to", juce::String (to));
    made->setProperty ("address", juce::String (address));
    made->setProperty ("value", juce::String (value));
    return juce::JSON::toString (juce::var (made), true).toStdString();
}

juce::var jsonOf (const httpclient::Reply& reply)
{
    juce::String problem;
    return parseJson (juce::String (reply.body), problem);
}

// The first row of /api/traffic with this direction and address, or a void var.
juce::var trafficRow (const Rig& rig, const std::string& direction, const std::string& address)
{
    const auto traffic = rig.traffic();   // kept: the loop reads into it
    for (const auto& row : *traffic["rows"].getArray())
        if (row["direction"].toString().toStdString() == direction && row["address"].toString().toStdString() == address)
            return row;
    return {};
}

bool waitUntil (const std::function<bool()>& condition, int millis = 3000)
{
    const auto end = Steady::now() + std::chrono::milliseconds (millis);
    while (Steady::now() < end)
    {
        if (condition())
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return condition();
}
}

TEST (CoreApi, SendToSelfIsHandledLikeTraffic)
{
    // B1's bench sent to Core's own port from a fresh socket: the message is traffic like any other --
    // the handler runs on the serve thread and the table has its row -- and not a call into Core.
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const auto reply = httpclient::post (rig.service->webPort().port, "/api/send", benchBody ("self", "/state/recall", "1"));
    EXPECT_EQ (reply.status, 200) << reply.body;
    EXPECT_EQ (jsonOf (reply)["address"].toString(), "/state/recall");
    ASSERT_TRUE (rig.said.waitFor ("/state/recall: replayed "));
    ASSERT_TRUE (waitUntil ([&] { return ! trafficRow (rig, "in", "/state/recall").isVoid(); }));
    EXPECT_EQ (static_cast<int> (trafficRow (rig, "in", "/state/recall")["count"]), 1);
    // The handler said its line on the serve thread, and nothing else was said on any other.
    const auto serveThread = rig.said.find ("/state/recall: replayed ")->thread;
    for (const auto& line : rig.said.all())
        EXPECT_EQ (line.thread, serveThread) << line.text;
}

TEST (CoreApi, TheBenchSendsMixerAndMotionThroughCoresOwnClients)
{
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    rig.mixer.messages (200);
    rig.motion.messages (200);
    const auto port = rig.service->webPort().port;
    EXPECT_EQ (httpclient::post (port, "/api/send", benchBody ("mixer", "/bench/one", "0.5")).status, 200);
    EXPECT_EQ (httpclient::post (port, "/api/send", benchBody ("motion", "/bench/two", "\"1\"")).status, 200);

    const auto one = rig.mixer.awaitAddress ("/bench/one");
    ASSERT_TRUE (one.has_value());
    EXPECT_EQ (one->args.size(), 1u);
    EXPECT_FLOAT_EQ (std::get<float> (one->args[0].value), 0.5f);
    const auto two = rig.motion.awaitAddress ("/bench/two");
    ASSERT_TRUE (two.has_value());
    EXPECT_EQ (std::get<std::string> (two->args.at (0).value), "1");

    // Sent as Core sends, so its table has the rows (B1: the clients were WatchedClients).
    ASSERT_TRUE (waitUntil ([&] { return ! trafficRow (rig, "out", "/bench/two").isVoid(); }));
    EXPECT_EQ (trafficRow (rig, "out", "/bench/one")["peer"].toString(), "mixer");
    EXPECT_EQ (trafficRow (rig, "out", "/bench/two")["last_type"].toString(), "str");
    for (const auto& line : rig.said.texts())
        EXPECT_EQ (line.find ("bench"), std::string::npos) << line;
}

TEST (CoreApi, TheWindowNeverCallsIntoCoreFromAnHttpThread)
{
    // The rig, the status and the bench from several HTTP threads at once while the serve thread
    // works: if a handler reached into Core, the owning-thread check would answer 400/500 here.
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const auto port = rig.service->webPort().port;
    std::atomic<int> notOk { 0 };
    std::atomic<bool> go { true };
    std::vector<std::thread> readers;
    for (const auto* path : { "/api/rig", "/api/status", "/api/rig" })
        readers.emplace_back ([&, path] {
            while (go)
                if (httpclient::get (port, path).status != 200)
                    ++notOk;
        });
    for (int i = 0; i < 20; ++i)
    {
        EXPECT_EQ (httpclient::post (port, "/api/send", benchBody (i % 2 ? "mixer" : "motion", "/bench/x", std::to_string (i))).status, 200);
        rig.mixer.osc (rig.port(), "/channel/1/gain", { pyFloat (0.1 * i) });
    }
    go = false;
    for (auto& reader : readers)
        reader.join();
    EXPECT_EQ (notOk.load(), 0);
    for (const auto& line : rig.said.texts())
        EXPECT_EQ (line.find ("does not own"), std::string::npos) << line;
}

TEST (CoreApi, TheServiceServesTheStatusAndWhoIsThere)
{
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const auto port = rig.service->webPort().port;
    const auto status = httpclient::get (port, "/api/status");
    EXPECT_EQ (status.status, 200);
    EXPECT_EQ (jsonOf (status)["audio_engine"].toString(), "engine");
    EXPECT_EQ (jsonOf (status)["reason"].toString(), "");

    EXPECT_FALSE (static_cast<bool> (jsonOf (httpclient::get (port, "/api/rig"))["engine"]["heard"]));
    const Socket engineSays;
    engineSays.osc (rig.port(), "/engine/hello", { pyStr ("boot-9") });
    EXPECT_TRUE (waitUntil ([&] {
        const auto engine = jsonOf (httpclient::get (port, "/api/rig"))["engine"];
        return static_cast<bool> (engine["heard"]) && static_cast<bool> (engine["open"]);
    }));
    EXPECT_EQ (jsonOf (httpclient::get (port, "/api/rig"))["engine"]["boot_id"].toString(), "boot-9");

    rig.hello (rig.mixer, "desk");
    EXPECT_TRUE (waitUntil ([&] { return jsonOf (httpclient::get (port, "/api/rig"))["devices"].size() > 0; }));
}

TEST (CoreApi, TheWindowAddressIsTheBoundOneAsB1Says)
{
    // B1 printed window_address(), what the socket was bound to, not the host the person wrote: a name
    // is the address it resolved to.
    Rig rig;
    auto options = rig.options();
    options.webBind = "localhost:0";
    ASSERT_TRUE (rig.start (options)) << rig.error;
    const auto port = rig.service->webPort();
    EXPECT_EQ (port.host, loopbackAddress());
    EXPECT_TRUE (rig.said.find ("window on http://" + loopbackAddress() + ":" + std::to_string (port.port)).has_value());
}

//==============================================================================
// Departments: stored in Core's config, applied live, never a reason not to start.

namespace
{
std::string departmentsBody (const std::vector<Department>& list)
{
    return canonicalBytes (objectOf ({ { "departments", departmentsJson (list) } }));
}

ServiceOptions withDepartmentsFile (const Rig& rig)
{
    auto options = rig.options();
    options.departmentsFile = departmentsFile (rig.home.getChildFile (".config/a3"));
    return options;
}
}

TEST (CoreService, ADepartmentSavedInTheWindowIsStoredAndToldAtOnce)
{
    Rig rig;
    const Socket light;
    const auto options = withDepartmentsFile (rig);
    ASSERT_TRUE (rig.start (options)) << rig.error;
    const Department entry { "light", loopbackAddress(), light.port(), true };
    const auto reply = httpclient::post (rig.service->webPort().port, "/api/departments", departmentsBody ({ entry }));
    EXPECT_EQ (reply.status, 200) << reply.body;
    EXPECT_EQ (loadDepartments (options.departmentsFile).list, (std::vector<Department> { entry }));
    EXPECT_FALSE (light.messages (500).empty()) << "a department that joins is told the whole state";
    EXPECT_TRUE (waitUntil ([&] {
        const auto rows = jsonOf (httpclient::get (rig.service->webPort().port, "/api/departments"))["departments"];
        return rows.size() == 1 && static_cast<int> (rows[0]["sent"]) > 0;
    }));
}

TEST (CoreService, AStoredDepartmentIsToldFromTheStart)
{
    Rig rig;
    const Socket light;
    const auto options = withDepartmentsFile (rig);
    ASSERT_TRUE (saveDepartments (options.departmentsFile, { { "light", loopbackAddress(), light.port(), true } }));
    ASSERT_TRUE (rig.start (options)) << rig.error;
    EXPECT_FALSE (light.messages (500).empty());
}

TEST (CoreService, ABrokenDepartmentsFileIsSaidAndCoreStarts)
{
    Rig rig;
    const auto options = withDepartmentsFile (rig);
    options.departmentsFile.getParentDirectory().createDirectory();
    options.departmentsFile.replaceWithText ("{ not json");
    ASSERT_TRUE (rig.start (options)) << rig.error;
    EXPECT_TRUE (rig.said.find ("departments: ").has_value());
}

TEST (CoreService, WithoutADepartmentsFileTheSaveIsRefused)
{
    Rig rig;
    ASSERT_TRUE (rig.start()) << rig.error;
    const auto reply = httpclient::post (rig.service->webPort().port, "/api/departments", departmentsBody ({}));
    EXPECT_EQ (reply.status, 400);
}
