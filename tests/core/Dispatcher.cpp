// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The dispatcher against python-osc 1.10.2's Dispatcher, given Core's map patterns (B0's and 4b's
// main block, tools/parity/record_wire.py), and the port of test_core_handlers_are_mapped's
// catch-all case.
#include <gtest/gtest.h>

#include <stdexcept>

#include "Dispatcher.h"
#include "WireFixture.h"

using namespace a3core;
using namespace wirefixture;

namespace
{
constexpr int defaultCall = -1;

OscMessage at (std::string address) { return { std::move (address), {} }; }
}

TEST (Dispatcher, CallsWhatPythonOscCalls)
{
    std::vector<int> calls;
    Dispatcher dispatcher;
    const auto& maps = *wire()["maps"].getArray();
    ASSERT_EQ (maps.size(), 14);
    for (int i = 0; i < maps.size(); ++i)
        dispatcher.map (maps[i]["pattern"].toString().toStdString(), [&calls, i] (const Endpoint&, const OscMessage&) { calls.push_back (i); });
    dispatcher.setDefault ([&calls] (const Endpoint&, const OscMessage&) { calls.push_back (defaultCall); });

    const auto& cases = *wire()["dispatch"].getArray();
    ASSERT_GE (cases.size(), 40);
    for (const auto& entry : cases)
    {
        std::vector<int> expected;
        for (const auto& call : *entry["calls"].getArray())
            expected.push_back (call.isString() ? defaultCall : (int) call);
        calls.clear();
        const auto address = entry["address"].toString().toStdString();
        dispatcher.dispatch ({ "127.0.0.1", 1 }, at (address));
        EXPECT_EQ (calls, expected) << "address \"" << address << "\"";
    }
}

TEST (Dispatcher, DefaultOnlyWhenNothingMatches)
{
    std::vector<std::string> calls;
    Dispatcher dispatcher;
    dispatcher.map ("/a/*", [&] (const Endpoint&, const OscMessage& m) { calls.push_back ("family " + m.address); });
    dispatcher.map ("/a/b", [&] (const Endpoint&, const OscMessage& m) { calls.push_back ("b " + m.address); });
    dispatcher.map ("/a/*", [&] (const Endpoint&, const OscMessage& m) { calls.push_back ("family again " + m.address); });

    dispatcher.dispatch ({ "h", 1 }, at ("/c"));
    EXPECT_TRUE (calls.empty());   // no default yet: nothing

    dispatcher.setDefault ([&] (const Endpoint&, const OscMessage& m) { calls.push_back ("default " + m.address); });
    dispatcher.dispatch ({ "h", 1 }, at ("/a/b"));
    // A pattern mapped twice keeps its first place, its handlers in map order.
    EXPECT_EQ (calls, (std::vector<std::string> { "family /a/b", "family again /a/b", "b /a/b" }));

    calls.clear();
    dispatcher.dispatch ({ "h", 1 }, at ("/c"));
    EXPECT_EQ (calls, (std::vector<std::string> { "default /c" }));
}

TEST (Dispatcher, TheMainDispatcherHasACatchAll)
{
    // Python's case reads a3-core.py for set_default_handler on the main dispatcher. Here: what
    // matches no map is still handed on, with its sender -- python-osc swallows it silently
    // otherwise. (Core's own dispatcher is built in Task 19, CoreMaps.)
    std::vector<std::string> unrouted;
    Dispatcher dispatcher;
    dispatcher.map ("/channel/*", [] (const Endpoint&, const OscMessage&) {});
    dispatcher.setDefault ([&] (const Endpoint& from, const OscMessage& m) { unrouted.push_back (from.host + " " + m.address); });
    dispatcher.dispatch ({ "192.168.8.30", 5000 }, at ("/desk/unknown"));
    dispatcher.dispatch ({ "192.168.8.30", 5000 }, at ("/channel/1/enc"));
    EXPECT_EQ (unrouted, (std::vector<std::string> { "192.168.8.30 /desk/unknown" }));
}

TEST (Dispatcher, APatternPythonCannotCompileIsRefused)
{
    Dispatcher dispatcher;
    const Handler none = [] (const Endpoint&, const OscMessage&) {};
    EXPECT_THROW (dispatcher.map ("/a/*[", none), std::invalid_argument);
    EXPECT_THROW (dispatcher.map ("/a/*(", none), std::invalid_argument);
    EXPECT_NO_THROW (dispatcher.map ("/a[/b", none));   // no '*': never read as a regex
}

TEST (Dispatcher, AVeryLongAddressStillDispatches)
{
    // One datagram can carry an address this long; python-osc calls the handler, and the matcher's
    // depth must not grow with the address.
    std::vector<std::string> calls;
    Dispatcher dispatcher;
    dispatcher.map ("/channel/*", [&] (const Endpoint&, const OscMessage& m) { calls.push_back ("channel " + std::to_string (m.address.size())); });
    dispatcher.map ("/beat", [&] (const Endpoint&, const OscMessage&) { calls.push_back ("beat"); });
    dispatcher.setDefault ([&] (const Endpoint&, const OscMessage&) { calls.push_back ("default"); });

    dispatcher.dispatch ({ "h", 1 }, at ("/channel/" + std::string (60000, 'a')));
    dispatcher.dispatch ({ "h", 1 }, at ("/channel/" + std::string (60000, '*')));
    dispatcher.dispatch ({ "h", 1 }, at ("/" + std::string (60000, '?')));
    EXPECT_EQ (calls, (std::vector<std::string> { "channel 60009", "channel 60009", "default" }));
}
