// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The OSC wire and Python's numbers against what python-osc 1.10.2 and Python recorded
// (tools/parity/record_wire.py). The desk sends numbers as text, Motion floats; types and number
// parsing must be Python's (Review Focus 2).
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

#include "OscWire.h"
#include "WireFixture.h"

using namespace a3core;
using namespace wirefixture;

namespace
{
// What SimpleUDPClient.send_message(address, value) builds for this recorded value.
std::vector<OscArg> sentArgs (const juce::var& value)
{
    const auto type = py (value);
    if (type == "None") return {};
    if (type == "list")
    {
        std::vector<OscArg> args;
        for (const auto& item : *value["items"].getArray())
        {
            auto sent = sentArgs (item);
            args.insert (args.end(), sent.begin(), sent.end());
        }
        return args;
    }
    if (type == "float") return { pyFloat (floatOf (value)) };
    return { received (value) };
}

bool sameBits (double a, double b)
{
    if (std::isnan (a) || std::isnan (b))
        return std::isnan (a) && std::isnan (b);
    return std::memcmp (&a, &b, sizeof a) == 0;
}

// One decoded argument against the Python value python-osc gave the handler.
void expectArgIs (const OscArg& arg, const juce::var& value, const std::string& where)
{
    const auto type = py (value);
    SCOPED_TRACE (where + " tag " + std::string (1, arg.tag));
    if (type == "int")
    {
        ASSERT_TRUE (arg.tag == 'i' || arg.tag == 'h' || arg.tag == 'r');
        const long long got = arg.tag == 'i' ? std::get<int32_t> (arg.value) : std::get<int64_t> (arg.value);
        EXPECT_EQ (got, *intOf (value));
    }
    else if (type == "float")
    {
        ASSERT_TRUE (arg.tag == 'f' || arg.tag == 'd');
        const double got = arg.tag == 'f' ? (double) std::get<float> (arg.value) : std::get<double> (arg.value);
        EXPECT_TRUE (sameBits (got, floatOf (value))) << got;
    }
    else if (type == "str")
    {
        ASSERT_EQ (arg.tag, 's');
        EXPECT_EQ (std::get<std::string> (arg.value), strOf (value));
    }
    else if (type == "bytes")
    {
        ASSERT_EQ (arg.tag, 'b');
        EXPECT_EQ (std::get<std::vector<uint8_t>> (arg.value), base64 (value["b64"]));
    }
    else if (type == "bool")
        EXPECT_EQ (arg.tag, (bool) value["value"] ? 'T' : 'F');
    else if (type == "None")
        EXPECT_EQ (arg.tag, 'N');
    else if (type == "tuple")   // MIDI: (port, status, data1, data2)
    {
        ASSERT_EQ (arg.tag, 'm');
        std::vector<uint8_t> expected;
        for (const auto& item : *value["items"].getArray())
            expected.push_back ((uint8_t) *intOf (item));
        EXPECT_EQ (std::get<std::vector<uint8_t>> (arg.value), expected);
    }
    else
        ADD_FAILURE() << "unexpected Python type " << type;
}

// Arrays are the one recorded case decoded differently on purpose (see decodePacket).
bool isArrayCase (const juce::var& entry) { return entry["name"].toString() == "array"; }

void expectDecodedAsPython (const juce::var& entry)
{
    const auto name = entry["name"].toString().toStdString();
    const auto bytes = hex (entry["hex"]);
    const auto got = decodePacket (bytes.data(), bytes.size());
    if (entry.hasProperty ("raises") || isArrayCase (entry))
    {
        EXPECT_FALSE (got.has_value()) << name;
        return;
    }
    ASSERT_TRUE (got.has_value()) << name;
    const auto& messages = *entry["messages"].getArray();
    ASSERT_EQ (got->size(), (size_t) messages.size()) << name;
    for (int m = 0; m < messages.size(); ++m)
    {
        const auto& message = (*got)[(size_t) m];
        EXPECT_EQ (message.address, messages[m]["address"].toString().toStdString()) << name;
        const auto& args = *messages[m]["args"].getArray();
        ASSERT_EQ (message.args.size(), (size_t) args.size()) << name;
        for (int a = 0; a < args.size(); ++a)
            expectArgIs (message.args[(size_t) a], args[a], name);
    }
}
}

TEST (OscWire, EncodesAsPythonOsc)
{
    EXPECT_EQ (wire()["python_osc"].toString(), juce::String ("1.10.2"));
    const auto& cases = *wire()["encode"].getArray();
    ASSERT_GE (cases.size(), 20);
    for (const auto& entry : cases)
    {
        const OscMessage message { entry["address"].toString().toStdString(), sentArgs (entry["value"]) };
        const auto expected = entry.hasProperty ("raises") ? std::vector<uint8_t>() : hex (entry["hex"]);
        EXPECT_EQ (encodeMessage (message), expected) << message.address;
    }
}

TEST (OscWire, TheTypeIsPythonOscs)
{
    EXPECT_EQ (pyInt (2147483647).tag, 'i');
    EXPECT_EQ (pyInt (-2147483647).tag, 'i');
    EXPECT_EQ (pyInt (2147483648LL).tag, 'h');
    EXPECT_EQ (pyInt (-2147483648LL).tag, 'h');   // bit_length() is 32
    EXPECT_EQ (pyFloat (0.5).tag, 'f');
    EXPECT_EQ (pyStr ("1").tag, 's');
    EXPECT_EQ (pyBool (true).tag, 'T');
    EXPECT_EQ (pyBool (false).tag, 'F');
}

TEST (OscWire, DecodesAsPythonOsc)
{
    const auto& cases = *wire()["decode"].getArray();
    ASSERT_GE (cases.size(), 30);
    for (const auto& entry : cases)
        expectDecodedAsPython (entry);
}

TEST (OscWire, TruncatedPacketIsRefused)
{
    int truncated = 0, refused = 0;
    for (const auto& entry : *wire()["decode"].getArray())
    {
        if (! entry["name"].toString().startsWith ("truncated-"))
            continue;
        ++truncated;
        refused += entry.hasProperty ("raises") ? 1 : 0;
        expectDecodedAsPython (entry);   // python-osc pads a short float and cuts a short bundle content
    }
    EXPECT_GE (truncated, 10);
    EXPECT_GE (refused, 8);
}

TEST (PythonNumbers, FloatOfTextAsPython)
{
    const auto& cases = *wire()["numbers"].getArray();
    ASSERT_GE (cases.size(), 30);
    for (const auto& entry : cases)
    {
        const auto arg = received (entry["value"]);
        const auto got = pythonFloat (arg);
        const auto& expected = entry["float"];
        const auto what = juce::JSON::toString (entry["value"], true).toStdString();
        if (expected.hasProperty ("raises"))
        {
            EXPECT_FALSE (got.has_value()) << what << " gave " << got.value_or (0.0);
            continue;
        }
        ASSERT_TRUE (got.has_value()) << what;
        EXPECT_TRUE (sameBits (*got, floatOf (expected))) << what << " gave " << *got;
    }
}

TEST (PythonNumbers, FloatOfAWireFloatIsItsValue)
{
    EXPECT_EQ (pythonFloat (OscArg { 'f', 0.8f }), (double) 0.8f);
    EXPECT_EQ (pythonFloat (pyFloat (1.7)), 1.7);   // not packed yet: still the Python float
    const auto sent = encodeMessage ({ "/x", { pyFloat (1.7) } });
    const auto received = decodePacket (sent.data(), sent.size());
    ASSERT_TRUE (received.has_value());
    EXPECT_EQ (pythonFloat (received->at (0).args.at (0)), (double) 1.7f);   // what the receiver gets
    EXPECT_EQ (pythonInt (OscArg { 'f', -1.7f }), -1);
}

TEST (PythonNumbers, IntOfTextAsPython)
{
    for (const auto& entry : *wire()["numbers"].getArray())
    {
        const auto arg = received (entry["value"]);
        const auto got = pythonInt (arg);
        const auto& expected = entry["int"];
        const auto what = juce::JSON::toString (entry["value"], true).toStdString();
        // A Python int beyond a long long is no int here (Core's int() reads small numbers).
        if (expected.hasProperty ("raises") || ! intOf (expected).has_value())
        {
            EXPECT_FALSE (got.has_value()) << what << " gave " << got.value_or (0);
            continue;
        }
        ASSERT_TRUE (got.has_value()) << what;
        EXPECT_EQ (*got, *intOf (expected)) << what;
    }
}
