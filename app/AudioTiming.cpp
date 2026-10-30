// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioTiming.h"
#include "SystemDevice.h"
#include <algorithm>
#include <cstdio>
#include <set>

namespace a3app
{
const std::vector<int>& pipewireBlocks()
{
    static const std::vector<int> blocks { 32, 64, 128, 256, 512, 1024 };
    return blocks;
}

const std::vector<int>& pipewirePeriods()
{
    static const std::vector<int> periods { 2, 3 };
    return periods;
}

AudioTiming timingOf (const BackendSettings& backend) { return { backend.blockSize, backend.periods }; }

Command forceQuantumCommand (int block)
{
    const int forced = block == defaultBlock ? 0 : block;
    return { "pw-metadata", "-n", "settings", "0", "clock.force-quantum", std::to_string (forced) };
}

Command restartWirePlumberCommand() { return { "systemctl", "--user", "restart", "wireplumber" }; }

namespace
{
bool startsWith (const std::string& text, const std::string& prefix) { return text.rfind (prefix, 0) == 0; }

bool isAlsaNode (const std::string& node) { return startsWith (node, "alsa_"); }

// What "system" is on one side: the configured node, else the current default when it is an ALSA node, else none.
std::string systemNode (const std::string& configured, const std::string& current)
{
    if (! configured.empty())
        return configured;
    return isAlsaNode (current) ? current : std::string();
}

// The node a JACK peer belongs to, or "" when it is no ALSA device.
std::string nodeOfPeer (const std::string& peer, const DefaultNodes& defaults)
{
    if (startsWith (peer, "system:playback_"))
        return systemNode (defaults.configuredSink, defaults.sink);
    if (startsWith (peer, "system:capture_"))
        return systemNode (defaults.configuredSource, defaults.source);
    const auto client = peer.substr (0, peer.find (':'));
    return startsWith (client, "alsa_") && client.size() < peer.size() ? client : std::string();
}
}

std::vector<std::string> alsaNodesOf (const IoMap& io, const DefaultNodes& defaults)
{
    std::set<std::string> nodes;
    const auto add = [&] (const std::string& node)
    {
        if (! node.empty())
            nodes.insert (node);
    };
    for (const auto& [port, peers] : io.jack)
        for (const auto& peer : peers)
            add (nodeOfPeer (peer, defaults));
    for (const auto& [from, to] : io.directLinks)
    {
        add (nodeOfPeer (from, defaults));
        add (nodeOfPeer (to, defaults));
    }
    const bool analogPair = io.analogLoop && (io.analogLoop->output > 0 || io.analogLoop->input > 0);
    if (io.roundTripLoop || analogPair)   // the loops' links are system's (ConnectionPlan.h)
    {
        add (systemNode (defaults.configuredSink, defaults.sink));
        add (systemNode (defaults.configuredSource, defaults.source));
    }
    return { nodes.begin(), nodes.end() };
}

juce::File periodsDropInFile (const juce::File& home)
{
    return home.getChildFile (".config/wireplumber/wireplumber.conf.d/80-a3-engine-alsa.conf");
}

juce::String periodsDropInText (const std::vector<std::string>& nodes, int periods)
{
    juce::String text;
    text << "# Written by a3-core's engine (the AUDIO page's Periods); removed when Periods goes back to "
         << defaultPeriods << ".\n"
         << "# Only the engine's own ALSA device. Edits here are overwritten by the next change on the page.\n"
         << "monitor.alsa.rules = [\n"
         << "  {\n"
         << "    matches = [\n";
    for (const auto& node : nodes)
        text << "      { node.name = \"" << juce::String (node) << "\" }\n";
    text << "    ]\n"
         << "    actions = {\n"
         << "      update-props = {\n"
         << "        api.alsa.period-num = " << periods << "\n"
         << "      }\n"
         << "    }\n"
         << "  }\n"
         << "]\n";
    return text;
}

juce::Result writePeriodsDropIn (const juce::File& file, const std::vector<std::string>& nodes, int periods)
{
    if (periods == defaultPeriods)
    {
        if (file.exists() && ! file.deleteFile())
            return juce::Result::fail ("cannot remove " + file.getFullPathName());
        return juce::Result::ok();
    }
    if (nodes.empty())
        return juce::Result::fail ("no ALSA device in the I/O map: no periods to set");
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("cannot create " + file.getParentDirectory().getFullPathName());
    // Not juce::TemporaryFile: its name ends in .conf too, and WirePlumber reads every *.conf in the directory.
    const auto temporary = file.getSiblingFile ("." + file.getFileName() + ".tmp");
    if (! temporary.replaceWithText (periodsDropInText (nodes, periods), false, false, "\n"))
        return juce::Result::fail ("cannot write " + temporary.getFullPathName());
    // rename(2) replaces the target atomically (juce::File::moveFileTo deletes it first).
    if (std::rename (temporary.getFullPathName().toRawUTF8(), file.getFullPathName().toRawUTF8()) != 0)
    {
        temporary.deleteFile();
        return juce::Result::fail ("cannot replace " + file.getFullPathName());
    }
    return juce::Result::ok();
}

juce::String timingText (int block, int periods)
{
    return "Block " + juce::String (block) + " frames " + juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + " "
           + juce::String (periods) + " periods";
}
}
