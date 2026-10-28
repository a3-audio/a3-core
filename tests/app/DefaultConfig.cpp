// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The shipped data/engine.json against what the rig does today: the patchbay's links for REAPER
// (a byte copy of a3-core's a3-patchbay.xml), Step 0's table of REAPER port -> engine port, and
// StemDeck's deck ports (SD section 8).
#include <algorithm>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <tuple>
#include "ConfigFile.h"
#include "EngineFixture.h"
#include "EnginePorts.h"
#include "SpatialTestData.h"

namespace
{
const juce::File defaultConfigFile { A3_DEFAULT_ENGINE_CONFIG };
const juce::File patchbayFile { A3_PATCHBAY_COPY };
const juce::File portMapFile { A3_REAPER_TO_ENGINE_PORTS };

a3app::AppConfig loadDefault()
{
    juce::String error;
    const auto config = a3app::loadAppConfig (defaultConfigFile, error);
    EXPECT_TRUE (config.has_value()) << error;
    return config.value_or (a3app::AppConfig {});
}

a3app::PortNames portsOf (const a3app::AppConfig& config)
{
    juce::String error;
    const auto spatial = a3::loadSpatialConfig (config.spatialDirectory, error);
    EXPECT_TRUE (spatial.has_value()) << error;
    return a3app::enginePortNames (config.engine, *spatial);
}

// The patchbay stores client names as regular expressions; the real JACK name has no escapes.
std::string unescaped (const juce::String& regex)
{
    return regex.replace ("\\", "").toStdString();
}

struct Socket { std::string client; std::vector<std::string> plugs; };
using Sockets = std::map<std::string, Socket>;

Sockets socketsIn (const juce::XmlElement& list)
{
    Sockets sockets;
    for (const auto* socket : list.getChildIterator())
    {
        Socket entry { unescaped (socket->getStringAttribute ("client")), {} };
        for (const auto* plug : socket->getChildIterator())
            entry.plugs.push_back (plug->getAllSubText().toStdString());
        sockets[socket->getStringAttribute ("name").toStdString()] = entry;
    }
    return sockets;
}

struct PatchbayLink { std::string from, to; };

// QjackCtl pairs the plugs of two connected sockets by position, as far as the shorter reaches.
std::vector<PatchbayLink> patchbayLinks()
{
    const auto document = juce::XmlDocument::parse (patchbayFile);
    EXPECT_NE (document, nullptr);
    const auto outputs = socketsIn (*document->getChildByName ("output-sockets"));
    const auto inputs = socketsIn (*document->getChildByName ("input-sockets"));
    std::vector<PatchbayLink> links;
    for (const auto* cable : document->getChildByName ("cables")->getChildIterator())
    {
        const auto& out = outputs.at (cable->getStringAttribute ("output").toStdString());
        const auto& in = inputs.at (cable->getStringAttribute ("input").toStdString());
        for (size_t i = 0; i < std::min (out.plugs.size(), in.plugs.size()); ++i)
            links.push_back ({ out.client + ":" + out.plugs[i], in.client + ":" + in.plugs[i] });
    }
    return links;
}

bool startsWith (const std::string& text, const std::string& prefix) { return text.rfind (prefix, 0) == 0; }

std::string afterColon (const std::string& name) { return name.substr (name.find (':') + 1); }

int numberAfter (const std::string& text, const std::string& prefix) { return std::stoi (text.substr (prefix.size())); }

// REAPER inputs 13-24 are StemDeck's stems, aux and AES67's cue: no engine counterpart (rev).
bool droppedInput (const std::string& reaperPort)
{
    return startsWith (reaperPort, "REAPER:in") && numberAfter (reaperPort, "REAPER:in") >= 13;
}

// The AES67 streams are named after their machine ("AES67 <host> out"): not known to a shipped file.
bool hostNamed (const std::string& client) { return startsWith (client, "AES67 "); }

bool contains (const std::vector<std::string>& list, const std::string& value)
{
    return std::find (list.begin(), list.end(), value) != list.end();
}
}

TEST (DefaultConfig, LoadsAndNamesOnlyRealPorts)
{
    const auto config = loadDefault();
    const auto ports = portsOf (config);
    EXPECT_EQ (config.engine.channels, 4);
    EXPECT_EQ (config.engine.decks, 2);
    EXPECT_EQ (config.engine.analogPairs, (std::vector<int> { 1, 2, 3, 4 }));
    EXPECT_EQ (config.engine.returnAnalogPair, 6);
    EXPECT_EQ (config.backend.type, a3app::BackendSettings::Type::jack);
    EXPECT_FALSE (config.io.jack.empty());

    std::set<std::string> known (ports.inputs.begin(), ports.inputs.end());
    known.insert (ports.outputs.begin(), ports.outputs.end());
    for (const auto& [port, peers] : config.io.jack)
    {
        EXPECT_TRUE (known.count (port) == 1) << port;
        EXPECT_FALSE (peers.empty()) << port;
        for (const auto& peer : peers)
            EXPECT_EQ (std::count (peer.begin(), peer.end(), ':'), 1) << port << " -> " << peer;
    }
    // Nothing escaped like a patchbay regex.
    for (const auto& [from, to] : config.io.directLinks)
    {
        EXPECT_EQ (from.find ('\\'), std::string::npos) << from;
        EXPECT_EQ (to.find ('\\'), std::string::npos) << to;
    }
}

TEST (DefaultConfig, ConnectsWhatThePatchbayConnectedForReaper)
{
    const auto config = loadDefault();
    const auto ports = portsOf (config);
    const auto map = juce::JSON::parse (portMapFile);
    ASSERT_TRUE (map.getDynamicObject() != nullptr);

    std::set<std::string> known (ports.inputs.begin(), ports.inputs.end());
    known.insert (ports.outputs.begin(), ports.outputs.end());

    int checked = 0, direct = 0, meters = 0, singleInstanceRecorders = 0;
    for (const auto& link : patchbayLinks())
    {
        const bool reaperIsSource = startsWith (link.from, "REAPER:");
        if (! reaperIsSource && ! startsWith (link.to, "REAPER:"))
            continue;
        const auto& reaperPort = reaperIsSource ? link.from : link.to;
        const auto& peer = reaperIsSource ? link.to : link.from;
        if (droppedInput (reaperPort) || hostNamed (peer.substr (0, peer.find (':'))))
            continue;
        if (startsWith (peer, "beat-analyzer:vu_"))   // O13: measured inside the engine (NoLinkToTheAnalyzersMeters)
        {
            ++meters;
            continue;
        }
        if (startsWith (peer, "StemDeck:rec_"))   // P20: the single-instance recorder is gone; M10 names StemDeck-A and AES67
        {
            ++singleInstanceRecorders;
            continue;
        }
        ASSERT_TRUE (map.getDynamicObject()->hasProperty (juce::String (reaperPort))) << reaperPort << " is not in Step 0's table";
        const auto target = map.getProperty (juce::String (reaperPort), {});
        if (target.isVoid())   // null in the table: a port the engine drops
            continue;
        const auto name = target.toString().toStdString();
        if (startsWith (name, "direct:"))
        {
            ++direct;
            EXPECT_TRUE (std::find (config.io.directLinks.begin(), config.io.directLinks.end(),
                                    std::make_pair (name.substr (7), peer)) != config.io.directLinks.end())
                << name << " -> " << peer;
            continue;
        }
        const auto port = afterColon (name);
        if (known.count (port) == 0)   // the 6.1 booth's fifth and sixth top: not in the 4.1 default
            continue;
        ++checked;
        const auto found = config.io.jack.find (port);
        ASSERT_TRUE (found != config.io.jack.end()) << port << " has no peers";
        EXPECT_TRUE (contains (found->second, peer)) << port << " should link " << peer << " (as " << reaperPort << " did)";
    }
    EXPECT_GE (checked, 15);
    EXPECT_EQ (meters, 50);   // every patchbay cable into beat-analyzer's vu_* ports
    EXPECT_EQ (direct, 0);    // REAPER's raw meter outs (out25-26, out31-38) fed only those meters
    EXPECT_EQ (singleInstanceRecorders, 2);   // rec_L/R -> StemDeck:rec_L/R, replaced by the M10 recorders below
    for (const auto& [port, local, sender] : { std::tuple<const char*, const char*, const char*> { "rec_L", "StemDeck-A:rec_L", "AES67 a3nuc1 out:send_CH1" },
                                               { "rec_R", "StemDeck-A:rec_R", "AES67 a3nuc1 out:send_CH2" } })
    {
        EXPECT_TRUE (contains (config.io.jack.at (port), local)) << port;
        EXPECT_TRUE (contains (config.io.jack.at (port), sender)) << port;
        EXPECT_FALSE (contains (config.io.jack.at (port), std::string ("StemDeck:") + port)) << port;
    }
}

TEST (DefaultConfig, DeckAIsLocalDeckBIsAes67)
{
    const auto config = loadDefault();
    const auto ports = portsOf (config);
    int deckPorts = 0;
    for (const auto& port : ports.inputs)
    {
        if (! startsWith (port, "deck"))
            continue;
        ++deckPorts;
        const auto letter = port.substr (4, port.find ('_') - 4);
        const auto tail = port.substr (port.find ('_') + 1);   // "7", "aux_3", "cue_9"
        const auto n = tail.substr (tail.find ('_') == std::string::npos ? 0 : tail.find ('_') + 1);
        const auto kind = std::isdigit ((unsigned char) tail[0]) ? 0 : (startsWith (tail, "aux") ? 1 : 2);
        const std::string local[] = { "bformat_", "aux_", "cue_" };
        const std::string remote[] = { "receive_MAIN", "receive_AUX", "receive_CUE" };
        const auto expected = letter == "A" ? "StemDeck-A:" + local[kind] + n
                                            : "AES67 deck " + letter + ":" + remote[kind] + n;
        const auto found = config.io.jack.find (port);
        ASSERT_TRUE (found != config.io.jack.end()) << port;
        EXPECT_EQ (found->second, std::vector<std::string> { expected }) << port;
    }
    EXPECT_EQ (deckPorts, 2 * 41);
    EXPECT_EQ (config.io.jack.at ("deckA_cue_9"), std::vector<std::string> { "StemDeck-A:cue_9" });
    EXPECT_EQ (config.io.jack.at ("deckB_aux_1"), std::vector<std::string> { "AES67 deck B:receive_AUX1" });
    for (const auto& retired : a3app::retiredDeckPorts (2))
        EXPECT_EQ (config.io.jack.count (retired), 0u) << retired;
}

TEST (DefaultConfig, CalibratedZonesHaveTrims)
{
    const auto config = loadDefault();
    ASSERT_TRUE (config.engine.trims.main.has_value());
    ASSERT_TRUE (config.engine.trims.booth.has_value());
    ASSERT_TRUE (config.engine.trims.phones.has_value());
    EXPECT_FLOAT_EQ (*config.engine.trims.main, -30.88f);
    EXPECT_FLOAT_EQ (*config.engine.trims.booth, -30.88f);
    EXPECT_FLOAT_EQ (*config.engine.trims.phones, 0.0f);
    EXPECT_FLOAT_EQ (config.engine.trims.rec, 0.0f);
    EXPECT_EQ (config.engine.filterKind, a3::FilterKind::svf);
    ASSERT_EQ (config.engine.delayModulation.size(), 2u);
    EXPECT_EQ (config.engine.delayModulation[0].parameter, "pitchL");
    EXPECT_FLOAT_EQ (config.engine.delayModulation[0].depth, 18.0f);
}

// O13: the engine measures the meters itself, so the default map links nothing to beat-analyzer's
// vu_* ports, and the direct capture links that only fed those meters are gone. The beat clock's
// input (bpm_1, from rec_L as the patchbay wired it) stays.
TEST (DefaultConfig, NoLinkToTheAnalyzersMeters)
{
    const auto config = loadDefault();
    for (const auto& [port, peers] : config.io.jack)
        for (const auto& peer : peers)
            EXPECT_FALSE (startsWith (peer, "beat-analyzer:vu_")) << port << " -> " << peer;
    for (const auto& [from, to] : config.io.directLinks)
    {
        EXPECT_FALSE (startsWith (to, "beat-analyzer:vu_")) << from << " -> " << to;
        EXPECT_FALSE (startsWith (from, "beat-analyzer:vu_")) << from << " -> " << to;
    }
    EXPECT_TRUE (config.io.directLinks.empty()) << "every direct link of Plan 4's map only fed the analyzer's meters";
    const auto rec = config.io.jack.find ("rec_L");
    ASSERT_TRUE (rec != config.io.jack.end());
    EXPECT_TRUE (contains (rec->second, "beat-analyzer:bpm_1"));
}

// M10: every deck host records, and the source is always the REC bus. The default map is a3nuc1's:
// deck A is local ("StemDeck-A" takes rec_L/R as inputs) and the Core's AES67 sender (a patchbay
// socket "AES67 .* out", plugs send_CH1/2) carries the same bus to the other hosts.
TEST (DefaultConfig, RecBusFeedsTheLocalStemDeck)
{
    const auto config = loadDefault();
    EXPECT_TRUE (contains (config.io.jack.at ("rec_L"), "StemDeck-A:rec_L"));
    EXPECT_TRUE (contains (config.io.jack.at ("rec_R"), "StemDeck-A:rec_R"));
}

TEST (DefaultConfig, RecBusFeedsAes67ChannelsOneAndTwo)
{
    const auto config = loadDefault();
    EXPECT_TRUE (contains (config.io.jack.at ("rec_L"), "AES67 a3nuc1 out:send_CH1"));
    EXPECT_TRUE (contains (config.io.jack.at ("rec_R"), "AES67 a3nuc1 out:send_CH2"));
}

TEST (DefaultConfig, NothingButTheRecBusFeedsARecorder)
{
    const auto config = loadDefault();
    const auto isRecorder = [] (const std::string& peer)
    {
        return (startsWith (peer, "StemDeck") && (afterColon (peer) == "rec_L" || afterColon (peer) == "rec_R"))
            || startsWith (peer, "AES67 a3nuc1 out:send_CH");
    };
    for (const auto& [port, peers] : config.io.jack)
        for (const auto& peer : peers)
            if (isRecorder (peer))
                EXPECT_TRUE (port == "rec_L" || port == "rec_R") << port << " -> " << peer;
    for (const auto& [from, to] : config.io.directLinks)
        EXPECT_FALSE (isRecorder (to) || isRecorder (from)) << from << " -> " << to;

    const auto recorders = [&] (const char* port)
    {
        std::set<std::string> found;
        for (const auto& peer : config.io.jack.at (port))
            if (isRecorder (peer))
                found.insert (peer);
        return found;
    };
    EXPECT_EQ (recorders ("rec_L"), (std::set<std::string> { "StemDeck-A:rec_L", "AES67 a3nuc1 out:send_CH1" }));
    EXPECT_EQ (recorders ("rec_R"), (std::set<std::string> { "StemDeck-A:rec_R", "AES67 a3nuc1 out:send_CH2" }));
}
