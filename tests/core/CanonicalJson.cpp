// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Byte-equality with Python's json: every device's truth hash depends on it.
#include <cstdlib>

#include <gtest/gtest.h>

#include "Json.h"

using namespace a3core;

namespace
{
juce::var parsed (const juce::String& text)
{
    juce::String error;
    const auto value = parseJson (text, error);
    EXPECT_EQ (error, juce::String()) << text;
    return value;
}

const juce::var& fixture()
{
    static const juce::var loaded = parsed (juce::File (A3_CORE_TEST_DATA "/parity/json.json").loadFileAsString());
    return loaded;
}

juce::var entry (const char* section, const char* key = nullptr)
{
    const auto& block = fixture()[juce::Identifier (section)];
    return key == nullptr ? block : block[juce::Identifier (key)];
}

std::string decodeBase64 (const juce::var& encoded)
{
    juce::MemoryOutputStream decoded;
    EXPECT_TRUE (juce::Base64::convertFromBase64 (decoded, encoded.toString()));
    return std::string (static_cast<const char*> (decoded.getData()), decoded.getDataSize());
}

void expectHashesAsRecorded (const juce::var& recorded)
{
    const auto value = parsed (recorded["text"].toString());
    const auto bytes = canonicalBytes (value);
    EXPECT_EQ (bytes, decodeBase64 (recorded["canonical_b64"]));
    EXPECT_EQ (sha256Hex (bytes), recorded["fingerprint"].toString().toStdString());
}
}

TEST (CanonicalJson, ShippedTruthHashesAsPython)
{
    expectHashesAsRecorded (entry ("truth"));
}

TEST (CanonicalJson, JoinedWithANetworkFile)
{
    // non-ASCII host names and a meters block, as the maintainer's file may carry them
    EXPECT_FALSE (entry ("network_text").toString().isEmpty());
    expectHashesAsRecorded (entry ("joined"));
}

TEST (CanonicalJson, FloatsAsPythonRepr)
{
    const auto* table = fixture()["floats"].getArray();
    ASSERT_NE (table, nullptr);
    ASSERT_GT (table->size(), 10);
    for (const auto& row : *table)
    {
        const auto text = row["text"].toString();
        const auto expected = row["repr"].toString().toStdString();
        EXPECT_EQ (pythonRepr (std::strtod (text.toRawUTF8(), nullptr)), expected) << text;
        EXPECT_EQ (canonicalBytes (parsed ("[" + text + "]")), "[" + expected + "]") << text;
    }
}

TEST (CanonicalJson, EscapesAsPython)
{
    EXPECT_EQ (canonicalBytes (juce::var (juce::String::fromUTF8 ("q\" b\\ n\n r\r t\t f\f b\b c\x1f z/ \xc3\xbc \xf0\x9f\x98\x80"))),
               "\"q\\\" b\\\\ n\\n r\\r t\\t f\\f b\\b c\\u001f z/ \xc3\xbc \xf0\x9f\x98\x80\"");
    EXPECT_EQ (dumpIndented (juce::var (juce::String::fromUTF8 ("\xc3\xbc\x7f\xf0\x9f\x98\x80")), 1),
               "\"\\u00fc\\u007f\\ud83d\\ude00\"");
}

TEST (CanonicalJson, KeysSortedByCodePoint)
{
    // U+FFFF sorts before U+1F600 by code point (UTF-16 would order them the other way round)
    const auto value = objectOf ({ { "b", 0 }, { "\xf0\x9f\x98\x80", 0 }, { "a", 0 },
                                   { "\xef\xbf\xbf", 0 }, { "B", 0 }, { "\xc3\xa9", 0 } });
    EXPECT_EQ (canonicalBytes (value),
               "{\"B\":0,\"a\":0,\"b\":0,\"\xc3\xa9\":0,\"\xef\xbf\xbf\":0,\"\xf0\x9f\x98\x80\":0}");
}

TEST (CanonicalJson, IntegersStayIntegers)
{
    EXPECT_EQ (canonicalBytes (parsed ("[1,-5,0,3000000000,9223372036854775807,1.0,-0.0,true,null]")),
               "[1,-5,0,3000000000,9223372036854775807,1.0,-0.0,true,null]");
    EXPECT_EQ (canonicalBytes (juce::var (2.0)), "2.0");
    EXPECT_EQ (canonicalBytes (juce::var (2)), "2");
}

TEST (CanonicalJson, IndentedDumpAsPython)
{
    const auto sample = entry ("sample");
    EXPECT_EQ (dumpIndented (parsed (sample["text"].toString()), 2), sample["indent2"].toString().toStdString());
    const auto network = entry ("network_indent1");
    EXPECT_EQ (dumpIndented (parsed (network["text"].toString()), 1), network["indent1"].toString().toStdString());
}

TEST (CanonicalJson, KeyOrderDoesNotMatter)
{
    const auto a = objectOf ({ { "b", 1 }, { "a", objectOf ({ { "y", 2 }, { "x", 3 } }) } });
    const auto b = objectOf ({ { "a", objectOf ({ { "x", 3 }, { "y", 2 } }) }, { "b", 1 } });
    EXPECT_EQ (sha256Hex (canonicalBytes (a)), sha256Hex (canonicalBytes (b)));
    EXPECT_EQ (canonicalBytes (a), "{\"a\":{\"x\":3,\"y\":2},\"b\":1}");
}

TEST (CanonicalJson, AChangedValueChangesIt)
{
    const auto contract = parsed ("{\"version\":1,\"hosts\":{\"core\":\"192.168.8.10\"}}");
    const auto mine = parsed ("{\"version\":1,\"hosts\":{\"core\":\"10.0.0.2\"}}");
    EXPECT_NE (sha256Hex (canonicalBytes (contract)), sha256Hex (canonicalBytes (mine)));
}

TEST (CanonicalJson, ItIsSha256Hex)
{
    const auto hash = sha256Hex (canonicalBytes (parsed ("{\"version\":1}")));
    EXPECT_EQ (hash.size(), 64u);
    EXPECT_EQ (hash.find_first_not_of ("0123456789abcdef"), std::string::npos);
    EXPECT_EQ (sha256Hex (""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST (CanonicalJson, ParsesAsPython)
{
    EXPECT_EQ (canonicalBytes (parsed ("{\"k\\u00fc\":\"\\ud83d\\ude00\\/\", \"a\": [ ] , \"b\":{}}")),
               "{\"a\":[],\"b\":{},\"k\xc3\xbc\":\"\xf0\x9f\x98\x80/\"}");
    for (const char* broken : { "", "{", "[1,]", "01", "1.", "[1] x", "{'a':1}", "[\"a\nb\"]", "\"\\ud800\"", "99999999999999999999" })
    {
        juce::String error;
        parseJson (broken, error);
        EXPECT_FALSE (error.isEmpty()) << broken;
    }
}

TEST (CanonicalJson, OutOfRangeFloatsReadAsPython)
{
    // Python: [inf, -inf, 0.0, -0.0]
    EXPECT_EQ (canonicalBytes (parsed ("[1e400,-1e400,1e-400,-1e-400]")), "[Infinity,-Infinity,0.0,-0.0]");
}

TEST (CanonicalJson, NulInAStringIsRefused)
{
    for (const char* broken : { "\"a\\u0000b\"", "{\"a\\u0000\":1}" })
    {
        juce::String error;
        parseJson (broken, error);
        EXPECT_FALSE (error.isEmpty()) << broken;
    }
}

TEST (CanonicalJson, NonFiniteWordsRoundTrip)
{
    EXPECT_EQ (canonicalBytes (parsed ("[NaN,Infinity,-Infinity]")), "[NaN,Infinity,-Infinity]");
}

TEST (CanonicalJson, OutOfRangeIsDecidedByMagnitude)
{
    // Python: [inf, 0.0]; the exponent's sign alone points the wrong way for both
    const std::string huge = "1" + std::string (400, '0') + "e-10";
    const std::string tiny = "0." + std::string (400, '0') + "1e+10";
    EXPECT_EQ (canonicalBytes (parsed ("[" + huge + "," + tiny + "]")), "[Infinity,0.0]");
}
