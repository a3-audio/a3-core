// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScenarioRunner.h"

#include <cmath>
#include <deque>
#include <memory>

#include "B0Truth.h"
#include "Core.h"
#include "FakeNet.h"
#include "Json.h"
#include "LatestWins.h"
#include "OscWire.h"

namespace scenarios
{
namespace
{
// scenario_core.py's constants: the socket Core's replays leave from, the first start-up token.
constexpr int selfSocketPort = 50000;
constexpr long long tokenBase = 1000000;
const std::string coreBootId = "core-boot-0001";
const juce::String stateMark = "<state>";
const juce::StringArray stateFiles { "state.json", "evening.json", "seen.json" };

juce::var readJson (const juce::File& file)
{
    juce::String error;
    auto value = a3core::parseJson (file.loadFileAsString(), error);
    if (error.isNotEmpty())
        throw std::runtime_error ((file.getFullPathName() + ": " + error).toStdString());
    return value;
}

std::string text (const juce::var& value) { return value.toString().toStdString(); }

a3core::Endpoint endpointOf (const juce::var& spec)
{
    const auto whole = spec.toString();
    return { whole.upToLastOccurrenceOf (":", false, false).toStdString(),
             whole.fromLastOccurrenceOf (":", false, false).getIntValue() };
}

a3core::Endpoint fromOf (const juce::var& step)
{
    const auto& from = step["from"];
    return { text (from[0]), static_cast<int> (from[1]) };
}

std::vector<uint8_t> base64 (const juce::var& encoded)
{
    juce::MemoryOutputStream decoded;
    juce::Base64::convertFromBase64 (decoded, encoded.toString());
    auto* bytes = static_cast<const uint8_t*> (decoded.getData());
    return { bytes, bytes + decoded.getDataSize() };
}

a3core::OscArg argOf (const juce::var& pair)
{
    const auto tag = static_cast<char> (pair[0].toString()[0]);
    const auto& value = pair[1];
    switch (tag)
    {
        case 'i': return { 'i', static_cast<int32_t> (static_cast<juce::int64> (value)) };
        case 'h': return { 'h', static_cast<int64_t> (static_cast<juce::int64> (value)) };
        case 'f': return { 'f', static_cast<float> (static_cast<double> (value)) };
        case 'd': return { 'd', static_cast<double> (value) };
        case 's': return { 's', text (value) };
        case 'b': return { 'b', base64 (value) };
        case 'T': case 'F': case 'N': return { tag, std::monostate {} };
        default: throw std::runtime_error (std::string ("a tag the scenarios do not know: ") + tag);
    }
}

std::vector<uint8_t> messageOf (const juce::var& osc)
{
    a3core::OscMessage message { text (osc[0]), {} };
    for (int i = 1; i < osc.size(); ++i)
        message.args.push_back (argOf (osc[i]));
    return a3core::encodeMessage (message);
}

void writeFiles (const juce::var& files, const juce::File& coreDir)
{
    coreDir.createDirectory();
    auto* object = files.getDynamicObject();
    if (object == nullptr)
        return;
    for (const auto& file : object->getProperties())
    {
        if (! stateFiles.contains (file.name.toString()))
            throw std::runtime_error ("files: " + file.name.toString().toStdString() + " is none of the state files");
        const auto content = file.value.isString() ? file.value.toString()
                                                   : juce::String (a3core::dumpIndented (file.value, 2)) + "\n";
        coreDir.getChildFile (file.name.toString()).replaceWithText (content, false, false, "\n");
    }
}

// One scenario's world: the clock, the outbox, the Core, the datagrams it sent itself.
struct Run
{
    explicit Run (const juce::var& scenario, const juce::File& stateDirToUse)
        : truth (b0::truth()), net (clock), stateDir (stateDirToUse)
    {
        writeFiles (scenario["files"], stateDir.getChildFile ("a3-core"));
        net.toSelf = [this] (const std::vector<uint8_t>& data) { toSelf.push_back (data); };

        const auto& given = scenario["args"];
        a3core::CoreArgs coreArgs { truth.host ("any"), truth.port ("core", "osc"),
                                    given.hasProperty ("mixer") ? endpointOf (given["mixer"]) : truth.endpoint ("mixer", "osc"),
                                    given.hasProperty ("motion") ? endpointOf (given["motion"]) : truth.endpoint ("motion", "osc"),
                                    b0::strings (given["subscriber"]) };
        const auto stateText = stateDir.getChildFile ("a3-core").getFullPathName();
        a3core::CoreEnvironment environment {
            truth, net, clock,
            [this, stateText] (const std::string& line, bool toStderr) {
                net.journal (juce::String::fromUTF8 (line.c_str()).replace (stateText, stateMark).toStdString(), toStderr);
            },
            stateDir,
            [] { return coreBootId; },
            [this] { return tokenBase + ++tokens; },
            { a3core::loopbackAddress(), selfSocketPort },
            [this] (const juce::File& written) { net.file (written.getFileName().toStdString(), readJson (written)); } };
        core = std::make_unique<a3core::Core> (environment, coreArgs);
    }

    // One pass of the serve loop over what Core sent its own port: the newest position wins.
    void feedBackOwnPort()
    {
        while (! toSelf.empty())
        {
            std::vector<std::vector<uint8_t>> waiting;
            waiting.swap (toSelf);
            const auto kept = a3core::latestWins (std::move (waiting), [this] (const std::vector<uint8_t>& data) {
                return a3core::positionKey (data.data(), data.size(),
                                            [this] (const std::string& address) { return a3core::isPosition (truth, address); });
            });
            for (const auto& data : kept)
                core->handlePacket ({ a3core::loopbackAddress(), selfSocketPort }, data.data(), data.size());
        }
    }

    void step (const juce::var& step)
    {
        clock.at = static_cast<double> (step["at"]);
        if (step.hasProperty ("osc"))
        {
            const auto data = messageOf (step["osc"]);
            core->handlePacket (fromOf (step), data.data(), data.size());
        }
        else if (step.hasProperty ("packet"))
        {
            const auto data = base64 (step["packet"]);
            core->handlePacket (fromOf (step), data.data(), data.size());
        }
        else if (step.hasProperty ("vu"))
        {
            const auto data = base64 (step["vu"]);
            core->handleVu (fromOf (step), data.data(), data.size());
        }
        else if (static_cast<bool> (step["tick"]))
            core->tick();
        else if (static_cast<bool> (step["flush"]))
            core->stop();
        else
            throw std::runtime_error ("a step that does nothing: " + text (juce::JSON::toString (step, true)));
        feedBackOwnPort();
    }

    const a3core::Truth& truth;
    fakenet::FakeClock clock;
    fakenet::FakeNet net;
    juce::File stateDir;
    long long tokens = 0;
    std::vector<std::vector<uint8_t>> toSelf;
    std::unique_ptr<a3core::Core> core;
};

std::string shown (const juce::var& entry) { return a3core::canonicalBytes (entry); }

bool sameArg (const juce::var& expected, const juce::var& actual)
{
    const auto tag = expected[0].toString();
    if (tag != actual[0].toString())
        return false;
    const auto& want = expected[1];
    const auto& got = actual[1];
    if (tag == "f")
        return static_cast<float> (static_cast<double> (want)) == static_cast<float> (static_cast<double> (got));
    if (tag == "d")
        return static_cast<double> (want) == static_cast<double> (got);
    if (tag == "i" || tag == "h")
        return static_cast<juce::int64> (want) == static_cast<juce::int64> (got);
    if (tag == "s" || tag == "b")
        return want.toString() == got.toString();
    return true;   // T, F, N: the tag is the value
}

bool sameArgs (const juce::var& expected, const juce::var& actual)
{
    if (expected.size() != actual.size())
        return false;
    for (int i = 0; i < expected.size(); ++i)
        if (! sameArg (expected[i], actual[i]))
            return false;
    return true;
}

bool sameEntry (const juce::var& want, const juce::var& got)
{
    if (want.hasProperty ("file"))
        return got.hasProperty ("file") && want["file"].toString() == got["file"].toString()
            && a3core::pythonEquals (want["json"], got["json"]);
    if (static_cast<double> (want["at"]) != static_cast<double> (got["at"]))
        return false;
    if (want.hasProperty ("journal"))
        return got.hasProperty ("journal") && want["journal"].toString() == got["journal"].toString()
            && static_cast<bool> (want["stderr"]) == static_cast<bool> (got["stderr"]);
    for (const auto* field : { "to", "host" })
        if (want[field].toString() != got[field].toString())
            return false;
    if (static_cast<int> (want["port"]) != static_cast<int> (got["port"]))
        return false;
    if (want.hasProperty ("raw"))
        return got.hasProperty ("raw") && want["raw"].toString() == got["raw"].toString();
    return got.hasProperty ("address") && want["address"].toString() == got["address"].toString()
        && sameArgs (want["args"], got["args"]);
}
}

juce::File directory() { return juce::File (A3_CORE_TEST_DATA).getSiblingFile ("scenarios"); }

std::vector<std::string> compare (const juce::Array<juce::var>& expected, const juce::Array<juce::var>& actual)
{
    constexpr size_t mostShown = 12;
    std::vector<std::string> differences;
    const int both = std::min (expected.size(), actual.size());
    for (int i = 0; i < both && differences.size() < mostShown; ++i)
        if (! sameEntry (expected[i], actual[i]))
            differences.push_back ("#" + std::to_string (i) + ": expected " + shown (expected[i]) + "\n      got " + shown (actual[i]));
    if (expected.size() != actual.size())
        differences.push_back ("expected " + std::to_string (expected.size()) + " entries, got " + std::to_string (actual.size())
                               + (actual.size() > both ? "; the first extra: " + shown (actual[both])
                                                       : "; the first missing: " + shown (expected[both])));
    return differences;
}

Outcome run (const std::string& name)
{
    const auto scenarioFile = directory().getChildFile (name + ".json");
    const auto scenario = readJson (scenarioFile);
    const auto expected = readJson (directory().getChildFile (name + ".expected.json"));

    auto stateDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("a3-scenario-" + name, "", false);
    stateDir.createDirectory();

    struct Removed
    {
        juce::File dir;
        ~Removed() { dir.deleteRecursively(); }
    } removed { stateDir };

    Outcome outcome;
    {
        Run run (scenario, stateDir);
        run.core->start();
        run.feedBackOwnPort();
        if (const auto* steps = scenario["steps"].getArray())
            for (const auto& step : *steps)
                run.step (step);
        outcome.transcript = run.net.transcript;
    }

    if (const auto* want = expected.getArray())
        outcome.differences = compare (*want, outcome.transcript);
    else
        outcome.differences.push_back (name + ".expected.json is not a list");
    if (! outcome.differences.empty())
    {
        outcome.actualFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("a3-scenario-" + name + ".actual.json");
        outcome.actualFile.replaceWithText (juce::JSON::toString (juce::var (outcome.transcript), false));
    }
    return outcome;
}
}
