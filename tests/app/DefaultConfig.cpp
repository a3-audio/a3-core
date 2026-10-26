// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The shipped data/engine.json against what the rig does today: the patchbay's links for REAPER
// (a byte copy of a3-core's a3-patchbay.xml), Step 0's table of REAPER port -> engine port, and
// StemDeck's deck ports (SD section 8).
#include <algorithm>
#include <gtest/gtest.h>
#include <map>
#include <set>
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

    int checked = 0, direct = 0;
    for (const auto& link : patchbayLinks())
    {
        const bool reaperIsSource = startsWith (link.from, "REAPER:");
        if (! reaperIsSource && ! startsWith (link.to, "REAPER:"))
            continue;
        const auto& reaperPort = reaperIsSource ? link.from : link.to;
        const auto& peer = reaperIsSource ? link.to : link.from;
        if (droppedInput (reaperPort) || hostNamed (peer.substr (0, peer.find (':'))))
            continue;
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
    EXPECT_GE (checked, 45);
    EXPECT_EQ (direct, 10);   // REAPER's raw meter outs: out25-26 and out31-38
}

TEST (DefaultConfig, DeckSlotsLinkToTheirStemDeck)
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
        const auto tail = port.substr (port.find ('_') + 1);   // "7", "cue_L", "aux_R"
        const auto found = config.io.jack.find (port);
        ASSERT_TRUE (found != config.io.jack.end()) << port;
        const auto expectedPort = std::isdigit ((unsigned char) tail[0]) ? "bformat_" + tail : tail;
        EXPECT_EQ (found->second, std::vector<std::string> { "StemDeck-" + letter + ":" + expectedPort }) << port;
    }
    EXPECT_EQ (deckPorts, 2 * (16 + 2 + 2));
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
