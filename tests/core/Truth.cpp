// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The truth reader against B0's a3_osc / a3_osc_join: ports of test_osc_join, test_osc_truth
// (reader cases) and test_meter_ballistics, plus a fixture of what Python's match() answers.
#include <gtest/gtest.h>

#include "Json.h"
#include "Truth.h"

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

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> members)
{
    auto* made = new juce::DynamicObject();
    for (const auto& member : members)
        made->setProperty (juce::Identifier (member.first), member.second);
    return juce::var (made);
}

juce::var object() { return juce::var (new juce::DynamicObject()); }

juce::var array (std::initializer_list<juce::var> items) { return juce::var (juce::Array<juce::var> (items)); }

std::string canonicalOf (const juce::var& value) { return canonicalBytes (value); }

const juce::var& parityFixture()
{
    static const juce::var loaded = parsed (juce::File (A3_CORE_TEST_DATA "/parity/json.json").loadFileAsString());
    return loaded;
}

const juce::var& matchFixture()
{
    static const juce::var loaded = parsed (juce::File (A3_CORE_TEST_DATA "/parity/truth-match.json").loadFileAsString());
    return loaded;
}

// A scratch directory per test (a plain object, so the tests stay `TEST (Truth, ...)`); the shipped truth is written into it from the recorded text.
struct Scratch
{
    Scratch()
    {
        dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a3-truth-test", "", false);
        EXPECT_TRUE (dir.createDirectory());
        truthFile = dir.getChildFile ("a3-osc.json");
        EXPECT_TRUE (truthFile.replaceWithText (parityFixture()["truth"]["text"].toString()));
    }
    ~Scratch() { dir.deleteRecursively(); }

    juce::File write (const juce::String& name, const juce::String& text) const
    {
        auto file = dir.getChildFile (name);
        EXPECT_TRUE (file.replaceWithText (text));
        return file;
    }

    Truth load (std::optional<juce::File> network = std::nullopt) const
    {
        auto result = Truth::load (truthFile, network);
        const auto* truth = std::get_if<Truth> (&result);
        EXPECT_NE (truth, nullptr);
        if (truth == nullptr)
            std::abort();
        return *truth;
    }

    Truth withMeters (const juce::var& meters) const
    {
        auto data = load().data();
        data.getDynamicObject()->setProperty ("meters", meters);
        return Truth (data);
    }

    Truth withoutMeters() const
    {
        auto data = load().data();
        data.getDynamicObject()->removeProperty ("meters");
        return Truth (data);
    }

    juce::File dir, truthFile;
};

const juce::var& contractSample()
{
    static const juce::var sample = object ({ { "version", 1 },
                                              { "hosts", object ({ { "core", "192.168.8.10" } }) },
                                              { "network", object ({ { "address", "192.168.8.10/24" } }) },
                                              { "listeners", array ({}) } });
    return sample;
}

juce::var mine()
{
    return object ({ { "hosts", object ({ { "core", "10.0.0.2" } }) },
                     { "network", object ({ { "address", "10.0.0.2/24" } }) } });
}

using Fields = std::map<std::string, int>;
using Matched = std::optional<std::pair<std::string, Fields>>;

Matched matched (const char* key, Fields fields) { return std::make_pair (std::string (key), std::move (fields)); }

void expectMeters (const Meters& got, double attack, double release, double hold)
{
    EXPECT_EQ (got, (Meters { attack, release, hold }));
}
}

// ---- test_osc_join ----------------------------------------------------------------------------

TEST (Truth, TheNetworkComesFromTheMaintainersFile)
{
    const auto joined = joinTruth (contractSample(), mine());
    EXPECT_EQ (canonicalOf (joined["hosts"]), "{\"core\":\"10.0.0.2\"}");
    EXPECT_EQ (canonicalOf (joined["network"]), "{\"address\":\"10.0.0.2/24\"}");
    EXPECT_EQ (canonicalOf (joined["listeners"]), "[]");
}

TEST (Truth, WithoutAFileTheContractsBlocksStay)
{
    EXPECT_EQ (canonicalOf (joinTruth (contractSample(), std::nullopt)), canonicalOf (contractSample()));
}

TEST (Truth, TheContractIsNotChanged)
{
    joinTruth (contractSample(), mine());
    EXPECT_EQ (canonicalOf (contractSample()["hosts"]), "{\"core\":\"192.168.8.10\"}");
}

TEST (Truth, AHostMissingFromTheFileComesFromThePackage)
{
    const auto contract = object ({ { "version", 1 },
                                    { "hosts", object ({ { "core", "192.168.8.10" }, { "local", "127.0.0.1" } }) },
                                    { "network", object ({ { "address", "192.168.8.10/24" } }) } });
    const auto joined = joinTruth (contract, object ({ { "hosts", object ({ { "core", "10.0.0.2" } }) },
                                                       { "network", object() } }));
    EXPECT_EQ (canonicalOf (joined["hosts"]), "{\"core\":\"10.0.0.2\",\"local\":\"127.0.0.1\"}");
}

TEST (Truth, ANetworkKeyMissingFromTheFileComesFromThePackage)
{
    const auto contract = object ({ { "hosts", object ({ { "core", "192.168.8.10" } }) },
                                    { "network", object ({ { "address", "192.168.8.10/24" }, { "gateway", "192.168.8.1" } }) } });
    const auto joined = joinTruth (contract, object ({ { "hosts", object() },
                                                       { "network", object ({ { "address", "10.0.0.2/24" } }) } }));
    EXPECT_EQ (canonicalOf (joined["network"]), "{\"address\":\"10.0.0.2/24\",\"gateway\":\"192.168.8.1\"}");
}

TEST (Truth, AGoodFileIsRead)
{
    Scratch s;
    const auto read = readNetwork (s.write ("network.json", juce::String (canonicalOf (mine()))));
    ASSERT_TRUE (read.network.has_value());
    EXPECT_EQ (canonicalOf (*read.network), canonicalOf (mine()));
    EXPECT_FALSE (read.problem.has_value());
}

TEST (Truth, AMissingFileIsNoProblemJustAbsent)
{
    Scratch s;
    const auto read = readNetwork (s.dir.getChildFile ("nonexistent/a3/network.json"));
    EXPECT_FALSE (read.network.has_value());
    EXPECT_FALSE (read.problem.has_value());
}

TEST (Truth, ABrokenFileIsRefusedWithAReason)
{
    Scratch s;
    for (const char* text : { "{", "[]", "{\"hosts\":{}}", "{\"hosts\":[],\"network\":{}}" })
    {
        const auto read = readNetwork (s.write ("broken.json", text));
        EXPECT_FALSE (read.network.has_value()) << text;
        ASSERT_TRUE (read.problem.has_value()) << text;
        EXPECT_FALSE (read.problem->empty()) << text;
    }
}

// ---- test_osc_truth: taking an address apart ----------------------------------------------------

TEST (Truth, AChannelAddress)
{
    Scratch s;
    EXPECT_EQ (s.load().match ("/channel/1/volume"), matched ("channel.volume", { { "ch", 1 } }));
    EXPECT_EQ (s.load().match ("/channel/4/filter/frequency"), matched ("channel.filter.frequency", { { "ch", 4 } }));
}

TEST (Truth, AnAddressWithoutFields)
{
    Scratch s;
    EXPECT_EQ (s.load().match ("/master/aux-return"), matched ("master.aux-return", {}));
}

TEST (Truth, AVuMeter)
{
    Scratch s;
    EXPECT_EQ (s.load().match ("/vu/40"), matched ("vu", { { "n", 40 } }));
}

TEST (Truth, OutsideTheRangeIsNoMatch)
{
    Scratch s;
    EXPECT_EQ (s.load().match ("/channel/0/volume"), std::nullopt);
    EXPECT_EQ (s.load().match ("/channel/5/volume"), std::nullopt);
    EXPECT_EQ (s.load().match ("/vu/67"), std::nullopt);
}

TEST (Truth, AnOldOrUnknownAddressIsNoMatch)
{
    Scratch s;
    for (const char* address : { "/fx/frequency", "/master/return", "/channel/1/pot_1", "/nothing" })
        EXPECT_EQ (s.load().match (address), std::nullopt) << address;
}

TEST (Truth, MatchAndAddressAgreeEverywhere)
{
    Scratch s;
    const auto truth = s.load();
    const auto addresses = truth.data()["addresses"];
    const auto* object = addresses.getDynamicObject();
    ASSERT_NE (object, nullptr);
    ASSERT_GT (object->getProperties().size(), 40);
    for (const auto& property : object->getProperties())
    {
        const std::string key = property.name.toString().toStdString();
        Fields fields;
        for (const char* name : { "ch", "n", "deck", "stem", "bus" })
            if (property.value.hasProperty (name))
                fields[name] = static_cast<int> (property.value[juce::Identifier (name)][0]);
        EXPECT_EQ (truth.match (truth.address (key, fields)), matched (key.c_str(), fields)) << key;
    }
}

// ---- test_osc_truth: the installed truth is joined ---------------------------------------------

TEST (Truth, A3NetworkNamesTheFileToJoin)
{
    Scratch s;
    const auto network = s.write ("network.json", "{\"hosts\":{\"mixer\":\"10.9.9.9\"},\"network\":{}}");
    EXPECT_EQ (s.load (network).host ("mixer"), "10.9.9.9");
    // as a3_osc.network_path names it
    EXPECT_EQ (networkPath (true, s.dir, network.getFullPathName().toRawUTF8()), network);
    EXPECT_EQ (networkPath (false, s.dir, network.getFullPathName().toRawUTF8()), network);
}

TEST (Truth, AnExplicitTruthReadsNoNetworkFile)
{
    Scratch s;
    EXPECT_EQ (s.load().host ("mixer"), "192.168.8.11");
    EXPECT_EQ (networkPath (true, s.dir, nullptr), std::nullopt);
    EXPECT_EQ (networkPath (true, s.dir, ""), std::nullopt);
    EXPECT_EQ (networkPath (false, s.dir, nullptr), s.dir.getChildFile (".config/a3/network.json"));
}

TEST (Truth, ABrokenNetworkFileLeavesTheDefaultsAndSaysWhy)
{
    Scratch s;
    const auto truth = s.load (s.write ("network.json", "{"));
    EXPECT_EQ (truth.host ("mixer"), "192.168.8.11");
    EXPECT_TRUE (truth.networkProblem.has_value());
}

TEST (Truth, TheFingerprintIsOfTheJoinedTruth)
{
    Scratch s;
    const auto joined = s.load (s.write ("network.json", "{\"hosts\":{\"mixer\":\"10.9.9.9\"},\"network\":{}}"));
    EXPECT_NE (joined.fingerprint(), s.load().fingerprint());
    EXPECT_EQ (parsed (juce::String (joined.canonical()))["hosts"]["mixer"].toString(), "10.9.9.9");
}

// ---- test_meter_ballistics ---------------------------------------------------------------------

TEST (Truth, TheDefaults)
{
    Scratch s;
    expectMeters (s.withMeters (object()).meters(), 0, 20, 1.5);   // METER_DEFAULTS: an empty block
}

TEST (Truth, NoBlockGivesTheDefaults)
{
    Scratch s;
    expectMeters (s.withoutMeters().meters(), 0, 20, 1.5);
}

TEST (Truth, AMissingKeyTakesItsDefault)
{
    Scratch s;
    expectMeters (s.withMeters (object ({ { "release_db_per_second", 30 } })).meters(), 0, 30, 1.5);
}

TEST (Truth, ACommentIsNotAKey)
{
    Scratch s;
    expectMeters (s.withMeters (object ({ { "_comment", "why" }, { "peak_hold_seconds", 2 } })).meters(), 0, 20, 2);
}

TEST (Truth, NumbersOnly)
{
    Scratch s;
    EXPECT_THROW (s.withMeters (object ({ { "attack_ms", "0" } })).meters(), TruthError);
    EXPECT_THROW (s.withMeters (object ({ { "release_db_per_second", juce::var() } })).meters(), TruthError);
    EXPECT_THROW (s.withMeters (object ({ { "peak_hold_seconds", array ({ 1.5 }) } })).meters(), TruthError);
}

TEST (Truth, ASwitchIsNotANumber)
{
    Scratch s;
    EXPECT_THROW (s.withMeters (object ({ { "attack_ms", false } })).meters(), TruthError);
    EXPECT_THROW (s.withMeters (object ({ { "peak_hold_seconds", true } })).meters(), TruthError);
}

TEST (Truth, TheReleaseHasToFall)
{
    Scratch s;
    EXPECT_THROW (s.withMeters (object ({ { "release_db_per_second", 0 } })).meters(), TruthError);
    EXPECT_THROW (s.withMeters (object ({ { "release_db_per_second", -20 } })).meters(), TruthError);
}

TEST (Truth, NoNegativeHold)
{
    Scratch s;
    EXPECT_THROW (s.withMeters (object ({ { "peak_hold_seconds", -0.1 } })).meters(), TruthError);
}

TEST (Truth, NoNegativeAttack)
{
    Scratch s;
    EXPECT_THROW (s.withMeters (object ({ { "attack_ms", -1 } })).meters(), TruthError);
}

TEST (Truth, ZeroHoldAndZeroAttackStand)
{
    Scratch s;
    const auto meters = s.withMeters (object ({ { "attack_ms", 0 }, { "peak_hold_seconds", 0 } })).meters();
    EXPECT_EQ (meters.attackMs, 0);
    EXPECT_EQ (meters.peakHoldSeconds, 0);
}

TEST (Truth, AMisspeltKeyIsRefused)
{
    Scratch s;
    EXPECT_THROW (s.withMeters (object ({ { "release_db_per_sec", 30 } })).meters(), TruthError);
}

TEST (Truth, TheBlockIsAnObject)
{
    Scratch s;
    EXPECT_THROW (s.withMeters (array ({ 0, 20, 1.5 })).meters(), TruthError);
}

namespace
{
// The local override: the maintainer's network.json beside hosts and network.
struct LocalOverride : Scratch
{
    Truth loadWith (const juce::var& meters)
    {
        auto blocks = packageBlocks();
        blocks.getDynamicObject()->setProperty ("meters", meters);
        return load (write ("network.json", juce::String (dumpIndented (blocks, 1))));
    }

    juce::var packageBlocks() const
    {
        const auto package = load().data();
        auto hosts = package["hosts"];
        hosts.getDynamicObject()->setProperty ("mixer", "10.9.9.9");
        return object ({ { "hosts", hosts }, { "network", package["network"] } });
    }
};
}

TEST (Truth, OneKeyChangesOnlyThat)
{
    LocalOverride s;
    const auto truth = s.loadWith (object ({ { "peak_hold_seconds", 3 } }));
    expectMeters (truth.meters(), 0, 20, 3);
    EXPECT_FALSE (truth.metersProblem.has_value());
}

TEST (Truth, TheServedTruthCarriesIt)
{
    LocalOverride s;
    const auto truth = s.loadWith (object ({ { "peak_hold_seconds", 3 } }));
    const auto served = parsed (juce::String (truth.canonical()))["meters"];
    EXPECT_EQ (static_cast<double> (served["peak_hold_seconds"]), 3);
    EXPECT_EQ (static_cast<double> (served["release_db_per_second"]), 20);
    EXPECT_NE (truth.fingerprint(), s.load().fingerprint());
}

TEST (Truth, ABadLocalValueKeepsThePackagesAndSaysWhy)
{
    LocalOverride s;
    const auto truth = s.loadWith (object ({ { "release_db_per_second", 0 }, { "peak_hold_seconds", 3 } }));
    EXPECT_EQ (truth.meters(), s.load().meters());
    ASSERT_TRUE (truth.metersProblem.has_value());
    EXPECT_NE (truth.metersProblem->find ("release_db_per_second"), std::string::npos);
    EXPECT_EQ (canonicalOf (truth.data()["meters"]), canonicalOf (s.load().data()["meters"]));
}

TEST (Truth, ABadLocalBlockLeavesTheNetworkAlone)
{
    LocalOverride s;
    const auto truth = s.loadWith ("fast");
    EXPECT_EQ (truth.host ("mixer"), "10.9.9.9");
    EXPECT_FALSE (truth.networkProblem.has_value());
    EXPECT_TRUE (truth.metersProblem.has_value());
}

TEST (Truth, WithoutALocalBlockThePackagesStand)
{
    LocalOverride s;
    const auto truth = s.load (s.write ("network.json", juce::String (dumpIndented (s.packageBlocks(), 1))));
    EXPECT_EQ (truth.meters(), s.load().meters());
    EXPECT_FALSE (truth.metersProblem.has_value());
}

TEST (Truth, TheFileReaderKeepsTheBlock)
{
    Scratch s;
    const auto read = readNetwork (s.write ("network.json",
        "{\"hosts\":{},\"network\":{},\"meters\":{\"attack_ms\":5}}"));
    ASSERT_TRUE (read.network.has_value());
    EXPECT_EQ (canonicalOf ((*read.network)["meters"]), "{\"attack_ms\":5}");
    EXPECT_FALSE (read.problem.has_value());
}

TEST (Truth, TheJoinGoesKeyByKey)
{
    const auto contract = object ({ { "hosts", object() }, { "network", object() },
                                    { "meters", object ({ { "attack_ms", 0 }, { "release_db_per_second", 20 } }) } });
    const auto joined = joinTruth (contract, object ({ { "hosts", object() }, { "network", object() },
                                                       { "meters", object ({ { "release_db_per_second", 30 } }) } }));
    EXPECT_EQ (canonicalOf (joined["meters"]), "{\"attack_ms\":0,\"release_db_per_second\":30}");
}

// ---- beyond the Python cases: the reader against what Python answers ---------------------------

TEST (Truth, MatchAgreesWithPythonForEveryAddress)
{
    Scratch s;
    const auto truth = s.load();
    const auto* rows = matchFixture()["matches"].getArray();
    ASSERT_NE (rows, nullptr);
    ASSERT_GT (rows->size(), 150);
    for (const auto& row : *rows)
    {
        const auto address = row["address"].toString();
        const auto got = truth.match (address.toStdString());
        if (row["match"].isVoid())
        {
            EXPECT_EQ (got, std::nullopt) << address;
            continue;
        }
        ASSERT_TRUE (got.has_value()) << address;
        EXPECT_EQ (got->first, row["match"][0].toString().toStdString()) << address;
        const auto* expected = row["match"][1].getDynamicObject();
        ASSERT_NE (expected, nullptr);
        ASSERT_EQ (static_cast<int> (got->second.size()), expected->getProperties().size()) << address;
        for (const auto& field : expected->getProperties())
            EXPECT_EQ (got->second.at (field.name.toString().toStdString()), static_cast<int> (field.value)) << address;
    }
}

TEST (Truth, FirstKeyInFileOrderWins)
{
    Scratch s;
    const auto truth = Truth (object ({ { "addresses", object ({
        { "a.first", object ({ { "pattern", "/x/{n}" }, { "n", array ({ 1, 5 }) } }) },
        { "b.second", object ({ { "pattern", "/x/{n}" }, { "n", array ({ 1, 9 }) } }) } }) } }));
    EXPECT_EQ (truth.match ("/x/3"), matched ("a.first", { { "n", 3 } }));
    EXPECT_EQ (truth.match ("/x/7"), matched ("b.second", { { "n", 7 } }));   // first one's range fails, the next fits
    EXPECT_EQ (truth.match ("/x/10"), std::nullopt);
}

TEST (Truth, AddressesAndRangesAsPythonBuildsThem)
{
    Scratch s;
    const auto truth = s.load();
    for (const auto& row : *matchFixture()["built"].getArray())
    {
        const auto key = row["key"].toString().toStdString();
        Fields fields;
        const auto* ranges = row["ranges"].getDynamicObject();
        for (const auto& range : ranges->getProperties())
        {
            const auto name = range.name.toString().toStdString();
            fields[name] = static_cast<int> (range.value[0]);
            EXPECT_EQ (truth.indexRange (key, name), std::make_pair (static_cast<int> (range.value[0]), static_cast<int> (range.value[1]))) << key;
        }
        EXPECT_EQ (truth.address (key, fields), row["address"].toString().toStdString()) << key;
        for (const auto& range : ranges->getProperties())
        {
            auto beyond = fields;
            beyond[range.name.toString().toStdString()] = static_cast<int> (range.value[1]) + 1;
            EXPECT_THROW (truth.address (key, beyond), TruthError) << key;
        }
    }
    EXPECT_THROW (truth.address ("channel.volume"), TruthError);   // a placeholder without its field
    EXPECT_THROW (truth.address ("no.such.key"), TruthError);
    EXPECT_THROW (truth.pattern ("no.such.key"), TruthError);
}

TEST (Truth, EndpointsPortsAndMetersAsPythonReadsThem)
{
    Scratch s;
    const auto truth = s.load();
    const auto* rows = matchFixture()["endpoints"].getArray();
    ASSERT_NE (rows, nullptr);
    ASSERT_GT (rows->size(), 10);
    for (const auto& row : *rows)
    {
        const auto program = row["program"].toString().toStdString();
        const auto role = row["role"].toString().toStdString();
        const auto endpoint = truth.endpoint (program, role);
        EXPECT_EQ (endpoint.host, row["host"].toString().toStdString()) << program << "." << role;
        EXPECT_EQ (endpoint.port, static_cast<int> (row["port"])) << program << "." << role;
        EXPECT_EQ (truth.port (program, role), static_cast<int> (row["port"]));
    }
    const auto& names = matchFixture()["vu_meters"];
    const auto listed = truth.vuMeters();
    ASSERT_EQ (static_cast<int> (listed.size()), names.size());
    for (int i = 0; i < names.size(); ++i)
        EXPECT_EQ (listed[static_cast<size_t> (i)], names[i].toString().toStdString());
    const auto& meters = matchFixture()["meters"];
    expectMeters (truth.meters(), meters[0], meters[1], meters[2]);
    EXPECT_EQ (static_cast<int> (truth.routes().size()), s.load().data()["routes"].size());
    EXPECT_THROW (truth.host ("no-such-machine"), TruthError);
    EXPECT_THROW (truth.listener ("nobody", "nothing"), TruthError);
}

TEST (Truth, TheInstalledPathsAreChosenAsPythonChooses)
{
    Scratch s;
    EXPECT_EQ (truthPath (std::nullopt, nullptr), juce::File ("/usr/share/a3/a3-osc.json"));
    EXPECT_EQ (truthPath (std::nullopt, ""), juce::File ("/usr/share/a3/a3-osc.json"));
    EXPECT_EQ (truthPath (std::nullopt, "/tmp/x.json"), juce::File ("/tmp/x.json"));
    EXPECT_EQ (truthPath (s.truthFile, "/tmp/x.json"), s.truthFile);
}

TEST (Truth, ABrokenContractIsAnErrorNotACrash)
{
    Scratch s;
    EXPECT_TRUE (std::holds_alternative<TruthError> (Truth::load (s.dir.getChildFile ("missing.json"), std::nullopt)));
    EXPECT_TRUE (std::holds_alternative<TruthError> (Truth::load (s.write ("bad.json", "{"), std::nullopt)));
}

namespace
{
std::string decodedBase64 (const juce::var& encoded)
{
    juce::MemoryOutputStream decoded;
    EXPECT_TRUE (juce::Base64::convertFromBase64 (decoded, encoded.toString()));
    return std::string (static_cast<const char*> (decoded.getData()), decoded.getDataSize());
}
}

TEST (Truth, LoadedFingerprintIsPythons)
{
    Scratch s;
    const auto& shipped = parityFixture()["truth"];
    const auto plain = s.load();
    EXPECT_EQ (plain.canonical(), decodedBase64 (shipped["canonical_b64"]));
    EXPECT_EQ (plain.fingerprint(), shipped["fingerprint"].toString().toStdString());

    const auto& joined = parityFixture()["joined"];
    const auto network = s.write ("network.json", parityFixture()["network_text"].toString());
    const auto loaded = s.load (network);
    EXPECT_FALSE (loaded.networkProblem.has_value());
    EXPECT_FALSE (loaded.metersProblem.has_value());
    EXPECT_EQ (loaded.canonical(), decodedBase64 (joined["canonical_b64"]));
    EXPECT_EQ (loaded.fingerprint(), joined["fingerprint"].toString().toStdString());
}
